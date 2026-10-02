/*
 * hxcpp builds mbedTLS with MBEDTLS_THREADING_C, and on Windows, where
 * mbedTLS has no mutexes of its own, with MBEDTLS_THREADING_ALT: these are
 * those mutexes, Critical Sections. mbedTLS reaches them through function
 * pointers that fail every lock until mbedtls_threading_set_alt() installs
 * them -- and every ctr_drbg and entropy context locks one, as does an RSA
 * key, and PSA, which TLS 1.3 runs on.
 *
 * hxcpp's SSL installed them in _hx_ssl_init(), so a program using sys.ssl
 * had them. Code that is not hxcpp's had them only if something called that
 * first: a static Lime build's curl, compiled against this mbedTLS, failed
 * every HTTPS request with "CTR_DRBG - The entropy source failed" in a
 * program that used nothing of sys.ssl. So they are installed at load,
 * before main, by an initializer that every object built against hxcpp's
 * mbedTLS configuration pulls into an MSVC link (see
 * include/mbedtls_config.h). MinGW has no such directive: there the
 * constructor runs when this object is linked, as it is beside hxcpp's SSL,
 * which still installs them itself.
 */
#include "mbedtls/threading.h"

#if defined(MBEDTLS_THREADING_ALT) && defined(_WIN32)

static void hxcpp_mutex_init(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL)
		return;
	InitializeCriticalSection(&mutex->cs);
	mutex->is_valid = 1;
}

static void hxcpp_mutex_free(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL || !mutex->is_valid)
		return;
	DeleteCriticalSection(&mutex->cs);
	mutex->is_valid = 0;
}

static int hxcpp_mutex_lock(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL || !mutex->is_valid)
		return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	EnterCriticalSection(&mutex->cs);
	return 0;
}

static int hxcpp_mutex_unlock(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL || !mutex->is_valid)
		return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	LeaveCriticalSection(&mutex->cs);
	return 0;
}

/* Once only: installing them again would initialise afresh the mutexes
   mbedTLS keeps for itself -- PSA's key slots and generator -- under whoever
   holds them. */
void hxcpp_mbedtls_threading_init(void)
{
	static volatile LONG installed = 0;
	if (InterlockedCompareExchange(&installed, 1, 0) == 0)
		mbedtls_threading_set_alt(hxcpp_mutex_init, hxcpp_mutex_free, hxcpp_mutex_lock, hxcpp_mutex_unlock);
}

#if defined(_MSC_VER)
static void __cdecl hxcpp_mbedtls_threading_at_load(void)
{
	hxcpp_mbedtls_threading_init();
}

/* An entry in the C runtime's initializer table, run before main. */
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void (__cdecl *hxcpp_mbedtls_threading_initializer)(void) = hxcpp_mbedtls_threading_at_load;
#elif defined(__GNUC__)
__attribute__((constructor)) static void hxcpp_mbedtls_threading_at_load(void)
{
	hxcpp_mbedtls_threading_init();
}
#endif

#else

/* Elsewhere mbedTLS has its own mutexes (MBEDTLS_THREADING_PTHREAD), set up
   statically. */
typedef int hxcpp_mbedtls_threading_unused;

#endif
