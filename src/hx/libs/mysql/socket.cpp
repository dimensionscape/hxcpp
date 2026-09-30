/*
 * Copyright (C)2005-2012 Haxe Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <hxcpp.h>
#include "socket.h"
#include <string.h>


#ifdef NEKO_WINDOWS
	static int init_done = 0;
	static WSADATA init_data;
#	include <mstcpip.h>
#	include <ws2tcpip.h>
#else
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <arpa/inet.h>
#	include <unistd.h>
#	include <netdb.h>
#	include <fcntl.h>
#	include <errno.h>
#	include <stdio.h>
#	include <poll.h>
#	define closesocket close
#	define SOCKET_ERROR (-1)
#endif

#if (defined(NEKO_WINDOWS) || defined(NEKO_MAC)) && !defined(MSG_NOSIGNAL)
#	define MSG_NOSIGNAL 0
#endif

static SERR block_error() {
#ifdef NEKO_WINDOWS
	int err = WSAGetLastError();
	// WSAETIMEDOUT is how a blocking socket with SO_RCVTIMEO or SO_SNDTIMEO
	// reports its timeout on Windows, where POSIX says EAGAIN: both are a
	// wait that ran out, not a failure of the connection.
	if( err == WSAEWOULDBLOCK || err == WSAEALREADY || err == WSAETIMEDOUT )
#else
	if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EALREADY )
#endif
		return PS_BLOCK;
	return PS_ERROR;
}

void psock_init() {
#ifdef NEKO_WINDOWS
	if( !init_done ) {
		WSAStartup(MAKEWORD(2,0),&init_data);
		init_done = 1;
	}
#endif
}

PSOCK psock_create() {
   hx::AutoGCFreeZone block;
	PSOCK s = socket(AF_INET,SOCK_STREAM,0);
#	if defined(NEKO_MAC) || defined(NEKO_BSD)
	if( s != INVALID_SOCKET )
		setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,NULL,0);
#	endif
#	ifdef NEKO_POSIX
	// we don't want sockets to be inherited in case of exec
	{
		int old = fcntl(s,F_GETFD,0);
		if( old >= 0 ) fcntl(s,F_SETFD,old|FD_CLOEXEC);
	}
#	endif
	return s;
}

void psock_close( PSOCK s ) {
   hx::AutoGCFreeZone block;
	POSIX_LABEL(close_again);
	if( closesocket(s) ) {
		HANDLE_EINTR(close_again);
	}
}

int psock_send( PSOCK s, const char *buf, int size ) {
   hx::AutoGCFreeZone block;
   return psock_send_no_gc(s,buf,size);
}

int psock_send_no_gc( PSOCK s, const char *buf, int size ) {
	int ret;
	POSIX_LABEL(send_again);
	ret = send(s,buf,size,MSG_NOSIGNAL);
	if( ret == SOCKET_ERROR ) {
		HANDLE_EINTR(send_again);
		return block_error();
	}
	return ret;
}

int psock_recv( PSOCK s, char *buf, int size ) {
   hx::AutoGCFreeZone block;
   return psock_recv_no_gc(s,buf,size);
}

int psock_recv_no_gc( PSOCK s, char *buf, int size ) {
	int ret;
	POSIX_LABEL(recv_again);
	ret = recv(s,buf,size,MSG_NOSIGNAL);
	if( ret == SOCKET_ERROR ) {
		HANDLE_EINTR(recv_again);
		return block_error();
	}
	return ret;
}

PHOST phost_resolve( const char *host ) {
   hx::AutoGCFreeZone block;
	PHOST ip = inet_addr(host);
	if( ip == INADDR_NONE ) {
		struct hostent *h;
#	if defined(NEKO_WINDOWS) || defined(NEKO_MAC) || defined(BLACKBERRY)
		h = gethostbyname(host);
#	else
		struct hostent hbase;
		char buf[1024];
		int errcode;
		gethostbyname_r(host,&hbase,buf,1024,&h,&errcode);
#	endif
		if( h == NULL )
			return UNRESOLVED_HOST;
		ip = *((unsigned int*)h->h_addr);
	}
	return ip;
}

SERR psock_connect( PSOCK s, PHOST host, int port ) {
   hx::AutoGCFreeZone block;
	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	*(int*)&addr.sin_addr.s_addr = host;
	if( connect(s,(struct sockaddr*)&addr,sizeof(addr)) != 0 )
		return block_error();
	return PS_OK;
}

static SERR set_timeout_option( PSOCK s, int option, double t ) {
	// 0, or less, is no limit.
	if( t < 0 )
		t = 0;
#ifdef NEKO_WINDOWS
	DWORD time = (DWORD)(t * 1000);
	if( t > 0 && time == 0 )
		time = 1;
#else
	struct timeval time;
	time.tv_usec = (int)((t - (int)t)*1000000);
	time.tv_sec = (int)t;
#endif
	if( setsockopt(s,SOL_SOCKET,option,(char*)&time,sizeof(time)) != 0 )
		return PS_ERROR;
	return PS_OK;
}

SERR psock_set_timeout( PSOCK s, double t ) {
	if( set_timeout_option(s,SO_SNDTIMEO,t) != PS_OK )
		return PS_ERROR;
	return set_timeout_option(s,SO_RCVTIMEO,t);
}

SERR psock_set_recv_timeout( PSOCK s, double t ) {
	return set_timeout_option(s,SO_RCVTIMEO,t);
}

SERR psock_set_send_timeout( PSOCK s, double t ) {
	return set_timeout_option(s,SO_SNDTIMEO,t);
}

/*
	TCP keepalive, so a connection to a host that has vanished is noticed:
	with none, a query waiting on a partitioned server waits for as long as
	the socket timeout, which was five hours. `idle` seconds without traffic
	before the first probe, `interval` between probes, `count` unanswered
	probes before the connection is dropped; 0 leaves the system's own.
	Windows before 10 (1703) has no count to set, and keeps it at ten.
*/
SERR psock_set_keepalive( PSOCK s, int idle, int interval, int count ) {
	int on = 1;
	if( setsockopt(s,SOL_SOCKET,SO_KEEPALIVE,(char*)&on,sizeof(on)) != 0 )
		return PS_ERROR;
#ifdef NEKO_WINDOWS
	if( idle > 0 || interval > 0 ) {
		struct tcp_keepalive values;
		DWORD returned = 0;
		values.onoff = 1;
		values.keepalivetime = (idle > 0 ? idle : 7200) * 1000;
		values.keepaliveinterval = (interval > 0 ? interval : 1) * 1000;
		if( WSAIoctl(s,SIO_KEEPALIVE_VALS,&values,sizeof(values),NULL,0,&returned,NULL,NULL) != 0 )
			return PS_ERROR;
	}
#	ifdef TCP_KEEPCNT
	// Refused where Windows is too old to know it, which leaves the ten.
	if( count > 0 )
		setsockopt(s,IPPROTO_TCP,TCP_KEEPCNT,(char*)&count,sizeof(count));
#	endif
#else
#	ifdef TCP_KEEPIDLE
	if( idle > 0 )
		setsockopt(s,IPPROTO_TCP,TCP_KEEPIDLE,(char*)&idle,sizeof(idle));
#	elif defined(TCP_KEEPALIVE)
	if( idle > 0 )
		setsockopt(s,IPPROTO_TCP,TCP_KEEPALIVE,(char*)&idle,sizeof(idle));
#	endif
#	ifdef TCP_KEEPINTVL
	if( interval > 0 )
		setsockopt(s,IPPROTO_TCP,TCP_KEEPINTVL,(char*)&interval,sizeof(interval));
#	endif
#	ifdef TCP_KEEPCNT
	if( count > 0 )
		setsockopt(s,IPPROTO_TCP,TCP_KEEPCNT,(char*)&count,sizeof(count));
#	endif
#endif
	return PS_OK;
}

/*
	The keepalive the socket has, read back from it rather than taken from
	what was asked for: `state` gets on (0 or 1), then the idle and interval
	in seconds and the probe count, each -1 where the system does not report
	it.
*/
static int keepalive_option( PSOCK s, int level, int option ) {
	// Zeroed first: Windows writes SO_KEEPALIVE as a single byte.
	int value = 0;
#	ifdef NEKO_WINDOWS
	int size = sizeof(value);
#	else
	socklen_t size = sizeof(value);
#	endif
	if( getsockopt(s,level,option,(char*)&value,&size) != 0 )
		return -1;
	return value;
}

void psock_keepalive_state( PSOCK s, int *state ) {
	int on = keepalive_option(s,SOL_SOCKET,SO_KEEPALIVE);
	state[0] = on < 0 ? -1 : on != 0;
	state[1] = -1;
	state[2] = -1;
	state[3] = -1;
#	if defined(TCP_KEEPIDLE)
	state[1] = keepalive_option(s,IPPROTO_TCP,TCP_KEEPIDLE);
#	elif defined(TCP_KEEPALIVE)
	state[1] = keepalive_option(s,IPPROTO_TCP,TCP_KEEPALIVE);
#	endif
#	ifdef TCP_KEEPINTVL
	state[2] = keepalive_option(s,IPPROTO_TCP,TCP_KEEPINTVL);
#	endif
#	ifdef TCP_KEEPCNT
	state[3] = keepalive_option(s,IPPROTO_TCP,TCP_KEEPCNT);
#	endif
}

int psock_last_error() {
#ifdef NEKO_WINDOWS
	return WSAGetLastError();
#else
	return errno;
#endif
}

/*
	connect() with a limit. A blocking connect to a host that drops the SYN
	waits for the operating system to give up -- 21 seconds on Windows, over
	two minutes on Linux -- and the socket timeouts do not bound it.
*/
SERR psock_connect_timeout( PSOCK s, PHOST host, int port, double timeout ) {
	if( timeout <= 0 )
		return psock_connect(s,host,port);
	hx::AutoGCFreeZone block;
	struct sockaddr_in addr;
	int ready;
	int err = 0;
#	ifdef NEKO_WINDOWS
	int errlen = sizeof(err);
#	else
	socklen_t errlen = sizeof(err);
#	endif
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	*(int*)&addr.sin_addr.s_addr = host;
	if( psock_set_blocking(s,0) != PS_OK )
		return PS_ERROR;
	if( connect(s,(struct sockaddr*)&addr,sizeof(addr)) != 0 ) {
		if( block_error() != PS_BLOCK ) {
			psock_set_blocking(s,1);
			return PS_ERROR;
		}
#	ifdef NEKO_WINDOWS
		{
			fd_set writable, failed;
			struct timeval limit;
			FD_ZERO(&writable);
			FD_ZERO(&failed);
			FD_SET(s,&writable);
			FD_SET(s,&failed);
			limit.tv_sec = (long)timeout;
			limit.tv_usec = (long)((timeout - (long)timeout) * 1000000);
			ready = select(0,NULL,&writable,&failed,&limit);
			if( ready > 0 && FD_ISSET(s,&failed) )
				ready = -1;
		}
#	else
		{
			struct pollfd fds;
			int ms = (int)(timeout * 1000);
			fds.fd = s;
			fds.events = POLLOUT;
			fds.revents = 0;
			POSIX_LABEL(poll_again);
			ready = poll(&fds,1,ms > 0 ? ms : 1);
			if( ready < 0 ) {
				HANDLE_EINTR(poll_again);
			}
		}
#	endif
		if( ready == 0 ) {
			psock_set_blocking(s,1);
			return PS_BLOCK; // timed out
		}
		if( ready < 0 || getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&err,&errlen) != 0 || err != 0 ) {
			psock_set_blocking(s,1);
#	ifdef NEKO_WINDOWS
			WSASetLastError(err ? err : WSAECONNREFUSED);
#	else
			errno = err ? err : ECONNREFUSED;
#	endif
			return PS_ERROR;
		}
	}
	psock_set_blocking(s,1);
	return PS_OK;
}


SERR psock_set_blocking( PSOCK s, int block ) {
#ifdef NEKO_WINDOWS
	{
		unsigned long arg = !block;
		if( ioctlsocket(s,FIONBIO,&arg) != 0 )
			return PS_ERROR;
	}
#else
	{
		int rights = fcntl(s,F_GETFL);
		if( rights == -1 )
			return PS_ERROR;
		if( block )
			rights &= ~O_NONBLOCK;
		else
			rights |= O_NONBLOCK;
		if( fcntl(s,F_SETFL,rights) == -1 )
			return PS_ERROR;
	}
#endif
	return PS_OK;
}

SERR psock_set_fastsend( PSOCK s, int fast ) {
	if( setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(char*)&fast,sizeof(fast)) )
		return block_error();
	return PS_OK;
}

void psock_wait( PSOCK s ) {
   hx::AutoGCFreeZone block;
#	ifdef NEKO_WINDOWS
	fd_set set;
	FD_ZERO(&set);
	FD_SET(s,&set);
	select((int)s+1,&set,NULL,NULL,NULL);
#	else
	struct pollfd fds;
	POSIX_LABEL(poll_again);
	fds.fd = s;
	fds.events = POLLIN;
	fds.revents = 0;
	if( poll(&fds,1,-1) < 0 ) {
		HANDLE_EINTR(poll_again);
	}
#	endif
}

/* ************************************************************************ */
