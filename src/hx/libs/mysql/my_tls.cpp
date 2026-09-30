/*
 * TLS and the cryptography MySQL 8's authentication needs, for the MySQL
 * client in this directory, on the mbedTLS hxcpp bundles for sys.ssl.
 *
 * - TLS on the connection, begun after the server's greeting with an
 *   SSLRequest packet: MySQL negotiates it inside its own protocol rather
 *   than on a separate port.
 * - SHA-256, for caching_sha2_password's scramble.
 * - RSA with OAEP padding, for sending a password to a server whose account
 *   needs full authentication, over a connection that is not encrypted.
 *
 * Everything here runs inside a GC-free zone when it can block, and touches
 * no collected memory: the MYSQL structure and its options are malloc'd.
 */
#include <hxcpp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "my_proto.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

// In hxcpp's SSL.cpp: sets mbedTLS's mutexes up on Windows, where it is
// built with MBEDTLS_THREADING_ALT. Called once before this file uses
// mbedTLS, in case nothing in the program has opened a TLS socket yet.
void _hx_ssl_init();

struct _MYSQL_TLS {
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt ca;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;
};

static void tls_ready() {
	static bool done = false;
	if( !done ) {
		_hx_ssl_init();
		done = true;
	}
}

static void tls_error( MYSQL *m, const char *what, int code ) {
	char detail[160];
	mbedtls_strerror(code, detail, sizeof(detail));
	snprintf(m->last_error, sizeof(m->last_error), "%s: %s (-0x%04X)", what, detail, (unsigned int)(-code));
	m->errcode = 2026; // CR_SSL_CONNECTION_ERROR, as libmysqlclient reports it
}

static int bio_send( void *ctx, const unsigned char *buf, size_t len ) {
	MYSQL *m = (MYSQL*)ctx;
	int r = psock_send_no_gc(m->s, (const char*)buf, (int)len);
	if( r > 0 )
		return r;
	if( r == PS_BLOCK ) {
		m->timed_out = 1;
		return MBEDTLS_ERR_SSL_TIMEOUT;
	}
	return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int bio_recv( void *ctx, unsigned char *buf, size_t len ) {
	MYSQL *m = (MYSQL*)ctx;
	int r = psock_recv_no_gc(m->s, (char*)buf, (int)len);
	if( r >= 0 )
		return r;
	if( r == PS_BLOCK ) {
		m->timed_out = 1;
		return MBEDTLS_ERR_SSL_TIMEOUT;
	}
	return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

int myp_tls_start( MYSQL *m, const char *host ) {
	int r;
	MYSQL_TLS *t;
	int verify = m->options.ssl_mode >= MYSQL_SSL_VERIFY_CA;

	tls_ready();
	t = (MYSQL_TLS*)malloc(sizeof(MYSQL_TLS));
	memset(t, 0, sizeof(MYSQL_TLS));
	mbedtls_ssl_init(&t->ssl);
	mbedtls_ssl_config_init(&t->conf);
	mbedtls_x509_crt_init(&t->ca);
	mbedtls_entropy_init(&t->entropy);
	mbedtls_ctr_drbg_init(&t->drbg);
	m->tls = t;

	if( (r = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy, (const unsigned char*)"hxcpp-mysql", 11)) != 0 ) {
		tls_error(m, "TLS setup failed", r);
		return 0;
	}
	if( (r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0 ) {
		tls_error(m, "TLS setup failed", r);
		return 0;
	}
	mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);

	if( verify ) {
		if( !m->options.ssl_ca || !*m->options.ssl_ca ) {
			snprintf(m->last_error, sizeof(m->last_error), "TLS verification needs the CA certificates that sign the server's (sslCa)");
			m->errcode = 2026;
			return 0;
		}
		if( (r = mbedtls_x509_crt_parse_file(&t->ca, m->options.ssl_ca)) < 0 ) {
			tls_error(m, "Could not read the CA certificates", r);
			return 0;
		}
		mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
		mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	} else {
		// PREFERRED and REQUIRED encrypt without checking whose certificate it
		// is, as libmysqlclient's modes of those names do: MySQL generates a
		// self-signed one by default, which no chain could verify.
		mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_NONE);
	}

	if( (r = mbedtls_ssl_setup(&t->ssl, &t->conf)) != 0 ) {
		tls_error(m, "TLS setup failed", r);
		return 0;
	}

	// The name is checked against the certificate only for VERIFY_IDENTITY;
	// set it anyway for SNI, unless it is an address, which RFC 6066 keeps
	// out of SNI.
	if( host && *host && (m->options.ssl_mode >= MYSQL_SSL_VERIFY_IDENTITY || strspn(host, "0123456789.:") != strlen(host)) ) {
		if( (r = mbedtls_ssl_set_hostname(&t->ssl, host)) != 0 ) {
			tls_error(m, "TLS setup failed", r);
			return 0;
		}
	}

	mbedtls_ssl_set_bio(&t->ssl, m, bio_send, bio_recv, NULL);

	{
		hx::AutoGCFreeZone blocking;
		do {
			r = mbedtls_ssl_handshake(&t->ssl);
		} while( r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE );
	}

	if( r != 0 ) {
		if( r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ) {
			char why[256];
			mbedtls_x509_crt_verify_info(why, sizeof(why), "", mbedtls_ssl_get_verify_result(&t->ssl));
			snprintf(m->last_error, sizeof(m->last_error), "The server's TLS certificate was refused: %s", why);
			m->errcode = 2026;
		} else
			tls_error(m, "TLS handshake failed", r);
		return 0;
	}

	if( verify && m->options.ssl_mode < MYSQL_SSL_VERIFY_IDENTITY ) {
		// VERIFY_CA: the chain, not the name. mbedTLS checked the name too if
		// one was set, and a mismatch alone is accepted here.
		unsigned int flags = mbedtls_ssl_get_verify_result(&t->ssl);
		if( (flags & ~MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0 ) {
			snprintf(m->last_error, sizeof(m->last_error), "The server's TLS certificate was refused");
			m->errcode = 2026;
			return 0;
		}
	}
	return 1;
}

void myp_tls_free( MYSQL *m ) {
	MYSQL_TLS *t = m->tls;
	if( !t )
		return;
	mbedtls_ssl_free(&t->ssl);
	mbedtls_ssl_config_free(&t->conf);
	mbedtls_x509_crt_free(&t->ca);
	mbedtls_ctr_drbg_free(&t->drbg);
	mbedtls_entropy_free(&t->entropy);
	free(t);
	m->tls = NULL;
}

void myp_tls_close_notify( MYSQL *m ) {
	if( m->tls )
		mbedtls_ssl_close_notify(&m->tls->ssl);
}

int myp_tls_recv( MYSQL *m, void *buf, int size ) {
	int r;
	do {
		r = mbedtls_ssl_read(&m->tls->ssl, (unsigned char*)buf, size);
	} while( r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE );
	if( r > 0 )
		return r;
	if( r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY )
		return 0;
	return -1;
}

int myp_tls_send( MYSQL *m, const void *buf, int size ) {
	int r;
	do {
		r = mbedtls_ssl_write(&m->tls->ssl, (const unsigned char*)buf, size);
	} while( r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE );
	return r > 0 ? r : -1;
}

void myp_sha256( const unsigned char *data, int length, unsigned char out[32] ) {
	mbedtls_sha256_ret(data, (size_t)length, out, 0);
}

int myp_rsa_encrypt( MYSQL *m, const char *pem, const unsigned char *in, int length, unsigned char *out, int capacity ) {
	int r;
	size_t written = 0;
	mbedtls_pk_context key;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;

	tls_ready();
	mbedtls_pk_init(&key);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);

	// A PEM buffer's length counts its terminating NUL.
	r = mbedtls_pk_parse_public_key(&key, (const unsigned char*)pem, strlen(pem) + 1);
	if( r != 0 ) {
		tls_error(m, "Could not read the server's RSA public key", r);
	} else if( !mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA) ) {
		snprintf(m->last_error, sizeof(m->last_error), "The server's public key is not an RSA key");
		m->errcode = 2061;
		r = -1;
	} else if( (r = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)"hxcpp-mysql-rsa", 15)) != 0 ) {
		tls_error(m, "Could not seed the random generator", r);
	} else {
		// RSA_PKCS1_OAEP_PADDING, which the server decrypts with: OAEP over
		// SHA-1, with MGF1 over SHA-1.
		mbedtls_rsa_set_padding(mbedtls_pk_rsa(key), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
		r = mbedtls_pk_encrypt(&key, in, (size_t)length, out, &written, (size_t)capacity, mbedtls_ctr_drbg_random, &drbg);
		if( r != 0 )
			tls_error(m, "Could not encrypt the password for the server", r);
	}

	mbedtls_ctr_drbg_free(&drbg);
	mbedtls_entropy_free(&entropy);
	mbedtls_pk_free(&key);
	return r == 0 ? (int)written : -1;
}
