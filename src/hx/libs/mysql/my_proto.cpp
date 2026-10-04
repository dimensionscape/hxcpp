/* ************************************************************************ */
/*																			*/
/*  MYSQL 5.0 Protocol Implementation 										*/
/*  Copyright (c)2008 Nicolas Cannasse										*/
/*
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <hxcpp.h>
#include <hx/OS.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "my_proto.h"

#define MAX_PACKET_LENGTH 0xFFFFFF

int myp_recv( MYSQL *m, void *buf, int size ) {
   hx::AutoGCFreeZone blocking;
   return myp_recv_no_gc(m,buf,size);
}

/*
	While a connection opens, each read waits only for what is left of the
	connect timeout, not for the whole of it again: a server that sent its
	greeting a byte at a time, each inside the timeout, held mysql_open for as
	long as it went on. False once nothing is left. A read on TLS asks this
	from the record layer, for each read of the socket it makes.
*/
int myp_wait_budget( MYSQL *m ) {
	double left;
	if( m->deadline <= 0 )
		return 1;
	left = m->deadline - psock_clock();
	if( left < 0.000001 )
		return 0;
	psock_set_recv_timeout(m->s,left);
	return 1;
}

int myp_recv_no_gc( MYSQL *m, void *buf, int size ) {
	while( size ) {
		int len;
		if( !m->tls && !myp_wait_budget(m) ) {
			m->timed_out = 1;
			return 0;
		}
		len = m->tls ? myp_tls_recv(m,buf,size) : psock_recv_no_gc(m->s,(char*)buf,size);
		if( len <= 0 ) {
			if( len == PS_BLOCK && !m->tls )
				m->timed_out = 1;
			return size == 0 ? 1 : 0;
		}
		buf = ((char*)buf) + len;
		size -= len;
	}
	return 1;
}


int myp_send( MYSQL *m, void *buf, int size ) {
   hx::AutoGCFreeZone blocking;
   return myp_send_no_gc(m,buf,size);
}

int myp_send_no_gc( MYSQL *m, void *buf, int size ) {
	while( size ) {
		int len = m->tls ? myp_tls_send(m,buf,size) : psock_send_no_gc(m->s,(char*)buf,size);
		if( len <= 0 ) {
			if( len == PS_BLOCK && !m->tls )
				m->timed_out = 1;
			return size == 0 ? 1 : 0;
		}
		buf = ((char*)buf) + len;
		size -= len;
	}
	return 1;
}

int myp_read( MYSQL_PACKET *p, void *buf, int size ) {
	if( p->size - p->pos < size ) {
		p->error = 1;
		return 0;
	}
	memcpy(buf,p->buf + p->pos,size);
	p->pos += size;
	return 1;
}

unsigned char myp_read_byte( MYSQL_PACKET *p ) {
	unsigned char c;
	if( !myp_read(p,&c,1) )
		return 0;
	return c;
}

unsigned short myp_read_ui16( MYSQL_PACKET *p ) {
	unsigned short i;
	if( !myp_read(p,&i,2) )
		return 0;
	return i;
}

int myp_read_int( MYSQL_PACKET *p ) {
	int i;
	if( !myp_read(p,&i,4) )
		return 0;
	return i;
}

int myp_read_bin( MYSQL_PACKET *p ) {
	int c = myp_read_byte(p);
	if( c <= 250 )
		return c;
	if( c == 251 )
		return -1; // NULL
	if( c == 252 )
		return myp_read_ui16(p);
	if( c == 253 ) {
		c = 0;
		myp_read(p,&c,3);
		return c;
	}
	if( c == 254 ) {
		// Eight bytes follow, not four. Reading four left the other four to
		// be taken for whatever came next -- the status flags of an OK
		// packet, or the next column of a row.
		long long v = 0;
		myp_read(p,&v,8);
		if( v < 0 || v > 0x7FFFFFFF ) {
			p->error = 1;
			return 0;
		}
		return (int)v;
	}
	p->error = 1;
	return 0;
}

// A length-encoded integer at its full width, for the counts an OK packet
// carries: affected rows and the insert id are 64-bit.
long long myp_read_bin64( MYSQL_PACKET *p ) {
	int c = myp_read_byte(p);
	if( c <= 250 )
		return c;
	if( c == 251 )
		return -1; // NULL
	if( c == 252 )
		return myp_read_ui16(p);
	if( c == 253 ) {
		int v = 0;
		myp_read(p,&v,3);
		return v;
	}
	if( c == 254 ) {
		long long v = 0;
		myp_read(p,&v,8);
		return v;
	}
	p->error = 1;
	return 0;
}

const char *myp_read_string( MYSQL_PACKET *p ) {
	char *str;
	if( p->pos >= p->size ) {
		p->error = 1;
		return "";
	}
	str = p->buf + p->pos;
	p->pos += strlen(str) + 1;
	return str;
}

char *myp_read_bin_str( MYSQL_PACKET *p ) {
	int size = myp_read_bin(p);
	char *str;
	if( size == -1 )
		return NULL;
	// Against what is left of the packet, not by adding the length to the
	// position: a length near 2^31 wrapped the sum negative, passed, and
	// sized a malloc of size + 1, which wrapped too -- NULL, copied into.
	if( p->error || size < 0 || size > p->size - p->pos ) {
		p->error = 1;
		return NULL;
	}
	str = (char*)malloc(size + 1);
	if( str == NULL ) {
		p->error = 1;
		return NULL;
	}
	memcpy(str,p->buf + p->pos, size);
	str[size] = 0;
	p->pos += size;
	return str;
}

int myp_read_packet( MYSQL *m, MYSQL_PACKET *p ) {
   hx::AutoGCFreeZone blocking;
	unsigned int psize;
	p->pos = 0;
	p->error = 0;
	if( !myp_recv_no_gc(m,&psize,4) ) {
		p->error = 1;
		p->size = 0;
		return 0;
	}
	//p->id = (psize >> 24);
	psize &= 0xFFFFFF;
	p->size = psize;
	// A buffer is made when there is none, whatever the size: the rows of a
	// result keep the buffers they arrive in, and the connection's own was
	// let go before the first, so an empty packet then wrote its end marker
	// through a null pointer.
	if( p->buf == NULL || p->mem < (int)psize ) {
		char *buf = (char*)malloc(psize + 1);
		if( buf == NULL ) {
			p->error = 1;
			p->size = 0;
			return 0;
		}
		free(p->buf);
		p->buf = buf;
		p->mem = psize;
	}
	p->buf[psize] = 0;
	if( psize == 0 || !myp_recv_no_gc(m,p->buf,psize) ) {
		p->error = 1;
		p->size = 0;
		p->buf[0] = 0;
		return 0;
	}
	return 1;
}

int myp_send_packet( MYSQL *m, MYSQL_PACKET *p, int *packet_counter ) {
   hx::AutoGCFreeZone blocking;
	unsigned int header;
	char *buf = p->buf;
	int size = p->size;
	int next = 1;
	while( next ) {
		int psize;
		if( size >= MAX_PACKET_LENGTH )
			psize = MAX_PACKET_LENGTH;
		else {
			psize = size;
			next = 0;
		}
		header = psize | (((*packet_counter)++) << 24);
		if( !myp_send_no_gc(m,&header,4) || !myp_send_no_gc(m,buf,psize) ) {
			p->error = 1;
			return 0;
		}
		buf += psize;
		size -= psize;
	}
	return 1;
}

void myp_begin_packet( MYSQL_PACKET *p, int minsize ) {
	if( p->mem < minsize ) {
		free(p->buf);
		p->buf = (char*)malloc(minsize + 1);
		p->mem = minsize;
	}
	p->error = 0;
	p->size = 0;
}

void myp_write( MYSQL_PACKET *p, const void *data, int size ) {
	if( p->size + size > p->mem ) {
		char *buf2;
		if( p->mem == 0 ) p->mem = 32;
		do {
			p->mem <<= 1;
		} while( p->size + size > p->mem );
		buf2 = (char*)malloc(p->mem + 1);
		memcpy(buf2,p->buf,p->size);
		free(p->buf);
		p->buf = buf2;
	}
	memcpy( p->buf + p->size , data, size );
	p->size += size;
}

void myp_write_byte( MYSQL_PACKET *p, int i ) {
	unsigned char c = (unsigned char)i;
	myp_write(p,&c,1);
}

void myp_write_ui16( MYSQL_PACKET *p, int i ) {
	unsigned short c = (unsigned char)i;
	myp_write(p,&c,2);
}

void myp_write_int( MYSQL_PACKET *p, int i ) {
	myp_write(p,&i,4);
}

void myp_write_string( MYSQL_PACKET *p, const char *str ) {
	myp_write(p,str,strlen(str) + 1);
}

void myp_write_bin( MYSQL_PACKET *p, int size ) {
	if( size <= 250 ) {
		unsigned char l = (unsigned char)size;
		myp_write(p,&l,1);
	} else if( size < 0x10000 ) {
		unsigned char c = 252;
		unsigned short l = (unsigned short)size;
		myp_write(p,&c,1);
		myp_write(p,&l,2);
	} else if( size < 0x1000000 ) {
		unsigned char c = 253;
		unsigned int l = (unsigned short)size;
		myp_write(p,&c,1);
		myp_write(p,&l,3);
	} else {
		unsigned char c = 254;
		myp_write(p,&c,1);
		myp_write(p,&size,4);
	}
}

void myp_crypt( unsigned char *out, const unsigned char *s1, const unsigned char *s2, unsigned int len ) {
	unsigned int i;
	for(i=0;i<len;i++)
		out[i] = s1[i] ^ s2[i];
}

void myp_encrypt_password( const char *pass, const char *seed, SHA1_DIGEST out ) {
	SHA1_CTX ctx;
	SHA1_DIGEST hash_stage1, hash_stage2;
	// stage 1: hash password
	sha1_init(&ctx);
	sha1_update(&ctx,(const unsigned char *)pass,strlen(pass));;
	sha1_final(&ctx,hash_stage1);
	// stage 2: hash stage 1; note that hash_stage2 is stored in the database
	sha1_init(&ctx);
	sha1_update(&ctx, hash_stage1, SHA1_SIZE);
	sha1_final(&ctx, hash_stage2);
	// create crypt string as sha1(message, hash_stage2)
	sha1_init(&ctx);
	sha1_update(&ctx, (const unsigned char *)seed, SHA1_SIZE);
	sha1_update(&ctx, hash_stage2, SHA1_SIZE);
	sha1_final( &ctx, out );
	// xor the result
	myp_crypt(out,out,hash_stage1,SHA1_SIZE);
}

typedef struct {
	unsigned long seed1;
	unsigned long seed2;
	unsigned long max_value;
	double max_value_dbl;
} rand_ctx;

static void random_init( rand_ctx *r, unsigned long seed1, unsigned long seed2 ) {
	r->max_value = 0x3FFFFFFFL;
	r->max_value_dbl = (double)r->max_value;
	r->seed1 = seed1 % r->max_value ;
	r->seed2 = seed2 % r->max_value;
}

static double myp_rnd( rand_ctx *r ) {
	r->seed1 = (r->seed1 * 3 + r->seed2) % r->max_value;
	r->seed2 = (r->seed1 + r->seed2 + 33) % r->max_value;
	return (((double) r->seed1)/r->max_value_dbl);
}

static void hash_password( unsigned long *result, const char *password, int password_len ) {
	unsigned long nr = 1345345333L, add = 7, nr2 = 0x12345671L;
	unsigned long tmp;
	const char *password_end = password + password_len;
	for(; password < password_end; password++) {
		if( *password == ' ' || *password == '\t' )
			continue;
		tmp = (unsigned long)(unsigned char)*password;
		nr ^= (((nr & 63)+add)*tmp)+(nr << 8);
		nr2 += (nr2 << 8) ^ nr;
		add += tmp;
	}
	result[0] = nr & (((unsigned long) 1L << 31) -1L);
	result[1] = nr2 & (((unsigned long) 1L << 31) -1L);
}

void myp_encrypt_pass_323( const char *password, const char seed[SEED_LENGTH_323], char to[SEED_LENGTH_323] ) {
	rand_ctx r;
	unsigned long hash_pass[2], hash_seed[2];
	char extra, *to_start = to;
	const char *seed_end = seed + SEED_LENGTH_323;
	hash_password(hash_pass,password,(unsigned int)strlen(password));
	hash_password(hash_seed,seed,SEED_LENGTH_323);
	random_init(&r,hash_pass[0] ^ hash_seed[0],hash_pass[1] ^ hash_seed[1]);
	while( seed < seed_end ) {
		*to++ = (char)(floor(myp_rnd(&r)*31)+64);
		seed++;
	}
	extra= (char)(floor(myp_rnd(&r)*31));
	while( to_start != to )
		*(to_start++) ^= extra;
}

// defined in mysql/strings/ctype-*.c
//
// The collation the greeting names is the server's default, one byte of it.
// MySQL 8 defaults to utf8mb4_0900_ai_ci, 255, which was missing: against a
// default MySQL 8 server every escape threw "Unsupported charset : #255".
const char *myp_charset_name( int charset ) {
	switch( charset ) {
	case 5:
	case 8:
	case 15:
	case 31:
	case 47:
	case 48:
	case 49:
	case 94:
		return "latin1";
	case 11:
	case 65:
		return "ascii";
	case 63:
		return "binary";
	// 101+ : utf16
	// 160+ : utf32
	case 33:
	case 76:
	case 83:
	case 223:
	case 254:
		return "utf8";
	case 45:
	case 46:
	case 255:
		return "utf8mb4"; // superset of utf8 with up to 4 bytes per-char
	default:
		if( charset >= 192 && charset <= 215 )
			return "utf8";
		if( charset >= 224 && charset <= 247 )
			return "utf8mb4";
	}
	return NULL;
}

int myp_supported_charset( int charset ) {
	return myp_charset_name(charset) != NULL;
}

int myp_escape_string( int charset, char *sout, const char *sin, int length ) {
	// this is safe for UTF8 as well since mysql protects against invalid UTF8 char injection
	const char *send = sin + length;
	char *sbegin = sout;
	while( sin != send ) {
		char c = *sin++;
		switch( c ) {
		case 0:
			*sout++ = '\\';
			*sout++ = '0';
			break;
		case '\n':
			*sout++ = '\\';
			*sout++ = 'n';
			break;
		case '\r':
			*sout++ = '\\';
			*sout++ = 'r';
			break;
		case '\\':
			*sout++ = '\\';
			*sout++ = '\\';
			break;
		case '\'':
			*sout++ = '\\';
			*sout++ = '\'';
			break;
		case '"':
			*sout++ = '\\';
			*sout++ = '"';
			break;
		case '\032':
			*sout++ = '\\';
			*sout++ = 'Z';
			break;
		default:
			*sout++ = c;
		}
	}
	*sout = 0;
	return sout - sbegin;
}

int myp_escape_quotes( int charset, char *sout, const char *sin, int length ) {
	const char *send = sin + length;
	char *sbegin = sout;
	while( sin != send ) {
		char c = *sin++;
		*sout++ = c;
		if( c == '\'' )
			*sout++ = c;
	}
	*sout = 0;
	return sout - sbegin;
}

/* ************************************************************************ */
