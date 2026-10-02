#ifdef HX_WINDOWS
#define MBEDTLS_THREADING_ALT
#endif
#ifndef HX_WINDOWS
#define MBEDTLS_THREADING_PTHREAD
#endif

#define MBEDTLS_THREADING_C

#if defined(MBEDTLS_THREADING_ALT) && defined(_MSC_VER)
// Every object built against this configuration -- mbedTLS's own, hxcpp's
// SSL, a native extension, a static Lime's curl -- pulls into the link the
// initializer that installs the Windows mutexes before main
// (../../threading_alt.c), so they are in place for code that never calls
// hxcpp's _hx_ssl_init().
#if defined(_M_IX86)
#pragma comment(linker, "/include:_hxcpp_mbedtls_threading_initializer")
#else
#pragma comment(linker, "/include:hxcpp_mbedtls_threading_initializer")
#endif
#endif
