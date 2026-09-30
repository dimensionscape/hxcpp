/* ************************************************************************ */
/*																			*/
/*  MYSQL 5.0 Protocol Implementation 										*/
/*  Copyright (c)2008 Nicolas Cannasse										*/
/*
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include "my_proto.h"

static void error( MYSQL *m, const char *err, const char *param ) {
	if( param ) {
		unsigned int max = MAX_ERR_SIZE - (strlen(err) + 3);
		if( strlen(param) > max ) {
			char *p2 = (char*)malloc(max + 1);
			memcpy(p2,param,max-3);
			p2[max - 3] = '.';
			p2[max - 2] = '.';
			p2[max - 1] = '.';
			p2[max] = 0;
			snprintf(m->last_error,sizeof(m->last_error),err,param);
			free(p2);
			return;
		}
	}
	snprintf(m->last_error,sizeof(m->last_error),err,param);
	m->errcode = -1;
}

static void save_error( MYSQL *m, MYSQL_PACKET *p ) {
	int ecode;
	p->pos = 0;
	// seems like we sometimes get some FFFFFF sequences before
	// the actual error...
	do {
		if( myp_read_byte(p) != 0xFF ) {
			m->errcode = -1;
			error(m,"Failed to decode error",NULL);
			return;
		}
		ecode = myp_read_ui16(p);
	} while( ecode == 0xFFFF );
	if( m->is41 && p->buf[p->pos] == '#' )
		p->pos += 6; // skip sqlstate marker
	error(m,"%s",myp_read_string(p));
	m->errcode = ecode;
}

/*
	The rest of an OK packet, after its 0x00: what the statement did, and the
	session state the server reports after it. The status flags are the only
	place the server says whether a transaction is open and whether
	backslashes still escape, and they were read once, from the greeting, and
	never again: after SET sql_mode = 'NO_BACKSLASH_ESCAPES' a quote was still
	escaped with a backslash, which that mode reads as a literal backslash
	followed by the end of the string.
*/
static void myp_read_ok( MYSQL *m, MYSQL_PACKET *p ) {
	m->affected_rows = myp_read_bin64(p);
	m->last_insert_id = myp_read_bin64(p);
	if( m->is41 && p->size - p->pos >= 4 ) {
		m->infos.server_status = myp_read_ui16(p);
		m->warning_count = myp_read_ui16(p);
	}
}

/* The same flags, from the EOF packet that ends a result. */
static void myp_read_eof( MYSQL *m, MYSQL_PACKET *p ) {
	if( m->is41 && p->size >= 5 ) {
		p->pos = 1;
		m->warning_count = myp_read_ui16(p);
		m->infos.server_status = myp_read_ui16(p);
	}
}

static int myp_ok( MYSQL *m, int allow_others ) {
	int code;
	MYSQL_PACKET *p = &m->packet;
	if( !myp_read_packet(m,p) ) {
		error(m,"Failed to read packet",NULL);
		return 0;
	}
	code = myp_read_byte(p);
	if( code == 0x00 ) {
		// A result set's header is not an OK packet even when its first
		// byte could be read as one; the caller parses those.
		if( !allow_others )
			myp_read_ok(m,p);
		return 1;
	}
	if( code == 0xFF )
		save_error(m,p);
	else if( allow_others )
		return 1;
	else
		error(m,"Invalid packet error",NULL);
	return 0;
}

static void myp_close( MYSQL *m ) {
	if( m->s != INVALID_SOCKET )
		psock_close(m->s);
	m->s = INVALID_SOCKET;
}

MYSQL *mysql_init( void *unused ) {
	MYSQL *m = (MYSQL*)malloc(sizeof(struct _MYSQL));
	psock_init();
	memset(m,0,sizeof(struct _MYSQL));
	m->s = INVALID_SOCKET;
	error(m,"NO ERROR",NULL);
	m->errcode = 0;
	m->last_field_count = -1;
	m->last_insert_id = -1;
	m->affected_rows = -1;
	return m;
}

// The nonce every auth plugin scrambles with: the first 20 bytes of the auth
// data in the greeting or in an auth switch request.
#define NONCE_SIZE 20
#define AUTH_MAX 512

static const char *NATIVE_PASSWORD = "mysql_native_password";
static const char *CACHING_SHA2_PASSWORD = "caching_sha2_password";
static const char *SHA256_PASSWORD = "sha256_password";
static const char *CLEAR_PASSWORD = "mysql_clear_password";

static int known_plugin( const char *plugin ) {
	return strcmp(plugin,NATIVE_PASSWORD) == 0 || strcmp(plugin,CACHING_SHA2_PASSWORD) == 0
		|| strcmp(plugin,SHA256_PASSWORD) == 0 || strcmp(plugin,CLEAR_PASSWORD) == 0;
}

/*
	The password, NUL-terminated, XORed with the nonce and encrypted with the
	server's RSA key: what caching_sha2_password and sha256_password take
	over a connection that is not encrypted.
*/
static int rsa_password( MYSQL *m, const char *pem, const char *pass, const unsigned char *nonce, unsigned char *out ) {
	int length = (int)strlen(pass) + 1;
	int i, n;
	unsigned char *clear = (unsigned char*)malloc(length);
	for(i=0;i<length;i++)
		clear[i] = (unsigned char)(i < length - 1 ? pass[i] : 0) ^ nonce[i % NONCE_SIZE];
	n = myp_rsa_encrypt(m,pem,clear,length,out,AUTH_MAX);
	memset(clear,0,length);
	free(clear);
	return n;
}

/*
	The answer to a plugin's first challenge, into `out` (AUTH_MAX bytes):
	its length, or -1 with the error set.
*/
static int auth_response( MYSQL *m, const char *plugin, const char *pass, const unsigned char *nonce, unsigned char *out ) {
	int length = (int)strlen(pass);
	if( strcmp(plugin,NATIVE_PASSWORD) == 0 ) {
		if( !length )
			return 0;
		myp_encrypt_password(pass,(const char*)nonce,out);
		return SHA1_SIZE;
	}
	if( strcmp(plugin,CACHING_SHA2_PASSWORD) == 0 ) {
		// XOR(SHA256(password), SHA256(SHA256(SHA256(password)), nonce))
		unsigned char stage1[32], stage2[32], digest[32], both[32 + NONCE_SIZE];
		int i;
		if( !length )
			return 0;
		myp_sha256((const unsigned char*)pass,length,stage1);
		myp_sha256(stage1,32,stage2);
		memcpy(both,stage2,32);
		memcpy(both + 32,nonce,NONCE_SIZE);
		myp_sha256(both,32 + NONCE_SIZE,digest);
		for(i=0;i<32;i++)
			out[i] = stage1[i] ^ digest[i];
		return 32;
	}
	if( strcmp(plugin,SHA256_PASSWORD) == 0 || strcmp(plugin,CLEAR_PASSWORD) == 0 ) {
		if( m->tls || !length ) {
			if( length + 1 > AUTH_MAX ) {
				error(m,"Password too long",NULL);
				return -1;
			}
			memcpy(out,pass,length + 1);
			return length + 1;
		}
		if( strcmp(plugin,CLEAR_PASSWORD) == 0 ) {
			error(m,"The account uses mysql_clear_password, which would send the password in the clear: connect with TLS",NULL);
			return -1;
		}
		if( m->options.server_public_key )
			return rsa_password(m,m->options.server_public_key,pass,nonce,out);
		if( m->options.allow_public_key_retrieval ) {
			out[0] = 1; // asks for the server's public key
			return 1;
		}
		error(m,"The account uses sha256_password, which needs TLS, the server's RSA public key (serverPublicKey), or allowPublicKeyRetrieval",NULL);
		return -1;
	}
	snprintf(m->last_error,sizeof(m->last_error),"Unsupported authentication plugin '%s'",plugin);
	m->errcode = 2059; // CR_AUTH_PLUGIN_CANNOT_LOAD
	return -1;
}

static int auth_send( MYSQL *m, const unsigned char *data, int length, int *pcount ) {
	MYSQL_PACKET *p = &m->packet;
	myp_begin_packet(p,length);
	myp_write(p,data,length);
	if( !myp_send_packet(m,p,pcount) ) {
		error(m,"Failed to send authentication packet",NULL);
		return 0;
	}
	return 1;
}

static MYSQL *connect_failed( MYSQL *m ) {
	myp_tls_free(m);
	myp_close(m);
	return NULL;
}

MYSQL *mysql_real_connect( MYSQL *m, const char *host, const char *user, const char *pass, void *unused, int port, const char *socket, int options ) {
	PHOST h;
	unsigned char nonce[NONCE_SIZE + 13];
	char plugin[64];
	unsigned char auth[AUTH_MAX];
	int authlen;
	unsigned int flags;
	MYSQL_PACKET *p = &m->packet;
	int pcount = 1;
	if( !pass )
		pass = "";
	if( !user )
		user = "";
	if( socket && *socket ) {
		error(m,"Unix Socket connections are not supported",NULL);
		return NULL;
	}
	h = phost_resolve(host);
	if( h == UNRESOLVED_HOST ) {
		error(m,"Failed to resolve host '%s'",host);
		return NULL;
	}
	m->s = psock_create();
	if( m->s == INVALID_SOCKET ) {
		error(m,"Failed to create socket",NULL);
		return NULL;
	}
	psock_set_fastsend(m->s,1);
	psock_set_timeout(m->s,50); // 50 seconds
	if( psock_connect(m->s,h,port) != PS_OK ) {
		myp_close(m);
		error(m,"Failed to connect on host '%s'",host);
		return NULL;
	}
	if( !myp_read_packet(m,p) ) {
		myp_close(m);
		error(m,"Failed to read handshake packet",NULL);
		return NULL;
	}
	// process handshake packet
	{
		char filler[13];
		unsigned int len;
		memset(nonce,0,sizeof(nonce));
		strcpy(plugin,NATIVE_PASSWORD);
		m->infos.proto_version = myp_read_byte(p);
		// this seems like an error packet
		if( m->infos.proto_version == 0xFF ) {
			myp_close(m);
			save_error(m,p);
			return NULL;
		}
		m->infos.server_version = strdup(myp_read_string(p));
		m->infos.thread_id = myp_read_int(p);
		myp_read(p,nonce,8);
		myp_read_byte(p); // should be 0
		m->infos.server_flags = myp_read_ui16(p);
		m->infos.server_charset = myp_read_byte(p);
		m->infos.server_status = myp_read_ui16(p);
		m->infos.server_flags |= myp_read_ui16(p) << 16;
		len = myp_read_byte(p);
		myp_read(p,filler,10);
		// try to disable 41
		m->is41 = (m->infos.server_flags & FL_PROTOCOL_41) != 0;
		if( !p->error && m->is41 ) {
			// The rest of the nonce: max(13, length - 8) bytes, of which the
			// first 12 complete it.
			unsigned char part2[256];
			int size2 = (int)len - 8;
			if( size2 < 13 )
				size2 = 13;
			myp_read(p,part2,size2);
			memcpy(nonce + 8,part2,12);
		}
		if( p->pos < p->size ) {
			// 5.5+: the auth plugin the server expects to be answered with.
			const char *named = myp_read_string(p);
			if( (m->infos.server_flags & FL_PLUGIN_AUTH) && *named && strlen(named) < sizeof(plugin) )
				strcpy(plugin,named);
		}
		if( p->error ) {
			myp_close(m);
			error(m,"Failed to decode server handshake",NULL);
			return NULL;
		}
		// A plugin this client does not speak: answer as mysql_native_password
		// and let the server switch the client to what the account uses.
		if( !known_plugin(plugin) )
			strcpy(plugin,NATIVE_PASSWORD);
	}

	flags = m->infos.server_flags & (FL_LONG_PASSWORD | FL_PROTOCOL_41 | FL_TRANSACTIONS | FL_SECURE_CONNECTION | FL_PLUGIN_AUTH | FL_PLUGIN_AUTH_LENENC);

	// TLS: asked for with an SSLRequest -- the first 32 bytes of a handshake
	// response, with CLIENT_SSL set -- after which both sides run the TLS
	// handshake and the real response follows, encrypted.
	if( m->is41 && m->options.ssl_mode != MYSQL_SSL_DISABLED ) {
		if( m->infos.server_flags & FL_SSL ) {
			char filler[23];
			flags |= FL_SSL;
			myp_begin_packet(p,32);
			myp_write_int(p,flags);
			myp_write_int(p,0x01000000);
			myp_write_byte(p,m->infos.server_charset);
			memset(filler,0,23);
			myp_write(p,filler,23);
			if( !myp_send_packet(m,p,&pcount) ) {
				error(m,"Failed to send TLS request",NULL);
				return connect_failed(m);
			}
			if( !myp_tls_start(m,host) )
				return connect_failed(m);
		} else if( m->options.ssl_mode >= MYSQL_SSL_REQUIRED ) {
			myp_close(m);
			error(m,"The server does not support TLS, and the connection requires it",NULL);
			return NULL;
		}
	}

	// fill answer packet
	if( m->is41 ) {
		char filler[23];
		authlen = auth_response(m,plugin,pass,nonce,auth);
		if( authlen < 0 )
			return connect_failed(m);
		myp_begin_packet(p,128);
		myp_write_int(p,flags);
		myp_write_int(p,0x01000000);
		myp_write_byte(p,m->infos.server_charset);
		memset(filler,0,23);
		myp_write(p,filler,23);
		myp_write_string(p,user);
		if( flags & FL_PLUGIN_AUTH_LENENC ) {
			myp_write_bin(p,authlen);
			myp_write(p,auth,authlen);
		} else if( flags & FL_SECURE_CONNECTION ) {
			if( authlen > 255 ) {
				error(m,"The server cannot take an authentication response this long",NULL);
				return connect_failed(m);
			}
			myp_write_byte(p,authlen);
			myp_write(p,auth,authlen);
		} else {
			myp_write(p,auth,authlen);
			myp_write_byte(p,0);
		}
		if( flags & FL_PLUGIN_AUTH )
			myp_write_string(p,plugin);
	} else {
		myp_begin_packet(p,128);
		myp_write_ui16(p,flags);
		// max_packet_size
		myp_write_byte(p,0xFF);
		myp_write_byte(p,0xFF);
		myp_write_byte(p,0xFF);
		myp_write_string(p,user);
		if( *pass ) {
			char hpass[SEED_LENGTH_323 + 1];
			myp_encrypt_pass_323(pass,(const char*)nonce,hpass);
			hpass[SEED_LENGTH_323] = 0;
			myp_write(p,hpass,SEED_LENGTH_323 + 1);
		} else
			myp_write_bin(p,0);
	}
	// send connection packet
	if( !myp_send_packet(m,p,&pcount) ) {
		error(m,"Failed to send connection packet",NULL);
		return connect_failed(m);
	}

	// The server answers until it accepts or refuses: an auth switch to
	// another plugin, more data for caching_sha2_password, a public key.
	while( 1 ) {
		int code;
		if( !myp_read_packet(m,p) ) {
			error(m,"Failed to read packet",NULL);
			return connect_failed(m);
		}
		// increase packet counter (because we read one packet)
		pcount++;
		code = myp_read_byte(p);
		if( code == 0x00 ) { // OK packet
			myp_read_ok(m,p);
			break;
		}
		if( code == 0xFF ) { // ERROR
			save_error(m,p);
			return connect_failed(m);
		}
		if( code == 0xFE ) {
			if( p->size == 1 ) {
				// we are asked to send old password authentification
				char hpass[SEED_LENGTH_323 + 1];
				myp_encrypt_pass_323(pass,(const char*)nonce,hpass);
				hpass[SEED_LENGTH_323] = 0;
				if( !auth_send(m,(const unsigned char*)hpass,SEED_LENGTH_323 + 1,&pcount) )
					return connect_failed(m);
				continue;
			}
			// Auth switch: the account uses another plugin than the greeting
			// named, and here is a fresh nonce for it. This was taken as a
			// broken packet, so no account on any other plugin could log in.
			{
				const char *named = myp_read_string(p);
				int rest = p->size - p->pos;
				if( strlen(named) >= sizeof(plugin) || !known_plugin(named) ) {
					snprintf(m->last_error,sizeof(m->last_error),"Unsupported authentication plugin '%s'",named);
					m->errcode = 2059;
					return connect_failed(m);
				}
				strcpy(plugin,named);
				memset(nonce,0,sizeof(nonce));
				if( rest > NONCE_SIZE )
					rest = NONCE_SIZE;
				if( rest > 0 )
					myp_read(p,nonce,rest);
			}
			authlen = auth_response(m,plugin,pass,nonce,auth);
			if( authlen < 0 || !auth_send(m,auth,authlen,&pcount) )
				return connect_failed(m);
			continue;
		}
		if( code == 0x01 ) {
			// More data from the plugin.
			if( strcmp(plugin,CACHING_SHA2_PASSWORD) == 0 && p->size == 2 && (unsigned char)p->buf[1] == 3 )
				continue; // fast auth succeeded: the OK follows
			if( strcmp(plugin,CACHING_SHA2_PASSWORD) == 0 && p->size == 2 && (unsigned char)p->buf[1] == 4 ) {
				// Full authentication: the server has no cached hash of this
				// account's password and needs the password itself.
				if( m->tls ) {
					if( !auth_send(m,(const unsigned char*)pass,(int)strlen(pass) + 1,&pcount) )
						return connect_failed(m);
					continue;
				}
				if( m->options.server_public_key ) {
					authlen = rsa_password(m,m->options.server_public_key,pass,nonce,auth);
					if( authlen < 0 || !auth_send(m,auth,authlen,&pcount) )
						return connect_failed(m);
					continue;
				}
				if( m->options.allow_public_key_retrieval ) {
					unsigned char request = 2;
					if( !auth_send(m,&request,1,&pcount) )
						return connect_failed(m);
					continue; // the key comes back as more data
				}
				error(m,"The server needs the password itself (caching_sha2_password full authentication): connect with TLS, or give the server's RSA public key (serverPublicKey), or allowPublicKeyRetrieval",NULL);
				return connect_failed(m);
			}
			if( p->size > 1 && (strcmp(plugin,CACHING_SHA2_PASSWORD) == 0 || strcmp(plugin,SHA256_PASSWORD) == 0) && !m->tls ) {
				// The public key asked for: PEM, which the packet buffer has
				// NUL-terminated.
				const char *pem = p->buf + 1;
				authlen = rsa_password(m,pem,pass,nonce,auth);
				if( authlen < 0 || !auth_send(m,auth,authlen,&pcount) )
					return connect_failed(m);
				continue;
			}
		}
		error(m,"Invalid packet error",NULL);
		return connect_failed(m);
	}

	m->infos.auth_plugin = strdup(plugin);
	// we are connected, setup a longer timeout
	psock_set_timeout(m->s,18000);
	return m;
}

int mysql_select_db( MYSQL *m, const char *dbname ) {
	MYSQL_PACKET *p = &m->packet;
	int pcount = 0;
	myp_begin_packet(p,0);
	myp_write_byte(p,COM_INIT_DB);
	// send dbname without trailing 0x00
	myp_write(p,dbname,strlen(dbname));
	if( !myp_send_packet(m,p,&pcount) ) {
		error(m,"Failed to send packet",NULL);
		return -1;
	}
	return myp_ok(m,0) ? 0 : -1;
}

int mysql_real_query( MYSQL *m, const char *query, int qlength ) {
	MYSQL_PACKET *p = &m->packet;
	int pcount = 0;
	myp_begin_packet(p,0);
	myp_write_byte(p,COM_QUERY);
	myp_write(p,query,qlength);
	m->last_field_count = -1;
	m->affected_rows = -1;
	m->last_insert_id = -1;
	if( !myp_send_packet(m,p,&pcount) ) {
		error(m,"Failed to send packet",NULL);
		return -1;
	}
	if( !myp_ok(m,1) )
		return -1;
	p->id = IS_QUERY;
	return 0;
}

static int do_store( MYSQL *m, MYSQL_RES *r ) {
	int i;
	MYSQL_PACKET *p = &m->packet;
	p->pos = 0;
	r->nfields = myp_read_bin(p);
	if( p->error ) return 0;
	r->fields = (MYSQL_FIELD*)malloc(sizeof(MYSQL_FIELD) * r->nfields);
	memset(r->fields,0,sizeof(MYSQL_FIELD) * r->nfields);
	for(i=0;i<r->nfields;i++) {
		if( !myp_read_packet(m,p) )
			return 0;
		{
			MYSQL_FIELD *f = r->fields + i;
			f->catalog = m->is41 ? myp_read_bin_str(p) : NULL;
			f->db = m->is41 ? myp_read_bin_str(p) : NULL;
			f->table = myp_read_bin_str(p);
			f->org_table = m->is41 ? myp_read_bin_str(p) : NULL;
			f->name = myp_read_bin_str(p);
			f->org_name = m->is41 ? myp_read_bin_str(p) : NULL;
			if( m->is41 ) myp_read_byte(p);
			f->charset = m->is41 ? myp_read_ui16(p) : 0x08;
			f->length = m->is41 ? myp_read_int(p) : myp_read_bin(p);
			f->type = (FIELD_TYPE)(m->is41 ? myp_read_byte(p) : myp_read_bin(p));
			f->flags = m->is41 ? myp_read_ui16(p) : myp_read_bin(p);
			f->decimals = myp_read_byte(p);
			if( m->is41 ) myp_read_byte(p); // should be 0
			if( m->is41 ) myp_read_byte(p); // should be 0
			if( p->error )
				return 0;
		}
	}
	// first EOF packet
	if( !myp_read_packet(m,p) )
		return 0;
	if( myp_read_byte(p) != 0xFE || p->size >= 9 )
		return 0;
	myp_read_eof(m,p);
	// reset packet buffer (to prevent to store large buffer in row data)
	free(p->buf);
	p->buf = NULL;
	p->mem = 0;
	// datas
	while( 1 ) {
		if( !myp_read_packet(m,p) )
			return 0;
		// EOF : end of datas
		if( (unsigned char)p->buf[0] == 0xFE && p->size < 9 ) {
			myp_read_eof(m,p);
			break;
		}
		// ERROR ?
		if( (unsigned char)p->buf[0] == 0xFF ) {
			save_error(m,p);
			return 0;
		}
		// allocate one more row
		if( r->row_count == r->memory_rows ) {
			MYSQL_ROW_DATA *rows;
			r->memory_rows = r->memory_rows ? (r->memory_rows << 1) : 1;
			rows = (MYSQL_ROW_DATA*)malloc(r->memory_rows * sizeof(MYSQL_ROW_DATA));
			memcpy(rows,r->rows,r->row_count * sizeof(MYSQL_ROW_DATA));
			free(r->rows);
			r->rows = rows;
		}
		// read row fields
		{
			MYSQL_ROW_DATA *current = r->rows + r->row_count++;
			int prev = 0;			
			current->raw = p->buf;
			current->lengths = (unsigned long*)malloc(sizeof(unsigned long) * r->nfields);
			current->datas = (char**)malloc(sizeof(char*) * r->nfields);
			for(i=0;i<r->nfields;i++) {
				int l = myp_read_bin(p);
				if( !p->error )
					p->buf[prev] = 0;
				if( l == -1 ) {
					current->lengths[i] = 0;
					current->datas[i] = NULL;
				} else {
					current->lengths[i] = l;
					current->datas[i] = p->buf + p->pos;
					p->pos += l;
				}
				prev = p->pos;
			}
			if( !p->error )
				p->buf[prev] = 0;
		}
		// the packet buffer as been stored, don't reuse it
		p->buf = NULL;
		p->mem = 0;
		if( p->error )
			return 0;
	}
	return 1;
}

MYSQL_RES *mysql_store_result( MYSQL *m ) {
	MYSQL_RES *r;
	MYSQL_PACKET *p = &m->packet;
	if( p->id != IS_QUERY )
		return NULL;
	// OK without result
	if( p->buf[0] == 0 ) {
		p->pos = 0;
		m->last_field_count = myp_read_byte(p); // 0
		myp_read_ok(m,p);
		return NULL;
	}
	r = (MYSQL_RES*)malloc(sizeof(struct _MYSQL_RES));
	memset(r,0,sizeof(struct _MYSQL_RES));
	m->errcode = 0;
	if( !do_store(m,r) ) {
		mysql_free_result(r);
		if( !m->errcode )
			error(m,"Failure while storing result",NULL);
		return NULL;
	}
	m->last_field_count = r->nfields;
	return r;
}

int mysql_field_count( MYSQL *m ) {
	return m->last_field_count;
}

int mysql_affected_rows( MYSQL *m ) {
	return m->affected_rows > 0x7FFFFFFF ? 0x7FFFFFFF : (int)m->affected_rows;
}

int mysql_server_status( MYSQL *m ) {
	return m->infos.server_status;
}

int mysql_escape_string( MYSQL *m, char *sout, const char *sin, int length ) {
	return myp_escape_string(m->infos.server_charset,sout,sin,length);
}

const char *mysql_character_set_name( MYSQL *m ) {
	const char *name = myp_charset_name(m->infos.server_charset);
	if( name == NULL ) {
		static char tmp[512];
		snprintf(tmp,sizeof(tmp),"#%d",m->infos.server_charset);
		return tmp;
	}
	return name;
}

int mysql_real_escape_string( MYSQL *m, char *sout, const char *sin, int length ) {
	if( !myp_supported_charset(m->infos.server_charset) )
		return -1;
	if( m->infos.server_status & SERVER_STATUS_NO_BACKSLASH_ESCAPES )
		return myp_escape_quotes(m->infos.server_charset,sout,sin,length);
	return myp_escape_string(m->infos.server_charset,sout,sin,length);
}

void mysql_close( MYSQL *m ) {
	MYSQL_PACKET *p = &m->packet;
	int pcount = 0;
	if( m->s != INVALID_SOCKET ) {
		myp_begin_packet(p,0);
		myp_write_byte(p,COM_QUIT);
		myp_send_packet(m,p,&pcount);
		myp_tls_close_notify(m);
	}
	myp_close(m);
	myp_tls_free(m);
	free(m->packet.buf);
	free(m->infos.server_version);
	free(m->infos.auth_plugin);
	free(m->options.ssl_ca);
	free(m->options.server_public_key);
	free(m);
}

int mysql_is_tls( MYSQL *m ) {
	return m->tls != NULL;
}

void mysql_set_options( MYSQL *m, int ssl_mode, char *ssl_ca, char *server_public_key, int allow_public_key_retrieval ) {
	m->options.ssl_mode = ssl_mode;
	free(m->options.ssl_ca);
	m->options.ssl_ca = ssl_ca;
	free(m->options.server_public_key);
	m->options.server_public_key = server_public_key;
	m->options.allow_public_key_retrieval = allow_public_key_retrieval;
}

const char *mysql_auth_plugin( MYSQL *m ) {
	return m->infos.auth_plugin ? m->infos.auth_plugin : "";
}

const char *mysql_error( MYSQL *m ) {
	return m->last_error;
}

// RESULTS API

unsigned int mysql_num_rows( MYSQL_RES *r ) {
	return r->row_count;
}

int mysql_num_fields( MYSQL_RES *r ) {
	return r->nfields;
}

MYSQL_FIELD *mysql_fetch_fields( MYSQL_RES *r ) {
	return r->fields;
}

unsigned long *mysql_fetch_lengths( MYSQL_RES *r ) {
	return r->current ? r->current->lengths : NULL;
}

MYSQL_ROW mysql_fetch_row( MYSQL_RES * r ) {
	MYSQL_ROW_DATA *cur = r->current;
	if( cur == NULL )
		cur = r->rows;
	else {
		// free the previous result, since we're done with it
		free(cur->datas);
		free(cur->lengths);
		free(cur->raw);
		cur->datas = NULL;
		cur->lengths = NULL;
		cur->raw = NULL;
		// next
		cur++;
	}
	if( cur >= r->rows + r->row_count ) {		
		free(r->rows);
		r->rows = NULL;
		r->memory_rows = 0;
		cur = NULL;	
	}
	r->current = cur;
	return cur ? cur->datas : NULL;
}

void mysql_free_result( MYSQL_RES *r ) {
	if( r->fields ) {
		int i;
		for(i=0;i<r->nfields;i++) {
			MYSQL_FIELD *f = r->fields + i;
			free(f->catalog);
			free(f->db);
			free(f->table);
			free(f->org_table);
			free(f->name);
			free(f->org_name);
		}
		free(r->fields);
	}
	if( r->rows ) {
		int i;
		for(i=0;i<r->row_count;i++) {
			MYSQL_ROW_DATA *row = r->rows + i;
			free(row->datas);
			free(row->lengths);
			free(row->raw);
		}
		free(r->rows);
	}
	free(r);
}

/* ************************************************************************ */
