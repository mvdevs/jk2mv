// Copyright (c) 2004-2013 Sergey Lyubka <valenok@gmail.com>
// Copyright (c) 2013-2014 Cesanta Software Limited
// All rights reserved
//
// This library is dual-licensed: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation. For the terms of this
// license, see <http://www.gnu.org/licenses/>.
//
// You are free to use this library under the terms of the GNU General
// Public License, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// Alternatively, you can license this library under a commercial
// license, as set out in <http://cesanta.com/>.

#ifdef WIN32
#	pragma warning(disable:4267)
#	pragma warning(disable:4244)
#endif

#ifdef NOEMBED_NET_SKELETON
#include "net_skeleton.h"
#else
// net_skeleton start
// Copyright (c) 2014 Cesanta Software Limited
// All rights reserved
//
// This software is dual-licensed: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation. For the terms of this
// license, see <http://www.gnu.org/licenses/>.
//
// You are free to use this software under the terms of the GNU General
// Public License, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// Alternatively, you can license this software under a commercial
// license, as set out in <http://cesanta.com/>.
//
// $Date: 2014-09-28 05:04:41 UTC $

#ifndef NS_SKELETON_HEADER_INCLUDED
#define NS_SKELETON_HEADER_INCLUDED

#define NS_SKELETON_VERSION "2.1.0"

#undef UNICODE                  // Use ANSI WinAPI functions
#undef _UNICODE                 // Use multibyte encoding on Windows
#define _MBCS                   // Use multibyte encoding on Windows
#define _INTEGRAL_MAX_BITS 64   // Enable _stati64() on Windows
#undef WIN32_LEAN_AND_MEAN      // Let windows.h always include winsock2.h
#define _XOPEN_SOURCE 600       // For flockfile() on Linux
#define __STDC_FORMAT_MACROS    // <inttypes.h> wants this for C++
#define __STDC_LIMIT_MACROS     // C++ wants that for INT64_MAX
#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE       // Enable fseeko() and ftello() functions
#endif
#define _FILE_OFFSET_BITS 64    // Enable 64-bit file offsets

#ifdef _MSC_VER
#pragma warning (disable : 4127)  // FD_SET() emits warning, disable it
#pragma warning (disable : 4204)  // missing c99 support
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")    // Linking with winsock library
#endif
#include <windows.h>
#include <process.h>
#ifndef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef __func__
#define STRX(x) #x
#define STR(x) STRX(x)
#define __func__ __FILE__ ":" STR(__LINE__)
#endif
#ifndef va_copy
#define va_copy(x,y) x = y
#endif // MINGW #defines va_copy
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define sleep(x) Sleep((x) * 1000)
#define to64(x) _atoi64(x)
typedef int socklen_t;
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned __int64 uint64_t;
typedef __int64   int64_t;
typedef SOCKET sock_t;
typedef struct _stati64 ns_stat_t;
#ifndef S_ISDIR
#define S_ISDIR(x) ((x) & _S_IFDIR)
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <arpa/inet.h>  // For inet_pton() when NS_ENABLE_IPV6 is defined
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#define closesocket(x) close(x)
#define __cdecl
#define INVALID_SOCKET (-1)
#define to64(x) strtoll(x, NULL, 10)
typedef int sock_t;
typedef struct stat ns_stat_t;
#endif

#ifdef NS_ENABLE_DEBUG
#define DBG(x) do { printf("%-20s ", __func__); printf x; putchar('\n'); \
  fflush(stdout); } while(0)
#else
#define DBG(x)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
#endif

typedef void *SSL;
typedef void *SSL_CTX;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

union socket_address {
  struct sockaddr sa;
  struct sockaddr_in sin;
  struct sockaddr sin6;
};

// Describes chunk of memory
struct ns_str {
  const char *p;
  size_t len;
};

// IO buffers interface
struct iobuf {
  char *buf;
  size_t len;
  size_t size;
};

void iobuf_init(struct iobuf *, size_t initial_size);
void iobuf_free(struct iobuf *);
size_t iobuf_append(struct iobuf *, const void *data, size_t data_size);
void iobuf_remove(struct iobuf *, size_t data_size);
void iobuf_resize(struct iobuf *, size_t new_size);

// Callback function (event handler) prototype, must be defined by user.
// Net skeleton will call event handler, passing events defined above.
struct ns_connection;
typedef void (*ns_callback_t)(struct ns_connection *, int event_num, void *evp);

// Events. Meaning of event parameter (evp) is given in the comment.
#define NS_POLL    0  // Sent to each connection on each call to ns_mgr_poll()
#define NS_ACCEPT  1  // New connection accept()-ed. union socket_address *addr
#define NS_CONNECT 2  // connect() succeeded or failed. int *success_status
#define NS_RECV    3  // Data has benn received. int *num_bytes
#define NS_SEND    4  // Data has been written to a socket. int *num_bytes
#define NS_CLOSE   5  // Connection is closed. NULL


struct ns_mgr {
  struct ns_connection *active_connections;
  const char *hexdump_file;         // Debug hexdump file path
  sock_t ctl[2];                    // Socketpair for mg_wakeup()
  void *user_data;                  // User data
};


struct ns_connection {
  struct ns_connection *next, *prev;  // ns_mgr::active_connections linkage
  struct ns_connection *listener;     // Set only for accept()-ed connections
  struct ns_mgr *mgr;

  sock_t sock;                // Socket
  union socket_address sa;    // Peer address
  struct iobuf recv_iobuf;    // Received data
  struct iobuf send_iobuf;    // Data scheduled for sending
  SSL *ssl;
  SSL_CTX *ssl_ctx;
  void *user_data;            // User-specific data
  void *proto_data;           // Application protocol-specific data
  time_t last_io_time;        // Timestamp of the last socket IO
  ns_callback_t callback;     // Event handler function

  unsigned int flags;
#define NSF_FINISHED_SENDING_DATA   (1 << 0)
#define NSF_BUFFER_BUT_DONT_SEND    (1 << 1)
#define NSF_SSL_HANDSHAKE_DONE      (1 << 2)
#define NSF_CONNECTING              (1 << 3)
#define NSF_CLOSE_IMMEDIATELY       (1 << 4)
#define NSF_WANT_READ               (1 << 5)
#define NSF_WANT_WRITE              (1 << 6)
#define NSF_LISTENING               (1 << 7)
#define NSF_UDP                     (1 << 8)

#define NSF_USER_1                  (1 << 20)
#define NSF_USER_2                  (1 << 21)
#define NSF_USER_3                  (1 << 22)
#define NSF_USER_4                  (1 << 23)
#define NSF_USER_5                  (1 << 24)
#define NSF_USER_6                  (1 << 25)
};

void ns_mgr_init(struct ns_mgr *, void *user_data);
void ns_mgr_free(struct ns_mgr *);
time_t ns_mgr_poll(struct ns_mgr *, int milli);
void ns_broadcast(struct ns_mgr *, ns_callback_t, void *, size_t);

struct ns_connection *ns_next(struct ns_mgr *, struct ns_connection *);
struct ns_connection *ns_add_sock(struct ns_mgr *, sock_t,
                                  ns_callback_t, void *);
struct ns_connection *ns_bind(struct ns_mgr *, const char *,
                              ns_callback_t, void *);
struct ns_connection *ns_connect(struct ns_mgr *, const char *,
                                 ns_callback_t, void *);

int ns_send(struct ns_connection *, const void *buf, int len);
int ns_printf(struct ns_connection *, const char *fmt, ...);
int ns_vprintf(struct ns_connection *, const char *fmt, va_list ap);

// Utility functions
void *ns_start_thread(void *(*f)(void *), void *p);
int ns_socketpair(sock_t [2]);
int ns_socketpair2(sock_t [2], int sock_type);  // SOCK_STREAM or SOCK_DGRAM
void ns_set_close_on_exec(sock_t);
void ns_sock_to_str(sock_t sock, char *buf, size_t len, int flags);
int ns_hexdump(const void *buf, int len, char *dst, int dst_len);
int ns_avprintf(char **buf, size_t size, const char *fmt, va_list ap);
int ns_resolve(const char *domain_name, char *ip_addr_buf, size_t buf_len);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // NS_SKELETON_HEADER_INCLUDED
// Copyright (c) 2014 Cesanta Software Limited
// All rights reserved
//
// This software is dual-licensed: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation. For the terms of this
// license, see <http://www.gnu.org/licenses/>.
//
// You are free to use this software under the terms of the GNU General
// Public License, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// Alternatively, you can license this software under a commercial
// license, as set out in <http://cesanta.com/>.
//
// $Date: 2014-09-28 05:04:41 UTC $


#ifndef NS_MALLOC
#define NS_MALLOC malloc
#endif

#ifndef NS_REALLOC
#define NS_REALLOC realloc
#endif

#ifndef NS_FREE
#define NS_FREE free
#endif

#ifndef NS_CALLOC
#define NS_CALLOC calloc
#endif

#define NS_CTL_MSG_MESSAGE_SIZE     (8 * 1024)
#define NS_READ_BUFFER_SIZE         2048
#define NS_UDP_RECEIVE_BUFFER_SIZE  2000
#define NS_VPRINTF_BUFFER_SIZE      500

struct ctl_msg {
  ns_callback_t callback;
  char message[NS_CTL_MSG_MESSAGE_SIZE];
};

void iobuf_resize(struct iobuf *io, size_t new_size) {
  char *p;
  if ((new_size > io->size || (new_size < io->size && new_size >= io->len)) &&
      (p = (char *) NS_REALLOC(io->buf, new_size)) != NULL) {
    io->size = new_size;
    io->buf = p;
  }
}

void iobuf_init(struct iobuf *iobuf, size_t initial_size) {
  iobuf->len = iobuf->size = 0;
  iobuf->buf = NULL;
  iobuf_resize(iobuf, initial_size);
}

void iobuf_free(struct iobuf *iobuf) {
  if (iobuf != NULL) {
    if (iobuf->buf != NULL) NS_FREE(iobuf->buf);
    iobuf_init(iobuf, 0);
  }
}

size_t iobuf_append(struct iobuf *io, const void *buf, size_t len) {
  char *p = NULL;

  assert(io != NULL);
  assert(io->len <= io->size);

  if (len <= 0) {
  } else if (io->len + len <= io->size) {
    memcpy(io->buf + io->len, buf, len);
    io->len += len;
  } else if ((p = (char *) NS_REALLOC(io->buf, io->len + len)) != NULL) {
    io->buf = p;
    memcpy(io->buf + io->len, buf, len);
    io->len += len;
    io->size = io->len;
  } else {
    len = 0;
  }

  return len;
}

void iobuf_remove(struct iobuf *io, size_t n) {
  if (n > 0 && n <= io->len) {
    memmove(io->buf, io->buf + n, io->len - n);
    io->len -= n;
  }
}

static size_t ns_out(struct ns_connection *nc, const void *buf, size_t len) {
  if (nc->flags & NSF_UDP) {
    long n = sendto(nc->sock, buf, len, 0, &nc->sa.sa, sizeof(nc->sa.sin));
    DBG(("%p %d send %ld (%d %s)", nc, nc->sock, n, errno, strerror(errno)));
    return n < 0 ? 0 : n;
  } else {
    return iobuf_append(&nc->send_iobuf, buf, len);
  }
}

#ifndef NS_DISABLE_THREADS
void *ns_start_thread(void *(*f)(void *), void *p) {
#ifdef _WIN32
  return (void *) _beginthread((void (__cdecl *)(void *)) f, 0, p);
#else
  pthread_t thread_id = (pthread_t) 0;
  pthread_attr_t attr;

  (void) pthread_attr_init(&attr);
  (void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

#if defined(NS_STACK_SIZE) && NS_STACK_SIZE > 1
  (void) pthread_attr_setstacksize(&attr, NS_STACK_SIZE);
#endif

  pthread_create(&thread_id, &attr, f, p);
  pthread_attr_destroy(&attr);

  return (void *) thread_id;
#endif
}
#endif  // NS_DISABLE_THREADS

static void ns_add_conn(struct ns_mgr *mgr, struct ns_connection *c) {
  c->next = mgr->active_connections;
  mgr->active_connections = c;
  c->prev = NULL;
  if (c->next != NULL) c->next->prev = c;
}

static void ns_remove_conn(struct ns_connection *conn) {
  if (conn->prev == NULL) conn->mgr->active_connections = conn->next;
  if (conn->prev) conn->prev->next = conn->next;
  if (conn->next) conn->next->prev = conn->prev;
}

// Print message to buffer. If buffer is large enough to hold the message,
// return buffer. If buffer is to small, allocate large enough buffer on heap,
// and return allocated buffer.
int ns_avprintf(char **buf, size_t size, const char *fmt, va_list ap) {
  va_list ap_copy;
  int len;

  va_copy(ap_copy, ap);
  len = vsnprintf(*buf, size, fmt, ap_copy);
  va_end(ap_copy);

  if (len < 0) {
    // eCos and Windows are not standard-compliant and return -1 when
    // the buffer is too small. Keep allocating larger buffers until we
    // succeed or out of memory.
    *buf = NULL;
    while (len < 0) {
      if (*buf) NS_FREE(*buf);
      size *= 2;
      if ((*buf = (char *) NS_MALLOC(size)) == NULL) break;
      va_copy(ap_copy, ap);
      len = vsnprintf(*buf, size, fmt, ap_copy);
      va_end(ap_copy);
    }
  } else if (len > (int) size) {
    // Standard-compliant code path. Allocate a buffer that is large enough.
    if ((*buf = (char *) NS_MALLOC(len + 1)) == NULL) {
      len = -1;
    } else {
      va_copy(ap_copy, ap);
      len = vsnprintf(*buf, len + 1, fmt, ap_copy);
      va_end(ap_copy);
    }
  }

  return len;
}

int ns_vprintf(struct ns_connection *nc, const char *fmt, va_list ap) {
  char mem[NS_VPRINTF_BUFFER_SIZE], *buf = mem;
  int len;

  if ((len = ns_avprintf(&buf, sizeof(mem), fmt, ap)) > 0) {
    ns_out(nc, buf, len);
  }
  if (buf != mem && buf != NULL) {
    NS_FREE(buf);
  }

  return len;
}

int ns_printf(struct ns_connection *conn, const char *fmt, ...) {
  int len;
  va_list ap;
  va_start(ap, fmt);
  len = ns_vprintf(conn, fmt, ap);
  va_end(ap);
  return len;
}

static void hexdump(struct ns_connection *nc, const char *path,
                    int num_bytes, int ev) {
  const struct iobuf *io = ev == NS_SEND ? &nc->send_iobuf : &nc->recv_iobuf;
  FILE *fp;
  char *buf, src[60], dst[60];
  int buf_size = num_bytes * 5 + 100;

  if ((fp = fopen(path, "a")) != NULL) {
    ns_sock_to_str(nc->sock, src, sizeof(src), 3);
    ns_sock_to_str(nc->sock, dst, sizeof(dst), 7);
    fprintf(fp, "%lu %p %s %s %s %d\n", (unsigned long) time(NULL),
            nc->user_data, src,
            ev == NS_RECV ? "<-" : ev == NS_SEND ? "->" :
            ev == NS_ACCEPT ? "<A" : ev == NS_CONNECT ? "C>" : "XX",
            dst, num_bytes);
    if (num_bytes > 0 && (buf = (char *) NS_MALLOC(buf_size)) != NULL) {
      ns_hexdump(io->buf + (ev == NS_SEND ? 0 : io->len) -
        (ev == NS_SEND ? 0 : num_bytes), num_bytes, buf, buf_size);
      fprintf(fp, "%s", buf);
      NS_FREE(buf);
    }
    fclose(fp);
  }
}

static void ns_call(struct ns_connection *nc, int ev, void *p) {
  if (nc->mgr->hexdump_file != NULL && ev != NS_POLL) {
    int len = (ev == NS_RECV || ev == NS_SEND) ? * (int *) p : 0;
    hexdump(nc, nc->mgr->hexdump_file, len, ev);
  }

  nc->callback(nc, ev, p);
}

static void ns_destroy_conn(struct ns_connection *conn) {
  closesocket(conn->sock);
  iobuf_free(&conn->recv_iobuf);
  iobuf_free(&conn->send_iobuf);
  NS_FREE(conn);
}

static void ns_close_conn(struct ns_connection *conn) {
  DBG(("%p %d", conn, conn->flags));
  ns_call(conn, NS_CLOSE, NULL);
  ns_remove_conn(conn);
  ns_destroy_conn(conn);
}

void ns_set_close_on_exec(sock_t sock) {
#ifdef _WIN32
  (void) SetHandleInformation((HANDLE) sock, HANDLE_FLAG_INHERIT, 0);
#else
  fcntl(sock, F_SETFD, FD_CLOEXEC);
#endif
}

static void ns_set_non_blocking_mode(sock_t sock) {
#ifdef _WIN32
  unsigned long on = 1;
  ioctlsocket(sock, FIONBIO, &on);
#else
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

// TODO(lsm): use non-blocking resolver
static int ns_resolve2(const char *host, struct in_addr *ina) {
  struct hostent *he;
  if ((he = gethostbyname(host)) == NULL) {
    DBG(("gethostbyname(%s) failed: %s", host, strerror(errno)));
  } else {
    memcpy(ina, he->h_addr_list[0], sizeof(*ina));
    return 1;
  }
  return 0;
}

// Resolve FDQN "host", store IP address in the "ip".
// Return > 0 (IP address length) on success.
int ns_resolve(const char *host, char *buf, size_t n) {
  struct in_addr ad;
  return ns_resolve2(host, &ad) ? snprintf(buf, n, "%s", inet_ntoa(ad)) : 0;
}

// Address format: [PROTO://][IP_ADDRESS:]PORT[:CERT][:CA_CERT]
static int ns_parse_address(const char *str, union socket_address *sa,
                            int *proto, int *use_ssl, char *cert, char *ca) {
  unsigned int a, b, c, d, port;
  int n = 0, len = 0;
  char host[200];

  // MacOS needs that. If we do not zero it, subsequent bind() will fail.
  // Also, all-zeroes in the socket address means binding to all addresses
  // for both IPv4 and IPv6 (INADDR_ANY and IN6ADDR_ANY_INIT).
  memset(sa, 0, sizeof(*sa));
  sa->sin.sin_family = AF_INET;

  *proto = SOCK_STREAM;
  *use_ssl = 0;
  cert[0] = ca[0] = '\0';

  if (memcmp(str, "ssl://", 6) == 0) {
    str += 6;
    *use_ssl = 1;
  } else if (memcmp(str, "udp://", 6) == 0) {
    str += 6;
    *proto = SOCK_DGRAM;
  } else if (memcmp(str, "tcp://", 6) == 0) {
    str += 6;
  }

  if (sscanf(str, "%u.%u.%u.%u:%u%n", &a, &b, &c, &d, &port, &len) == 5) {
    // Bind to a specific IPv4 address, e.g. 192.168.1.5:8080
    sa->sin.sin_addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
    sa->sin.sin_port = htons((uint16_t) port);
  } else if (sscanf(str, "%199[^ :]:%u%n", host, &port, &len) == 2) {
    sa->sin.sin_port = htons((uint16_t) port);
    ns_resolve2(host, &sa->sin.sin_addr);
  } else if (sscanf(str, "%u%n", &port, &len) == 1) {
    // If only port is specified, bind to IPv4, INADDR_ANY
    sa->sin.sin_port = htons((uint16_t) port);
  }

  if (*use_ssl && (sscanf(str + len, ":%99[^:]:%99[^:]%n", cert, ca, &n) == 2 ||
                   sscanf(str + len, ":%99[^:]%n", cert, &n) == 1)) {
    len += n;
  }

  return port < 0xffff && str[len] == '\0' ? len : 0;
}

// 'sa' must be an initialized address to bind to
static sock_t ns_open_listening_socket(union socket_address *sa, int proto) {
  socklen_t sa_len = (sa->sa.sa_family == AF_INET) ?
    sizeof(sa->sin) : sizeof(sa->sin6);
  sock_t sock = INVALID_SOCKET;
#ifndef _WIN32
  int on = 1;
#endif

  if ((sock = socket(sa->sa.sa_family, proto, 0)) != INVALID_SOCKET &&
#ifndef _WIN32
      // SO_RESUSEADDR is not enabled on Windows because the semantics of
      // SO_REUSEADDR on UNIX and Windows is different. On Windows,
      // SO_REUSEADDR allows to bind a socket to a port without error even if
      // the port is already open by another program. This is not the behavior
      // SO_REUSEADDR was designed for, and leads to hard-to-track failure
      // scenarios. Therefore, SO_REUSEADDR was disabled on Windows.
      !setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *) &on, sizeof(on)) &&
#endif
      !bind(sock, &sa->sa, sa_len) &&
      (proto == SOCK_DGRAM || listen(sock, SOMAXCONN) == 0)) {
    ns_set_non_blocking_mode(sock);
    // In case port was set to 0, get the real port number
    (void) getsockname(sock, &sa->sa, &sa_len);
  } else if (sock != INVALID_SOCKET) {
    closesocket(sock);
    sock = INVALID_SOCKET;
  }

  return sock;
}

struct ns_connection *ns_bind(struct ns_mgr *srv, const char *str,
                              ns_callback_t callback, void *user_data) {
  union socket_address sa;
  struct ns_connection *nc = NULL;
  int use_ssl, proto;
  char cert[100], ca_cert[100];
  sock_t sock;

  ns_parse_address(str, &sa, &proto, &use_ssl, cert, ca_cert);
  if (use_ssl && cert[0] == '\0') return NULL;

  if ((sock = ns_open_listening_socket(&sa, proto)) == INVALID_SOCKET) {
  } else if ((nc = ns_add_sock(srv, sock, callback, NULL)) == NULL) {
    closesocket(sock);
  } else {
    nc->sa = sa;
    nc->flags |= NSF_LISTENING;
    nc->user_data = user_data;
    nc->callback = callback;

    if (proto == SOCK_DGRAM) {
      nc->flags |= NSF_UDP;
    }

    DBG(("%p sock %d/%d ssl %p %p", nc, sock, proto, nc->ssl_ctx, nc->ssl));
  }

  return nc;
}

static struct ns_connection *accept_conn(struct ns_connection *ls) {
  struct ns_connection *c = NULL;
  union socket_address sa;
  socklen_t len = sizeof(sa);
  sock_t sock = INVALID_SOCKET;

  // NOTE(lsm): on Windows, sock is always > FD_SETSIZE
  if ((sock = accept(ls->sock, &sa.sa, &len)) == INVALID_SOCKET) {
  } else if ((c = ns_add_sock(ls->mgr, sock, ls->callback,
              ls->user_data)) == NULL) {
    closesocket(sock);
  } else {
    c->listener = ls;
    c->proto_data = ls->proto_data;
    ns_call(c, NS_ACCEPT, &sa);
    DBG(("%p %d %p %p", c, c->sock, c->ssl_ctx, c->ssl));
  }

  return c;
}

static int ns_is_error(int n) {
  return n == 0 ||
    (n < 0 && errno != EINTR && errno != EINPROGRESS &&
     errno != EAGAIN && errno != EWOULDBLOCK
#ifdef _WIN32
     && WSAGetLastError() != WSAEINTR && WSAGetLastError() != WSAEWOULDBLOCK
#endif
    );
}

void ns_sock_to_str(sock_t sock, char *buf, size_t len, int flags) {
  union socket_address sa;
  socklen_t slen = sizeof(sa);

  if (buf != NULL && len > 0) {
    buf[0] = '\0';
    memset(&sa, 0, sizeof(sa));
    if (flags & 4) {
      getpeername(sock, &sa.sa, &slen);
    } else {
      getsockname(sock, &sa.sa, &slen);
    }
    if (flags & 1) {
#if   defined(_WIN32)
      // Only Windoze Vista (and newer) have inet_ntop()
      strncpy(buf, inet_ntoa(sa.sin.sin_addr), len);
#else
      inet_ntop(sa.sa.sa_family, (void *) &sa.sin.sin_addr, buf,(socklen_t)len);
#endif
    }
    if (flags & 2) {
      snprintf(buf + strlen(buf), len - (strlen(buf) + 1), "%s%d",
               flags & 1 ? ":" : "", (int) ntohs(sa.sin.sin_port));
    }
  }
}

int ns_hexdump(const void *buf, int len, char *dst, int dst_len) {
  const unsigned char *p = (const unsigned char *) buf;
  char ascii[17] = "";
  int i, idx, n = 0;

  for (i = 0; i < len; i++) {
    idx = i % 16;
    if (idx == 0) {
      if (i > 0) n += snprintf(dst + n, dst_len - n, "  %s\n", ascii);
      n += snprintf(dst + n, dst_len - n, "%04x ", i);
    }
    n += snprintf(dst + n, dst_len - n, " %02x", p[i]);
    ascii[idx] = p[i] < 0x20 || p[i] > 0x7e ? '.' : p[i];
    ascii[idx + 1] = '\0';
  }

  while (i++ % 16) n += snprintf(dst + n, dst_len - n, "%s", "   ");
  n += snprintf(dst + n, dst_len - n, "  %s\n\n", ascii);

  return n;
}

static void ns_read_from_socket(struct ns_connection *conn) {
  char buf[NS_READ_BUFFER_SIZE];
  int n = 0;

  if (conn->flags & NSF_CONNECTING) {
    int ok = 1, ret;
    socklen_t len = sizeof(ok);

    ret = getsockopt(conn->sock, SOL_SOCKET, SO_ERROR, (char *) &ok, &len);
    (void) ret;
    conn->flags &= ~NSF_CONNECTING;
    DBG(("%p ok=%d", conn, ok));
    if (ok != 0) {
      conn->flags |= NSF_CLOSE_IMMEDIATELY;
    }
    ns_call(conn, NS_CONNECT, &ok);
    return;
  }

  {
    while ((n = (int) recv(conn->sock, buf, sizeof(buf), 0)) > 0) {
      DBG(("%p %d <- %d bytes (PLAIN)", conn, conn->flags, n));
      iobuf_append(&conn->recv_iobuf, buf, n);
      ns_call(conn, NS_RECV, &n);
    }
  }

  if (ns_is_error(n)) {
    conn->flags |= NSF_CLOSE_IMMEDIATELY;
  }
}

static void ns_write_to_socket(struct ns_connection *conn) {
  struct iobuf *io = &conn->send_iobuf;
  int n = 0;

  { n = (int) send(conn->sock, io->buf, io->len, 0); }

  DBG(("%p %d -> %d bytes", conn, conn->flags, n));

  ns_call(conn, NS_SEND, &n);
  if (ns_is_error(n)) {
    conn->flags |= NSF_CLOSE_IMMEDIATELY;
  } else if (n > 0) {
    iobuf_remove(io, n);
  }
}

int ns_send(struct ns_connection *conn, const void *buf, int len) {
  return (int) ns_out(conn, buf, len);
}

static void ns_handle_udp(struct ns_connection *ls) {
  struct ns_connection nc;
  char buf[NS_UDP_RECEIVE_BUFFER_SIZE];
  int n;
  socklen_t s_len = sizeof(nc.sa);

  memset(&nc, 0, sizeof(nc));
  n = recvfrom(ls->sock, buf, sizeof(buf), 0, &nc.sa.sa, &s_len);
  if (n <= 0) {
    DBG(("%p recvfrom: %s", ls, strerror(errno)));
  } else {
    nc.mgr = ls->mgr;
    nc.recv_iobuf.buf = buf;
    nc.recv_iobuf.len = nc.recv_iobuf.size = n;
    nc.sock = ls->sock;
    nc.callback = ls->callback;
    nc.user_data = ls->user_data;
    nc.proto_data = ls->proto_data;
    nc.mgr = ls->mgr;
    nc.listener = ls;
    nc.flags = NSF_UDP;
    DBG(("%p %d bytes received", ls, n));
    ns_call(&nc, NS_RECV, &n);
  }
}

static void ns_add_to_set(sock_t sock, fd_set *set, sock_t *max_fd) {
  if (sock != INVALID_SOCKET) {
    FD_SET(sock, set);
    if (*max_fd == INVALID_SOCKET || sock > *max_fd) {
      *max_fd = sock;
    }
  }
}

time_t ns_mgr_poll(struct ns_mgr *mgr, int milli) {
  struct ns_connection *conn, *tmp_conn;
  struct timeval tv;
  fd_set read_set, write_set;
  sock_t max_fd = INVALID_SOCKET;
  time_t current_time = time(NULL);

  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  ns_add_to_set(mgr->ctl[1], &read_set, &max_fd);

  for (conn = mgr->active_connections; conn != NULL; conn = tmp_conn) {
    tmp_conn = conn->next;
    if (!(conn->flags & (NSF_LISTENING | NSF_CONNECTING))) {
      ns_call(conn, NS_POLL, &current_time);
    }
    if (conn->flags & NSF_CLOSE_IMMEDIATELY) {
      ns_close_conn(conn);
    } else {
      if (!(conn->flags & NSF_WANT_WRITE)) {
        //DBG(("%p read_set", conn));
        ns_add_to_set(conn->sock, &read_set, &max_fd);
      }
      if (((conn->flags & NSF_CONNECTING) && !(conn->flags & NSF_WANT_READ)) ||
          (conn->send_iobuf.len > 0 && !(conn->flags & NSF_CONNECTING) &&
           !(conn->flags & NSF_BUFFER_BUT_DONT_SEND))) {
        //DBG(("%p write_set", conn));
        ns_add_to_set(conn->sock, &write_set, &max_fd);
      }
    }
  }

  tv.tv_sec = milli / 1000;
  tv.tv_usec = (milli % 1000) * 1000;

  if (select((int) max_fd + 1, &read_set, &write_set, NULL, &tv) > 0) {
    // select() might have been waiting for a long time, reset current_time
    // now to prevent last_io_time being set to the past.
    current_time = time(NULL);

    // Read wakeup messages
    if (mgr->ctl[1] != INVALID_SOCKET &&
        FD_ISSET(mgr->ctl[1], &read_set)) {
      struct ctl_msg ctl_msg;
      int len = (int) recv(mgr->ctl[1], (char *) &ctl_msg, sizeof(ctl_msg), 0);
      send(mgr->ctl[1], ctl_msg.message, 1, 0);
      if (len >= (int) sizeof(ctl_msg.callback) && ctl_msg.callback != NULL) {
        struct ns_connection *c;
        for (c = ns_next(mgr, NULL); c != NULL; c = ns_next(mgr, c)) {
          ctl_msg.callback(c, NS_POLL, ctl_msg.message);
        }
      }
    }

    for (conn = mgr->active_connections; conn != NULL; conn = tmp_conn) {
      tmp_conn = conn->next;
      if (FD_ISSET(conn->sock, &read_set)) {
        if (conn->flags & NSF_LISTENING) {
          if (conn->flags & NSF_UDP) {
            ns_handle_udp(conn);
          } else {
            // We're not looping here, and accepting just one connection at
            // a time. The reason is that eCos does not respect non-blocking
            // flag on a listening socket and hangs in a loop.
            accept_conn(conn);
          }
        } else {
          conn->last_io_time = current_time;
          ns_read_from_socket(conn);
        }
      }

      if (FD_ISSET(conn->sock, &write_set)) {
        if (conn->flags & NSF_CONNECTING) {
          ns_read_from_socket(conn);
        } else if (!(conn->flags & NSF_BUFFER_BUT_DONT_SEND)) {
          conn->last_io_time = current_time;
          ns_write_to_socket(conn);
        }
      }
    }
  }

  for (conn = mgr->active_connections; conn != NULL; conn = tmp_conn) {
    tmp_conn = conn->next;
    if ((conn->flags & NSF_CLOSE_IMMEDIATELY) ||
        (conn->send_iobuf.len == 0 &&
          (conn->flags & NSF_FINISHED_SENDING_DATA))) {
      ns_close_conn(conn);
    }
  }

  return current_time;
}

struct ns_connection *ns_connect(struct ns_mgr *mgr, const char *address,
                                 ns_callback_t callback, void *user_data) {
  sock_t sock = INVALID_SOCKET;
  struct ns_connection *nc = NULL;
  union socket_address sa;
  char cert[100], ca_cert[100];
  int rc, use_ssl, proto;

  ns_parse_address(address, &sa, &proto, &use_ssl, cert, ca_cert);
  if ((sock = socket(AF_INET, proto, 0)) == INVALID_SOCKET) {
    return NULL;
  }
  ns_set_non_blocking_mode(sock);
  rc = (proto == SOCK_DGRAM) ? 0 : connect(sock, &sa.sa, sizeof(sa.sin));

  if (rc != 0 && ns_is_error(rc)) {
    closesocket(sock);
    return NULL;
  } else if ((nc = ns_add_sock(mgr, sock, callback, user_data)) == NULL) {
    closesocket(sock);
    return NULL;
  }

  nc->sa = sa;   // Important, cause UDP conns will use sendto()
  nc->flags = (proto == SOCK_DGRAM) ? NSF_UDP : NSF_CONNECTING;

  return nc;
}

struct ns_connection *ns_add_sock(struct ns_mgr *s, sock_t sock,
                                  ns_callback_t callback, void *user_data) {
  struct ns_connection *conn;
  if ((conn = (struct ns_connection *) NS_MALLOC(sizeof(*conn))) != NULL) {
    memset(conn, 0, sizeof(*conn));
    ns_set_non_blocking_mode(sock);
    ns_set_close_on_exec(sock);
    conn->sock = sock;
    conn->user_data = user_data;
    conn->callback = callback;
    conn->mgr = s;
    conn->last_io_time = time(NULL);
    ns_add_conn(s, conn);
    DBG(("%p %d", conn, sock));
  }
  return conn;
}

struct ns_connection *ns_next(struct ns_mgr *s, struct ns_connection *conn) {
  return conn == NULL ? s->active_connections : conn->next;
}

void ns_broadcast(struct ns_mgr *mgr, ns_callback_t cb,void *data, size_t len) {
  struct ctl_msg ctl_msg;
  if (mgr->ctl[0] != INVALID_SOCKET && data != NULL &&
      len < sizeof(ctl_msg.message)) {
    ctl_msg.callback = cb;
    memcpy(ctl_msg.message, data, len);
    send(mgr->ctl[0], (char *) &ctl_msg,
         offsetof(struct ctl_msg, message) + len, 0);
    recv(mgr->ctl[0], (char *) &len, 1, 0);
  }
}

void ns_mgr_init(struct ns_mgr *s, void *user_data) {
  memset(s, 0, sizeof(*s));
  s->ctl[0] = s->ctl[1] = INVALID_SOCKET;
  s->user_data = user_data;

#ifdef _WIN32
  { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); }
#else
  // Ignore SIGPIPE signal, so if client cancels the request, it
  // won't kill the whole process.
  signal(SIGPIPE, SIG_IGN);
#endif

}

void ns_mgr_free(struct ns_mgr *s) {
  struct ns_connection *conn, *tmp_conn;

  DBG(("%p", s));
  if (s == NULL) return;
  // Do one last poll, see https://github.com/cesanta/mongoose/issues/286
  ns_mgr_poll(s, 0);

  if (s->ctl[0] != INVALID_SOCKET) closesocket(s->ctl[0]);
  if (s->ctl[1] != INVALID_SOCKET) closesocket(s->ctl[1]);
  s->ctl[0] = s->ctl[1] = INVALID_SOCKET;

  for (conn = s->active_connections; conn != NULL; conn = tmp_conn) {
    tmp_conn = conn->next;
    ns_close_conn(conn);
  }
}
// net_skeleton end
#endif  // NOEMBED_NET_SKELETON

#include <ctype.h>

#ifdef _WIN32         //////////////// Windows specific defines and includes
#include <io.h>       // For _lseeki64
#include <direct.h>   // For _mkdir
#ifndef S_ISDIR
#define S_ISDIR(x) ((x) & _S_IFDIR)
#endif
#ifdef stat
#undef stat
#endif
#ifdef lseek
#undef lseek
#endif
#ifdef popen
#undef popen
#endif
#ifdef pclose
#undef pclose
#endif
#define stat(x, y) mg_stat((x), (y))
#define fopen(x, y) mg_fopen((x), (y))
#define open(x, y, z) mg_open((x), (y), (z))
#define close(x) _close(x)
#define fileno(x) _fileno(x)
#define lseek(x, y, z) _lseeki64((x), (y), (z))
#define read(x, y, z) _read((x), (y), (z))
#define write(x, y, z) _write((x), (y), (z))
#define popen(x, y) _popen((x), (y))
#define pclose(x) _pclose(x)
#define mkdir(x, y) _mkdir(x)
#define rmdir(x) _rmdir(x)
#define strdup(x) _strdup(x)
#ifndef __func__
#define STRX(x) #x
#define STR(x) STRX(x)
#define __func__ __FILE__ ":" STR(__LINE__)
#endif
/* MINGW has adopted the MSVC formatting for 64-bit ints as of gcc 4.4 till 4.8*/
#if (defined(__MINGW32__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4 && __GNUC_MINOR__ < 8))) || defined(_MSC_VER)
#define INT64_FMT   "I64d"
#else
#define INT64_FMT   "lld"
#endif
#define flockfile(x)      ((void) (x))
#define funlockfile(x)    ((void) (x))
typedef struct _stati64 file_stat_t;
typedef HANDLE process_id_t;

#else                    ////////////// UNIX specific defines and includes

#if !defined(MONGOOSE_NO_FILESYSTEM) &&\
    (!defined(MONGOOSE_NO_DAV) || !defined(MONGOOSE_NO_DIRECTORY_LISTING))
#include <dirent.h>
#endif
#if !defined(MONGOOSE_NO_FILESYSTEM) && !defined(MONGOOSE_NO_DL)
#include <dlfcn.h>
#endif
#include <inttypes.h>
#include <pwd.h>
#if !defined(O_BINARY)
#define O_BINARY 0
#endif
#define INT64_FMT PRId64
typedef struct stat file_stat_t;
typedef pid_t process_id_t;
#endif                  //////// End of platform-specific defines and includes

#include "meerkat.h"

#define MAX_REQUEST_SIZE 16384
#define IOBUF_SIZE 8192
#define MAX_PATH_SIZE 8192
#define DEFAULT_CGI_PATTERN "**.cgi$|**.pl$|**.php$"
#define CGI_ENVIRONMENT_SIZE 8192
#define MAX_CGI_ENVIR_VARS 64
#define ENV_EXPORT_TO_CGI "MONGOOSE_CGI"
#define PASSWORDS_FILE_NAME ".htpasswd"

#ifndef MONGOOSE_USE_WEBSOCKET_PING_INTERVAL
#define MONGOOSE_USE_WEBSOCKET_PING_INTERVAL 5
#endif

// Extra HTTP headers to send in every static file reply
#if !defined(MONGOOSE_USE_EXTRA_HTTP_HEADERS)
#define MONGOOSE_USE_EXTRA_HTTP_HEADERS ""
#endif

#ifndef MONGOOSE_POST_SIZE_LIMIT
#define MONGOOSE_POST_SIZE_LIMIT 0
#endif

#ifndef MONGOOSE_IDLE_TIMEOUT_SECONDS
#define MONGOOSE_IDLE_TIMEOUT_SECONDS 300
#endif

#ifdef MONGOOSE_NO_FILESYSTEM
#define MONGOOSE_NO_AUTH
#define MONGOOSE_NO_DAV
#define MONGOOSE_NO_DIRECTORY_LISTING
#define MONGOOSE_NO_LOGGING
#define MONGOOSE_NO_SSI
#define MONGOOSE_NO_DL
#endif

struct vec {
  const char *ptr;
  int len;
};

// For directory listing and WevDAV support
struct dir_entry {
  struct connection *conn;
  char *file_name;
  file_stat_t st;
};

// NOTE(lsm): this enum shoulds be in sync with the config_options.
enum {
  ACCESS_CONTROL_LIST,
#ifndef MONGOOSE_NO_FILESYSTEM
  ACCESS_LOG_FILE,
#ifndef MONGOOSE_NO_AUTH
  AUTH_DOMAIN,
#endif
  DAV_AUTH_FILE,
  DOCUMENT_ROOT,
#ifndef MONGOOSE_NO_DIRECTORY_LISTING
  ENABLE_DIRECTORY_LISTING,
#endif
#endif
  ENABLE_PROXY,
  EXTRA_MIME_TYPES,
#if !defined(MONGOOSE_NO_FILESYSTEM) && !defined(MONGOOSE_NO_AUTH)
  GLOBAL_AUTH_FILE,
#endif
#ifndef MONGOOSE_NO_FILESYSTEM
  HIDE_FILES_PATTERN,
  HEXDUMP_FILE,
  INDEX_FILES,
#endif
  LISTENING_PORT,
#ifndef _WIN32
  RUN_AS_USER,
#endif
  URL_REWRITES,
  NUM_OPTIONS
};

static const char *static_config_options[] = {
  "access_control_list", NULL,
#ifndef MONGOOSE_NO_FILESYSTEM
  "access_log_file", NULL,
#ifndef MONGOOSE_NO_AUTH
  "auth_domain", "mydomain.com",
#endif
  "dav_auth_file", NULL,
  "document_root",  NULL,
#ifndef MONGOOSE_NO_DIRECTORY_LISTING
  "enable_directory_listing", "yes",
#endif
#endif
  "enable_proxy", NULL,
  "extra_mime_types", NULL,
#if !defined(MONGOOSE_NO_FILESYSTEM) && !defined(MONGOOSE_NO_AUTH)
  "global_auth_file", NULL,
#endif
#ifndef MONGOOSE_NO_FILESYSTEM
  "hide_files_patterns", NULL,
  "hexdump_file", NULL,
  "index_files","index.html,index.htm,index.shtml,index.cgi,index.php",
#endif
  "listening_port", NULL,
#ifndef _WIN32
  "run_as_user", NULL,
#endif
  "url_rewrites", NULL,
  NULL
};

struct mg_server {
  struct ns_mgr ns_mgr;
  union socket_address lsa;   // Listening socket address
  mg_handler_t event_handler;
  char *config_options[NUM_OPTIONS];
};

// Local endpoint representation
union endpoint {
  int fd;                     // Opened regular local file
  struct ns_connection *nc;   // CGI or proxy->target connection
};

enum endpoint_type {
 EP_NONE, EP_FILE, EP_CGI, EP_USER, EP_PUT, EP_CLIENT, EP_PROXY
};

#define MG_HEADERS_SENT NSF_USER_1
#define MG_LONG_RUNNING NSF_USER_2
#define MG_CGI_CONN NSF_USER_3
#define MG_PROXY_CONN NSF_USER_4
#define MG_PROXY_DONT_PARSE NSF_USER_5

struct connection {
  struct ns_connection *ns_conn;  // NOTE(lsm): main.c depends on this order
  struct mg_connection mg_conn;
  struct mg_server *server;
  union endpoint endpoint;
  enum endpoint_type endpoint_type;
  char *path_info;
  char *request;
  int64_t num_bytes_recv; // Total number of bytes received
  int64_t cl;             // Reply content length, for Range support
  int request_len;  // Request length, including last \r\n after last header
};

#define MG_CONN_2_CONN(c) ((struct connection *) ((char *) (c) - \
  offsetof(struct connection, mg_conn)))

static void open_local_endpoint(struct connection *conn, int skip_user);
static void close_local_endpoint(struct connection *conn);
static void mg_ev_handler(struct ns_connection *nc, int ev, void *p);

static const struct {
  const char *extension;
  size_t ext_len;
  const char *mime_type;
} static_builtin_mime_types[] = {
  {".html", 5, "text/html"},
  {".htm", 4, "text/html"},
  {".shtm", 5, "text/html"},
  {".shtml", 6, "text/html"},
  {".css", 4, "text/css"},
  {".js",  3, "application/x-javascript"},
  {".ico", 4, "image/x-icon"},
  {".gif", 4, "image/gif"},
  {".jpg", 4, "image/jpeg"},
  {".jpeg", 5, "image/jpeg"},
  {".png", 4, "image/png"},
  {".svg", 4, "image/svg+xml"},
  {".txt", 4, "text/plain"},
  {".torrent", 8, "application/x-bittorrent"},
  {".wav", 4, "audio/x-wav"},
  {".mp3", 4, "audio/x-mp3"},
  {".mid", 4, "audio/mid"},
  {".m3u", 4, "audio/x-mpegurl"},
  {".ogg", 4, "application/ogg"},
  {".ram", 4, "audio/x-pn-realaudio"},
  {".xml", 4, "text/xml"},
  {".json",  5, "application/json"},
  {".xslt", 5, "application/xml"},
  {".xsl", 4, "application/xml"},
  {".ra",  3, "audio/x-pn-realaudio"},
  {".doc", 4, "application/msword"},
  {".exe", 4, "application/octet-stream"},
  {".zip", 4, "application/x-zip-compressed"},
  {".xls", 4, "application/excel"},
  {".tgz", 4, "application/x-tar-gz"},
  {".tar", 4, "application/x-tar"},
  {".gz",  3, "application/x-gunzip"},
  {".arj", 4, "application/x-arj-compressed"},
  {".rar", 4, "application/x-rar-compressed"},
  {".rtf", 4, "application/rtf"},
  {".pdf", 4, "application/pdf"},
  {".swf", 4, "application/x-shockwave-flash"},
  {".mpg", 4, "video/mpeg"},
  {".webm", 5, "video/webm"},
  {".mpeg", 5, "video/mpeg"},
  {".mov", 4, "video/quicktime"},
  {".mp4", 4, "video/mp4"},
  {".m4v", 4, "video/x-m4v"},
  {".asf", 4, "video/x-ms-asf"},
  {".avi", 4, "video/x-msvideo"},
  {".bmp", 4, "image/bmp"},
  {".ttf", 4, "application/x-font-ttf"},
  {NULL,  0, NULL}
};

#ifndef MONGOOSE_NO_THREADS
void *mg_start_thread(void *(*f)(void *), void *p) {
  return ns_start_thread(f, p);
}
#endif  // MONGOOSE_NO_THREADS

#ifndef MONGOOSE_NO_MMAP
#ifdef _WIN32
static void *mmap(void *addr, int64_t len, int prot, int flags, int fd,
                  int offset) {
  HANDLE fh = (HANDLE) _get_osfhandle(fd);
  HANDLE mh = CreateFileMapping(fh, 0, PAGE_READONLY, 0, 0, 0);
  void *p = MapViewOfFile(mh, FILE_MAP_READ, 0, 0, (size_t) len);
  CloseHandle(mh);
  return p;
}
#define munmap(x, y)  UnmapViewOfFile(x)
#define MAP_FAILED NULL
#define MAP_PRIVATE 0
#define PROT_READ 0
#else
#include <sys/mman.h>
#endif

void *mg_mmap(FILE *fp, size_t size) {
  void *p = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fileno(fp), 0);
  return p == MAP_FAILED ? NULL : p;
}

void mg_munmap(void *p, size_t size) {
  munmap(p, size);
}
#endif  // MONGOOSE_NO_MMAP

#if defined(_WIN32) && !defined(MONGOOSE_NO_FILESYSTEM)
// Encode 'path' which is assumed UTF-8 string, into UNICODE string.
// wbuf and wbuf_len is a target buffer and its length.
static void to_wchar(const char *path, wchar_t *wbuf, size_t wbuf_len) {
  char buf[MAX_PATH_SIZE * 2], buf2[MAX_PATH_SIZE * 2], *p;

  strncpy(buf, path, sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  // Trim trailing slashes. Leave backslash for paths like "X:\"
  p = buf + strlen(buf) - 1;
  while (p > buf && p[-1] != ':' && (p[0] == '\\' || p[0] == '/')) *p-- = '\0';

  // Convert to Unicode and back. If doubly-converted string does not
  // match the original, something is fishy, reject.
  memset(wbuf, 0, wbuf_len * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, (int) wbuf_len);
  WideCharToMultiByte(CP_UTF8, 0, wbuf, (int) wbuf_len, buf2, sizeof(buf2),
                      NULL, NULL);
  if (strcmp(buf, buf2) != 0) {
    wbuf[0] = L'\0';
  }
}

static int mg_stat(const char *path, file_stat_t *st) {
  wchar_t wpath[MAX_PATH_SIZE];
  to_wchar(path, wpath, ARRAY_SIZE(wpath));
  DBG(("[%ls] -> %d", wpath, _wstati64(wpath, st)));
  return _wstati64(wpath, st);
}

static FILE *mg_fopen(const char *path, const char *mode) {
  wchar_t wpath[MAX_PATH_SIZE], wmode[10];
  to_wchar(path, wpath, ARRAY_SIZE(wpath));
  to_wchar(mode, wmode, ARRAY_SIZE(wmode));
  return _wfopen(wpath, wmode);
}

static int mg_open(const char *path, int flag, int mode) {
  wchar_t wpath[MAX_PATH_SIZE];
  to_wchar(path, wpath, ARRAY_SIZE(wpath));
  return _wopen(wpath, flag, mode);
}
#endif // _WIN32 && !MONGOOSE_NO_FILESYSTEM

// A helper function for traversing a comma separated list of values.
// It returns a list pointer shifted to the next value, or NULL if the end
// of the list found.
// Value is stored in val vector. If value has form "x=y", then eq_val
// vector is initialized to point to the "y" part, and val vector length
// is adjusted to point only to "x".
static const char *next_option(const char *list, struct vec *val,
                               struct vec *eq_val) {
  if (list == NULL || *list == '\0') {
    // End of the list
    list = NULL;
  } else {
    val->ptr = list;
    if ((list = strchr(val->ptr, ',')) != NULL) {
      // Comma found. Store length and shift the list ptr
      val->len = list - val->ptr;
      list++;
    } else {
      // This value is the last one
      list = val->ptr + strlen(val->ptr);
      val->len = list - val->ptr;
    }

    if (eq_val != NULL) {
      // Value has form "x=y", adjust pointers and lengths
      // so that val points to "x", and eq_val points to "y".
      eq_val->len = 0;
      eq_val->ptr = (const char *) memchr(val->ptr, '=', val->len);
      if (eq_val->ptr != NULL) {
        eq_val->ptr++;  // Skip over '=' character
        eq_val->len = val->ptr + val->len - eq_val->ptr;
        val->len = (eq_val->ptr - val->ptr) - 1;
      }
    }
  }

  return list;
}

// Like snprintf(), but never returns negative value, or a value
// that is larger than a supplied buffer.
static int mg_vsnprintf(char *buf, size_t buflen, const char *fmt, va_list ap) {
  int n;
  if (buflen < 1) return 0;
  n = vsnprintf(buf, buflen, fmt, ap);
  if (n < 0) {
    n = 0;
  } else if (n >= (int) buflen) {
    n = (int) buflen - 1;
  }
  buf[n] = '\0';
  return n;
}

static int mg_snprintf(char *buf, size_t buflen, const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = mg_vsnprintf(buf, buflen, fmt, ap);
  va_end(ap);
  return n;
}

// Check whether full request is buffered. Return:
//   -1  if request is malformed
//    0  if request is not yet fully buffered
//   >0  actual request length, including last \r\n\r\n
static int get_request_len(const char *s, int buf_len) {
  const unsigned char *buf = (unsigned char *) s;
  int i;

  for (i = 0; i < buf_len; i++) {
    // Control characters are not allowed but >=128 are.
    // Abort scan as soon as one malformed character is found.
    if (!isprint(buf[i]) && buf[i] != '\r' && buf[i] != '\n' && buf[i] < 128) {
      return -1;
    } else if (buf[i] == '\n' && i + 1 < buf_len && buf[i + 1] == '\n') {
      return i + 2;
    } else if (buf[i] == '\n' && i + 2 < buf_len && buf[i + 1] == '\r' &&
               buf[i + 2] == '\n') {
      return i + 3;
    }
  }

  return 0;
}

// Skip the characters until one of the delimiters characters found.
// 0-terminate resulting word. Skip the rest of the delimiters if any.
// Advance pointer to buffer to the next word. Return found 0-terminated word.
static char *skip(char **buf, const char *delimiters) {
  char *p, *begin_word, *end_word, *end_delimiters;

  begin_word = *buf;
  end_word = begin_word + strcspn(begin_word, delimiters);
  end_delimiters = end_word + strspn(end_word, delimiters);

  for (p = end_word; p < end_delimiters; p++) {
    *p = '\0';
  }

  *buf = end_delimiters;

  return begin_word;
}

// Parse HTTP headers from the given buffer, advance buffer to the point
// where parsing stopped.
static void parse_http_headers(char **buf, struct mg_connection *ri) {
  size_t i;

  for (i = 0; i < ARRAY_SIZE(ri->http_headers); i++) {
    ri->http_headers[i].name = skip(buf, ": ");
    ri->http_headers[i].value = skip(buf, "\r\n");
    if (ri->http_headers[i].name[0] == '\0')
      break;
    ri->num_headers = i + 1;
  }
}

static const char *status_code_to_str(int status_code) {
  switch (status_code) {

    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";

    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 208: return "Already Reported";
    case 226: return "IM Used";

    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 306: return "Switch Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";

    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I\'m a teapot";
    case 422: return "Unprocessable Entity";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";

    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";

    default:  return "Server Error";
  }
}

static int call_user(struct connection *conn, enum mg_event ev) {
  return conn != NULL && conn->server != NULL &&
    conn->server->event_handler != NULL ?
    conn->server->event_handler(&conn->mg_conn, ev) : MG_FALSE;
}

static void send_http_error(struct connection *conn, int code,
                            const char *fmt, ...) {
  const char *message = status_code_to_str(code);
  const char *rewrites = conn->server->config_options[URL_REWRITES];
  char headers[200], body[200];
  struct vec a, b;
  va_list ap;
  int body_len, headers_len, match_code;

  conn->mg_conn.status_code = code;

  // Invoke error handler if it is set
  if (call_user(conn, MG_HTTP_ERROR) == MG_TRUE) {
    close_local_endpoint(conn);
    return;
  }

  // Handle error code rewrites
  while ((rewrites = next_option(rewrites, &a, &b)) != NULL) {
    if ((match_code = atoi(a.ptr)) > 0 && match_code == code) {
      struct mg_connection *c = &conn->mg_conn;
      c->status_code = 302;
      mg_printf(c, "HTTP/1.1 %d Moved\r\n"
                "Location: %.*s?code=%d&orig_uri=%s&query_string=%s\r\n\r\n",
                c->status_code, b.len, b.ptr, code, c->uri,
                c->query_string == NULL ? "" : c->query_string);
      close_local_endpoint(conn);
      return;
    }
  }

  body_len = mg_snprintf(body, sizeof(body), "%d %s\n", code, message);
  if (fmt != NULL) {
    va_start(ap, fmt);
    body_len += mg_vsnprintf(body + body_len, sizeof(body) - body_len, fmt, ap);
    va_end(ap);
  }
  if ((code >= 300 && code <= 399) || code == 204) {
    // 3xx errors do not have body
    body_len = 0;
  }
  headers_len = mg_snprintf(headers, sizeof(headers),
                            "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n"
                            "Content-Type: text/plain\r\n\r\n",
                            code, message, body_len);
  ns_send(conn->ns_conn, headers, headers_len);
  ns_send(conn->ns_conn, body, body_len);
  close_local_endpoint(conn);  // This will write to the log file
}

static void write_chunk(struct connection *conn, const char *buf, int len) {
  char chunk_size[50];
  int n = mg_snprintf(chunk_size, sizeof(chunk_size), "%X\r\n", len);
  ns_send(conn->ns_conn, chunk_size, n);
  ns_send(conn->ns_conn, buf, len);
  ns_send(conn->ns_conn, "\r\n", 2);
}

size_t mg_printf(struct mg_connection *conn, const char *fmt, ...) {
  struct connection *c = MG_CONN_2_CONN(conn);
  va_list ap;

  va_start(ap, fmt);
  ns_vprintf(c->ns_conn, fmt, ap);
  va_end(ap);

  return c->ns_conn->send_iobuf.len;
}

static void ns_forward(struct ns_connection *from, struct ns_connection *to) {
  DBG(("%p -> %p %lu bytes", from, to, (unsigned long)from->recv_iobuf.len));
  ns_send(to, from->recv_iobuf.buf, from->recv_iobuf.len);
  iobuf_remove(&from->recv_iobuf, from->recv_iobuf.len);
}

static char *mg_strdup(const char *str) {
  char *copy = (char *) NS_MALLOC(strlen(str) + 1);
  if (copy != NULL) {
    strcpy(copy, str);
  }
  return copy;
}

static int isbyte(int n) {
  return n >= 0 && n <= 255;
}

static int parse_net(const char *spec, uint32_t *net, uint32_t *mask) {
  int n, a, b, c, d, slash = 32, len = 0;

  if ((sscanf(spec, "%d.%d.%d.%d/%d%n", &a, &b, &c, &d, &slash, &n) == 5 ||
      sscanf(spec, "%d.%d.%d.%d%n", &a, &b, &c, &d, &n) == 4) &&
      isbyte(a) && isbyte(b) && isbyte(c) && isbyte(d) &&
      slash >= 0 && slash < 33) {
    len = n;
    *net = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
    *mask = slash ? 0xffffffffU << (32 - slash) : 0;
  }

  return len;
}

// Verify given socket address against the ACL.
// Return -1 if ACL is malformed, 0 if address is disallowed, 1 if allowed.
static int check_acl(const char *acl, uint32_t remote_ip) {
  int allowed, flag;
  uint32_t net, mask;
  struct vec vec;

  // If any ACL is set, deny by default
  allowed = acl == NULL ? '+' : '-';

  while ((acl = next_option(acl, &vec, NULL)) != NULL) {
    flag = vec.ptr[0];
    if ((flag != '+' && flag != '-') ||
        parse_net(&vec.ptr[1], &net, &mask) == 0) {
      return -1;
    }

    if (net == (remote_ip & mask)) {
      allowed = flag;
    }
  }

  return allowed == '+';
}

// Protect against directory disclosure attack by removing '..',
// excessive '/' and '\' characters
static void remove_double_dots_and_double_slashes(char *s) {
  char *p = s;

  while (*s != '\0') {
    *p++ = *s++;
    if (s[-1] == '/' || s[-1] == '\\') {
      // Skip all following slashes, backslashes and double-dots
      while (s[0] != '\0') {
        if (s[0] == '/' || s[0] == '\\') { s++; }
        else if (s[0] == '.' && s[1] == '.') { s += 2; }
        else { break; }
      }
    }
  }
  *p = '\0';
}

int mg_url_decode(const char *src, int src_len, char *dst,
                  int dst_len, int is_form_url_encoded) {
  int i, j, a, b;
#define HEXTOI(x) (isdigit(x) ? x - '0' : x - 'W')

  for (i = j = 0; i < src_len && j < dst_len - 1; i++, j++) {
    if (src[i] == '%' && i < src_len - 2 &&
        isxdigit(* (const unsigned char *) (src + i + 1)) &&
        isxdigit(* (const unsigned char *) (src + i + 2))) {
      a = tolower(* (const unsigned char *) (src + i + 1));
      b = tolower(* (const unsigned char *) (src + i + 2));
      dst[j] = (char) ((HEXTOI(a) << 4) | HEXTOI(b));
      i += 2;
    } else if (is_form_url_encoded && src[i] == '+') {
      dst[j] = ' ';
    } else {
      dst[j] = src[i];
    }
  }

  dst[j] = '\0'; // Null-terminate the destination

  return i >= src_len ? j : -1;
}

static int is_valid_http_method(const char *s) {
  return !strcmp(s, "GET") || !strcmp(s, "POST") || !strcmp(s, "HEAD") ||
    !strcmp(s, "CONNECT") || !strcmp(s, "PUT") || !strcmp(s, "DELETE") ||
    !strcmp(s, "OPTIONS") || !strcmp(s, "PROPFIND") || !strcmp(s, "MKCOL");
}

// Parse HTTP request, fill in mg_request structure.
// This function modifies the buffer by NUL-terminating
// HTTP request components, header names and header values.
// Note that len must point to the last \n of HTTP headers.
static int parse_http_message(char *buf, int len, struct mg_connection *ri) {
  int is_request, n;

  // Reset the connection. Make sure that we don't touch fields that are
  // set elsewhere: remote_ip, remote_port, server_param
  ri->request_method = ri->uri = ri->http_version = ri->query_string = NULL;
  ri->num_headers = ri->status_code = ri->is_websocket = ri->content_len = 0;

  buf[len - 1] = '\0';

  // RFC says that all initial whitespaces should be ingored
  while (*buf != '\0' && isspace(* (unsigned char *) buf)) {
    buf++;
  }
  ri->request_method = skip(&buf, " ");
  ri->uri = skip(&buf, " ");
  ri->http_version = skip(&buf, "\r\n");

  // HTTP message could be either HTTP request or HTTP response, e.g.
  // "GET / HTTP/1.0 ...." or  "HTTP/1.0 200 OK ..."
  is_request = is_valid_http_method(ri->request_method);
  if ((is_request && memcmp(ri->http_version, "HTTP/", 5) != 0) ||
      (!is_request && memcmp(ri->request_method, "HTTP/", 5) != 0)) {
    len = -1;
  } else {
    if (is_request) {
      ri->http_version += 5;
    } else {
      ri->status_code = atoi(ri->uri);
    }
    parse_http_headers(&buf, ri);

    if ((ri->query_string = strchr(ri->uri, '?')) != NULL) {
      *(char *) ri->query_string++ = '\0';
    }
    n = (int) strlen(ri->uri);
    mg_url_decode(ri->uri, n, (char *) ri->uri, n + 1, 0);
    if (*ri->uri == '/' || *ri->uri == '.') {
      remove_double_dots_and_double_slashes((char *) ri->uri);
    }
  }

  return len;
}

static int lowercase(const char *s) {
  return tolower(* (const unsigned char *) s);
}

static int mg_strcasecmp(const char *s1, const char *s2) {
  int diff;

  do {
    diff = lowercase(s1++) - lowercase(s2++);
  } while (diff == 0 && s1[-1] != '\0');

  return diff;
}

static int mg_strncasecmp(const char *s1, const char *s2, size_t len) {
  int diff = 0;

  if (len > 0)
    do {
      diff = lowercase(s1++) - lowercase(s2++);
    } while (diff == 0 && s1[-1] != '\0' && --len > 0);

  return diff;
}

// Return HTTP header value, or NULL if not found.
const char *mg_get_header(const struct mg_connection *ri, const char *s) {
  int i;

  for (i = 0; i < ri->num_headers; i++)
    if (!mg_strcasecmp(s, ri->http_headers[i].name))
      return ri->http_headers[i].value;

  return NULL;
}

// Perform case-insensitive match of string against pattern
int mg_match_prefix(const char *pattern, int pattern_len, const char *str) {
  const char *or_str;
  int len, res, i = 0, j = 0;

  if ((or_str = (const char *) memchr(pattern, '|', pattern_len)) != NULL) {
    res = mg_match_prefix(pattern, or_str - pattern, str);
    return res > 0 ? res : mg_match_prefix(or_str + 1,
      (pattern + pattern_len) - (or_str + 1), str);
  }

  for (; i < pattern_len; i++, j++) {
    if (pattern[i] == '?' && str[j] != '\0') {
      continue;
    } else if (pattern[i] == '$') {
      return str[j] == '\0' ? j : -1;
    } else if (pattern[i] == '*') {
      i++;
      if (pattern[i] == '*') {
        i++;
        len = (int) strlen(str + j);
      } else {
        len = (int) strcspn(str + j, "/");
      }
      if (i == pattern_len) {
        return j + len;
      }
      do {
        res = mg_match_prefix(pattern + i, pattern_len - i, str + j + len);
      } while (res == -1 && len-- > 0);
      return res == -1 ? -1 : j + res + len;
    } else if (lowercase(&pattern[i]) != lowercase(&str[j])) {
      return -1;
    }
  }
  return j;
}

// This function prints HTML pages, and expands "{{something}}" blocks
// inside HTML by calling appropriate callback functions.
// Note that {{@path/to/file}} construct outputs embedded file's contents,
// which provides SSI-like functionality.
void mg_template(struct mg_connection *conn, const char *s,
                 struct mg_expansion *expansions) {
  int i, j, pos = 0, inside_marker = 0;

  for (i = 0; s[i] != '\0'; i++) {
    if (inside_marker == 0 && !memcmp(&s[i], "{{", 2)) {
      if (i > pos) {
        mg_send_data(conn, &s[pos], i - pos);
      }
      pos = i;
      inside_marker = 1;
    }
    if (inside_marker == 1 && !memcmp(&s[i], "}}", 2)) {
      for (j = 0; expansions[j].keyword != NULL; j++) {
        const char *kw = expansions[j].keyword;
        if ((int) strlen(kw) == i - (pos + 2) &&
            memcmp(kw, &s[pos + 2], i - (pos + 2)) == 0) {
          expansions[j].handler(conn);
          pos = i + 2;
          break;
        }
      }
      inside_marker = 0;
    }
  }
  if (i > pos) {
    mg_send_data(conn, &s[pos], i - pos);
  }
}

#ifndef MONGOOSE_NO_FILESYSTEM
static int must_hide_file(struct connection *conn, const char *path) {
  const char *pw_pattern = "**" PASSWORDS_FILE_NAME "$";
  const char *pattern = conn->server->config_options[HIDE_FILES_PATTERN];
  return mg_match_prefix(pw_pattern, strlen(pw_pattern), path) > 0 ||
    (pattern != NULL && mg_match_prefix(pattern, strlen(pattern), path) > 0);
}

// Return 1 if real file has been found, 0 otherwise
static int convert_uri_to_file_name(struct connection *conn, char *buf,
                                    size_t buf_len, file_stat_t *st) {
  struct vec a, b;
  const char *rewrites = conn->server->config_options[URL_REWRITES];
  const char *root = conn->server->config_options[DOCUMENT_ROOT];
  const char *uri = conn->mg_conn.uri;
  const char *domain = mg_get_header(&conn->mg_conn, "Host");
  int match_len, root_len = root == NULL ? 0 : strlen(root);

  // Perform virtual hosting rewrites
  if (rewrites != NULL && domain != NULL) {
    const char *colon = strchr(domain, ':');
    int domain_len = colon == NULL ? (int) strlen(domain) : colon - domain;

    while ((rewrites = next_option(rewrites, &a, &b)) != NULL) {
      if (a.len > 1 && a.ptr[0] == '@' && a.len == domain_len + 1 &&
          mg_strncasecmp(a.ptr + 1, domain, domain_len) == 0) {
        root = b.ptr;
        root_len = b.len;
        break;
      }
    }
  }

  // No filesystem access
  if (root == NULL || root_len == 0) return 0;

  // Handle URL rewrites
  mg_snprintf(buf, buf_len, "%.*s%s", root_len, root, uri);
  rewrites = conn->server->config_options[URL_REWRITES];  // Re-initialize!
  while ((rewrites = next_option(rewrites, &a, &b)) != NULL) {
    if ((match_len = mg_match_prefix(a.ptr, a.len, uri)) > 0) {
      mg_snprintf(buf, buf_len, "%.*s%s", (int) b.len, b.ptr, uri + match_len);
      break;
    }
  }

  if (stat(buf, st) == 0) return 1;

  return 0;
}
#endif  // MONGOOSE_NO_FILESYSTEM

static int should_keep_alive(const struct mg_connection *conn) {
  struct connection *c = MG_CONN_2_CONN(conn);
  const char *method = conn->request_method;
  const char *http_version = conn->http_version;
  const char *header = mg_get_header(conn, "Connection");
  return method != NULL &&
    (!strcmp(method, "GET") || c->endpoint_type == EP_USER) &&
    ((header != NULL && !mg_strcasecmp(header, "keep-alive")) ||
     (header == NULL && http_version && !strcmp(http_version, "1.1")));
}

size_t mg_write(struct mg_connection *c, const void *buf, int len) {
  struct connection *conn = MG_CONN_2_CONN(c);
  ns_send(conn->ns_conn, buf, len);
  return conn->ns_conn->send_iobuf.len;
}

void mg_send_status(struct mg_connection *c, int status) {
  if (c->status_code == 0) {
    c->status_code = status;
    mg_printf(c, "HTTP/1.1 %d %s\r\n", status, status_code_to_str(status));
  }
}

void mg_send_header(struct mg_connection *c, const char *name, const char *v) {
  if (c->status_code == 0) {
    c->status_code = 200;
    mg_printf(c, "HTTP/1.1 %d %s\r\n", 200, status_code_to_str(200));
  }
  mg_printf(c, "%s: %s\r\n", name, v);
}

static void terminate_headers(struct mg_connection *c) {
  struct connection *conn = MG_CONN_2_CONN(c);
  if (!(conn->ns_conn->flags & MG_HEADERS_SENT)) {
    mg_send_header(c, "Transfer-Encoding", "chunked");
    mg_write(c, "\r\n", 2);
    conn->ns_conn->flags |= MG_HEADERS_SENT;
  }
}

size_t mg_send_data(struct mg_connection *c, const void *data, int data_len) {
  struct connection *conn = MG_CONN_2_CONN(c);
  terminate_headers(c);
  write_chunk(MG_CONN_2_CONN(c), (const char *) data, data_len);
  return conn->ns_conn->send_iobuf.len;
}

size_t mg_printf_data(struct mg_connection *c, const char *fmt, ...) {
  struct connection *conn = MG_CONN_2_CONN(c);
  va_list ap;
  int len;
  char mem[IOBUF_SIZE], *buf = mem;

  terminate_headers(c);

  va_start(ap, fmt);
  len = ns_avprintf(&buf, sizeof(mem), fmt, ap);
  va_end(ap);

  if (len >= 0) {
    write_chunk((struct connection *) conn, buf, len);
  }
  if (buf != mem && buf != NULL) {
    NS_FREE(buf);
  }
  return conn->ns_conn->send_iobuf.len;
}

#if !defined(MONGOOSE_NO_WEBSOCKET) || !defined(MONGOOSE_NO_AUTH)
static int is_big_endian(void) {
  static const int n = 1;
  return ((char *) &n)[0] == 0;
}
#endif

#define ping_idle_websocket_connection(conn, t)

static void write_terminating_chunk(struct connection *conn) {
  mg_write(&conn->mg_conn, "0\r\n\r\n", 5);
}

static int call_request_handler(struct connection *conn) {
  int result;
  conn->mg_conn.content = conn->ns_conn->recv_iobuf.buf;
  if ((result = call_user(conn, MG_REQUEST)) == MG_TRUE) {
    if (conn->ns_conn->flags & MG_HEADERS_SENT) {
      write_terminating_chunk(conn);
    }
    close_local_endpoint(conn);
  }
  return result;
}

const char *mg_get_mime_type(const char *path, const char *default_mime_type) {
  const char *ext;
  size_t i, path_len;

  path_len = strlen(path);

  for (i = 0; static_builtin_mime_types[i].extension != NULL; i++) {
    ext = path + (path_len - static_builtin_mime_types[i].ext_len);
    if (path_len > static_builtin_mime_types[i].ext_len &&
        mg_strcasecmp(ext, static_builtin_mime_types[i].extension) == 0) {
      return static_builtin_mime_types[i].mime_type;
    }
  }

  return default_mime_type;
}

#ifndef MONGOOSE_NO_FILESYSTEM
// Convert month to the month number. Return -1 on error, or month number
static int get_month_index(const char *s) {
  static const char *month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  int i;

  for (i = 0; i < (int) ARRAY_SIZE(month_names); i++)
    if (!strcmp(s, month_names[i]))
      return i;

  return -1;
}

static int num_leap_years(int year) {
  return year / 4 - year / 100 + year / 400;
}

// Parse UTC date-time string, and return the corresponding time_t value.
static time_t parse_date_string(const char *datetime) {
  static const unsigned short days_before_month[] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };
  char month_str[32];
  int second, minute, hour, day, month, year, leap_days, days;
  time_t result = (time_t) 0;

  if (((sscanf(datetime, "%d/%3s/%d %d:%d:%d",
               &day, month_str, &year, &hour, &minute, &second) == 6) ||
       (sscanf(datetime, "%d %3s %d %d:%d:%d",
               &day, month_str, &year, &hour, &minute, &second) == 6) ||
       (sscanf(datetime, "%*3s, %d %3s %d %d:%d:%d",
               &day, month_str, &year, &hour, &minute, &second) == 6) ||
       (sscanf(datetime, "%d-%3s-%d %d:%d:%d",
               &day, month_str, &year, &hour, &minute, &second) == 6)) &&
      year > 1970 &&
      (month = get_month_index(month_str)) != -1) {
    leap_days = num_leap_years(year) - num_leap_years(1970);
    year -= 1970;
    days = year * 365 + days_before_month[month] + (day - 1) + leap_days;
    result = days * 24 * 3600 + hour * 3600 + minute * 60 + second;
  }

  return result;
}

// Look at the "path" extension and figure what mime type it has.
// Store mime type in the vector.
static void get_mime_type(const struct mg_server *server, const char *path,
                          struct vec *vec) {
  struct vec ext_vec, mime_vec;
  const char *list, *ext;
  size_t path_len;

  path_len = strlen(path);

  // Scan user-defined mime types first, in case user wants to
  // override default mime types.
  list = server->config_options[EXTRA_MIME_TYPES];
  while ((list = next_option(list, &ext_vec, &mime_vec)) != NULL) {
    // ext now points to the path suffix
    ext = path + path_len - ext_vec.len;
    if (mg_strncasecmp(ext, ext_vec.ptr, ext_vec.len) == 0) {
      *vec = mime_vec;
      return;
    }
  }

  vec->ptr = mg_get_mime_type(path, "text/plain");
  vec->len = strlen(vec->ptr);
}

static const char *suggest_connection_header(const struct mg_connection *conn) {
  return should_keep_alive(conn) ? "keep-alive" : "close";
}

static void construct_etag(char *buf, size_t buf_len, const file_stat_t *st) {
  mg_snprintf(buf, buf_len, "\"%lx.%" INT64_FMT "\"",
              (unsigned long) st->st_mtime, (int64_t) st->st_size);
}

// Return True if we should reply 304 Not Modified.
static int is_not_modified(const struct connection *conn,
                           const file_stat_t *stp) {
  char etag[64];
  const char *ims = mg_get_header(&conn->mg_conn, "If-Modified-Since");
  const char *inm = mg_get_header(&conn->mg_conn, "If-None-Match");
  construct_etag(etag, sizeof(etag), stp);
  return (inm != NULL && !mg_strcasecmp(etag, inm)) ||
    (ims != NULL && stp->st_mtime <= parse_date_string(ims));
}

// For given directory path, substitute it to valid index file.
// Return 0 if index file has been found, -1 if not found.
// If the file is found, it's stats is returned in stp.
static int find_index_file(struct connection *conn, char *path,
                           size_t path_len, file_stat_t *stp) {
  const char *list = conn->server->config_options[INDEX_FILES];
  file_stat_t st;
  struct vec filename_vec;
  size_t n = strlen(path), found = 0;

  // The 'path' given to us points to the directory. Remove all trailing
  // directory separator characters from the end of the path, and
  // then append single directory separator character.
  while (n > 0 && path[n - 1] == '/') {
    n--;
  }
  path[n] = '/';

  // Traverse index files list. For each entry, append it to the given
  // path and see if the file exists. If it exists, break the loop
  while ((list = next_option(list, &filename_vec, NULL)) != NULL) {

    // Ignore too long entries that may overflow path buffer
    if (filename_vec.len > (int) (path_len - (n + 2)))
      continue;

    // Prepare full path to the index file
    strncpy(path + n + 1, filename_vec.ptr, filename_vec.len);
    path[n + 1 + filename_vec.len] = '\0';

    //DBG(("[%s]", path));

    // Does it exist?
    if (!stat(path, &st)) {
      // Yes it does, break the loop
      *stp = st;
      found = 1;
      break;
    }
  }

  // If no index file exists, restore directory path
  if (!found) {
    path[n] = '\0';
  }

  return found;
}

static int parse_range_header(const char *header, int64_t *a, int64_t *b) {
  return sscanf(header, "bytes=%" INT64_FMT "-%" INT64_FMT, a, b);
}

static void gmt_time_string(char *buf, size_t buf_len, time_t *t) {
  strftime(buf, buf_len, "%a, %d %b %Y %H:%M:%S GMT", gmtime(t));
}

static void open_file_endpoint(struct connection *conn, const char *path,
                               file_stat_t *st, const char *extra_headers) {
  char date[64], lm[64], etag[64], range[64], headers[1000];
  const char *msg = "OK", *hdr;
  time_t curtime = time(NULL);
  int64_t r1, r2;
  struct vec mime_vec;
  int n;

  conn->endpoint_type = EP_FILE;
  ns_set_close_on_exec(conn->endpoint.fd);
  conn->mg_conn.status_code = 200;

  get_mime_type(conn->server, path, &mime_vec);
  conn->cl = st->st_size;
  range[0] = '\0';

  // If Range: header specified, act accordingly
  r1 = r2 = 0;
  hdr = mg_get_header(&conn->mg_conn, "Range");
  if (hdr != NULL && (n = parse_range_header(hdr, &r1, &r2)) > 0 &&
      r1 >= 0 && r2 >= 0) {
    conn->mg_conn.status_code = 206;
    conn->cl = n == 2 ? (r2 > conn->cl ? conn->cl : r2) - r1 + 1: conn->cl - r1;
    mg_snprintf(range, sizeof(range), "Content-Range: bytes "
                "%" INT64_FMT "-%" INT64_FMT "/%" INT64_FMT "\r\n",
                r1, r1 + conn->cl - 1, (int64_t) st->st_size);
    msg = "Partial Content";
    lseek(conn->endpoint.fd, r1, SEEK_SET);
  }

  // Prepare Etag, Date, Last-Modified headers. Must be in UTC, according to
  // http://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html#sec3.3
  gmt_time_string(date, sizeof(date), &curtime);
  gmt_time_string(lm, sizeof(lm), &st->st_mtime);
  construct_etag(etag, sizeof(etag), st);

  n = mg_snprintf(headers, sizeof(headers),
                  "HTTP/1.1 %d %s\r\n"
                  "Date: %s\r\n"
                  "Last-Modified: %s\r\n"
                  "Etag: %s\r\n"
                  "Content-Type: %.*s\r\n"
                  "Content-Length: %" INT64_FMT "\r\n"
                  "Connection: %s\r\n"
                  "Accept-Ranges: bytes\r\n"
                  "%s%s%s\r\n",
                  conn->mg_conn.status_code, msg, date, lm, etag,
                  (int) mime_vec.len, mime_vec.ptr, conn->cl,
                  suggest_connection_header(&conn->mg_conn),
                  range, extra_headers == NULL ? "" : extra_headers,
                  MONGOOSE_USE_EXTRA_HTTP_HEADERS);
  ns_send(conn->ns_conn, headers, n);

  if (!strcmp(conn->mg_conn.request_method, "HEAD")) {
    conn->ns_conn->flags |= NSF_FINISHED_SENDING_DATA;
    close(conn->endpoint.fd);
    conn->endpoint_type = EP_NONE;
  }
}

void mg_send_file_data(struct mg_connection *c, int fd) {
  struct connection *conn = MG_CONN_2_CONN(c);
  conn->endpoint_type = EP_FILE;
  conn->endpoint.fd = fd;
  ns_set_close_on_exec(conn->endpoint.fd);
}
#endif  // MONGOOSE_NO_FILESYSTEM

static void call_request_handler_if_data_is_buffered(struct connection *conn) {
  if (conn->num_bytes_recv >= (conn->cl + conn->request_len) &&
      call_request_handler(conn) == MG_FALSE) {
    open_local_endpoint(conn, 1);
  }
}

#if !defined(MONGOOSE_NO_DIRECTORY_LISTING) || !defined(MONGOOSE_NO_DAV)

#ifdef _WIN32
struct dirent {
  char d_name[MAX_PATH_SIZE];
};

typedef struct DIR {
  HANDLE   handle;
  WIN32_FIND_DATAW info;
  struct dirent result;
} DIR;

// Implementation of POSIX opendir/closedir/readdir for Windows.
static DIR *opendir(const char *name) {
  DIR *dir = NULL;
  wchar_t wpath[MAX_PATH_SIZE];
  DWORD attrs;

  if (name == NULL) {
    SetLastError(ERROR_BAD_ARGUMENTS);
  } else if ((dir = (DIR *) NS_MALLOC(sizeof(*dir))) == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  } else {
    to_wchar(name, wpath, ARRAY_SIZE(wpath));
    attrs = GetFileAttributesW(wpath);
    if (attrs != 0xFFFFFFFF &&
        ((attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)) {
      (void) wcscat(wpath, L"\\*");
      dir->handle = FindFirstFileW(wpath, &dir->info);
      dir->result.d_name[0] = '\0';
    } else {
      NS_FREE(dir);
      dir = NULL;
    }
  }

  return dir;
}

static int closedir(DIR *dir) {
  int result = 0;

  if (dir != NULL) {
    if (dir->handle != INVALID_HANDLE_VALUE)
      result = FindClose(dir->handle) ? 0 : -1;

    NS_FREE(dir);
  } else {
    result = -1;
    SetLastError(ERROR_BAD_ARGUMENTS);
  }

  return result;
}

static struct dirent *readdir(DIR *dir) {
  struct dirent *result = 0;

  if (dir) {
    if (dir->handle != INVALID_HANDLE_VALUE) {
      result = &dir->result;
      (void) WideCharToMultiByte(CP_UTF8, 0,
          dir->info.cFileName, -1, result->d_name,
          sizeof(result->d_name), NULL, NULL);

      if (!FindNextFileW(dir->handle, &dir->info)) {
        (void) FindClose(dir->handle);
        dir->handle = INVALID_HANDLE_VALUE;
      }

    } else {
      SetLastError(ERROR_FILE_NOT_FOUND);
    }
  } else {
    SetLastError(ERROR_BAD_ARGUMENTS);
  }

  return result;
}
#endif // _WIN32  POSIX opendir/closedir/readdir implementation

static int scan_directory(struct connection *conn, const char *dir,
                          struct dir_entry **arr) {
  char path[MAX_PATH_SIZE];
  struct dir_entry *p;
  struct dirent *dp;
  int arr_size = 0, arr_ind = 0, inc = 100;
  DIR *dirp;

  *arr = NULL;
  if ((dirp = (opendir(dir))) == NULL) return 0;

  while ((dp = readdir(dirp)) != NULL) {
    // Do not show current dir and hidden files
    if (!strcmp(dp->d_name, ".") ||
        !strcmp(dp->d_name, "..") ||
        must_hide_file(conn, dp->d_name)) {
      continue;
    }
    mg_snprintf(path, sizeof(path), "%s%c%s", dir, '/', dp->d_name);

    // Resize the array if nesessary
    if (arr_ind >= arr_size) {
      if ((p = (struct dir_entry *)
           NS_REALLOC(*arr, (inc + arr_size) * sizeof(**arr))) != NULL) {
        // Memset new chunk to zero, otherwize st_mtime will have garbage which
        // can make strftime() segfault, see
        // http://code.google.com/p/mongoose/issues/detail?id=79
        memset(p + arr_size, 0, sizeof(**arr) * inc);

        *arr = p;
        arr_size += inc;
      }
    }

    if (arr_ind < arr_size) {
      (*arr)[arr_ind].conn = conn;
      (*arr)[arr_ind].file_name = strdup(dp->d_name);
      stat(path, &(*arr)[arr_ind].st);
      arr_ind++;
    }
  }
  closedir(dirp);

  return arr_ind;
}

int mg_url_encode(const char *src, size_t s_len, char *dst, size_t dst_len) {
  static const char *dont_escape = "._-$,;~()";
  static const char *hex = "0123456789abcdef";
  size_t i = 0, j = 0;

  for (i = j = 0; dst_len > 0 && i < s_len && j + 2 < dst_len - 1; i++, j++) {
    if (isalnum(* (const unsigned char *) (src + i)) ||
        strchr(dont_escape, * (const unsigned char *) (src + i)) != NULL) {
      dst[j] = src[i];
    } else if (j + 3 < dst_len) {
      dst[j] = '%';
      dst[j + 1] = hex[(* (const unsigned char *) (src + i)) >> 4];
      dst[j + 2] = hex[(* (const unsigned char *) (src + i)) & 0xf];
      j += 2;
    }
  }

  dst[j] = '\0';
  return j;
}
#endif  // !NO_DIRECTORY_LISTING || !MONGOOSE_NO_DAV

#ifndef MONGOOSE_NO_DIRECTORY_LISTING

static void print_dir_entry(const struct dir_entry *de) {
  char size[64], mod[64], href[MAX_PATH_SIZE * 3];
  int64_t fsize = de->st.st_size;
  int is_dir = S_ISDIR(de->st.st_mode);
  const char *slash = is_dir ? "/" : "";

  if (is_dir) {
    mg_snprintf(size, sizeof(size), "%s", "[DIRECTORY]");
  } else {
     // We use (signed) cast below because MSVC 6 compiler cannot
     // convert unsigned __int64 to double.
    if (fsize < 1024) {
      mg_snprintf(size, sizeof(size), "%d", (int) fsize);
    } else if (fsize < 0x100000) {
      mg_snprintf(size, sizeof(size), "%.1fk", (double) fsize / 1024.0);
    } else if (fsize < 0x40000000) {
      mg_snprintf(size, sizeof(size), "%.1fM", (double) fsize / 1048576);
    } else {
      mg_snprintf(size, sizeof(size), "%.1fG", (double) fsize / 1073741824);
    }
  }
  strftime(mod, sizeof(mod), "%d-%b-%Y %H:%M", localtime(&de->st.st_mtime));
  mg_url_encode(de->file_name, strlen(de->file_name), href, sizeof(href));
  mg_printf_data(&de->conn->mg_conn,
                  "<tr><td><a href=\"%s%s\">%s%s</a></td>"
                  "<td>&nbsp;%s</td><td>&nbsp;&nbsp;%s</td></tr>\n",
                  href, slash, de->file_name, slash, mod, size);
}

// Sort directory entries by size, or name, or modification time.
// On windows, __cdecl specification is needed in case if project is built
// with __stdcall convention. qsort always requires __cdels callback.
static int __cdecl compare_dir_entries(const void *p1, const void *p2) {
  const struct dir_entry *a = (const struct dir_entry *) p1,
        *b = (const struct dir_entry *) p2;
  const char *qs = a->conn->mg_conn.query_string ?
    a->conn->mg_conn.query_string : "na";
  int cmp_result = 0;

  if (S_ISDIR(a->st.st_mode) && !S_ISDIR(b->st.st_mode)) {
    return -1;  // Always put directories on top
  } else if (!S_ISDIR(a->st.st_mode) && S_ISDIR(b->st.st_mode)) {
    return 1;   // Always put directories on top
  } else if (*qs == 'n') {
    cmp_result = strcmp(a->file_name, b->file_name);
  } else if (*qs == 's') {
    cmp_result = a->st.st_size == b->st.st_size ? 0 :
      a->st.st_size > b->st.st_size ? 1 : -1;
  } else if (*qs == 'd') {
    cmp_result = a->st.st_mtime == b->st.st_mtime ? 0 :
      a->st.st_mtime > b->st.st_mtime ? 1 : -1;
  }

  return qs[1] == 'd' ? -cmp_result : cmp_result;
}

static void send_directory_listing(struct connection *conn, const char *dir) {
  struct dir_entry *arr = NULL;
  int i, num_entries, sort_direction = conn->mg_conn.query_string != NULL &&
    conn->mg_conn.query_string[1] == 'd' ? 'a' : 'd';

  mg_send_header(&conn->mg_conn, "Transfer-Encoding", "chunked");
  mg_send_header(&conn->mg_conn, "Content-Type", "text/html; charset=utf-8");

  mg_printf_data(&conn->mg_conn,
              "<html><head><title>Index of %s</title>"
              "<style>th {text-align: left;}</style></head>"
              "<body><h1>Index of %s</h1><pre><table cellpadding=\"0\">"
              "<tr><th><a href=\"?n%c\">Name</a></th>"
              "<th><a href=\"?d%c\">Modified</a></th>"
              "<th><a href=\"?s%c\">Size</a></th></tr>"
              "<tr><td colspan=\"3\"><hr></td></tr>",
              conn->mg_conn.uri, conn->mg_conn.uri,
              sort_direction, sort_direction, sort_direction);

  num_entries = scan_directory(conn, dir, &arr);
  qsort(arr, num_entries, sizeof(arr[0]), compare_dir_entries);
  for (i = 0; i < num_entries; i++) {
    print_dir_entry(&arr[i]);
    NS_FREE(arr[i].file_name);
  }
  NS_FREE(arr);

  write_terminating_chunk(conn);
  close_local_endpoint(conn);
}
#endif  // MONGOOSE_NO_DIRECTORY_LISTING

static void send_options(struct connection *conn) {
  conn->mg_conn.status_code = 200;
  mg_printf(&conn->mg_conn, "%s",
            "HTTP/1.1 200 OK\r\nAllow: GET, POST, HEAD, CONNECT, PUT, "
            "DELETE, OPTIONS, PROPFIND, MKCOL\r\nDAV: 1\r\n\r\n");
  close_local_endpoint(conn);
}

#ifndef MONGOOSE_NO_AUTH
void mg_send_digest_auth_request(struct mg_connection *c) {
  struct connection *conn = MG_CONN_2_CONN(c);
  c->status_code = 401;
  mg_printf(c,
            "HTTP/1.1 401 Unauthorized\r\n"
            "WWW-Authenticate: Digest qop=\"auth\", "
            "realm=\"%s\", nonce=\"%lu\"\r\n\r\n",
            conn->server->config_options[AUTH_DOMAIN],
            (unsigned long) time(NULL));
  close_local_endpoint(conn);
}

// Use the global passwords file, if specified by auth_gpass option,
// or search for .htpasswd in the requested directory.
static FILE *open_auth_file(struct connection *conn, const char *path,
                            int is_directory) {
  char name[MAX_PATH_SIZE];
  const char *p, *gpass = conn->server->config_options[GLOBAL_AUTH_FILE];
  FILE *fp = NULL;

  if (gpass != NULL) {
    // Use global passwords file
    fp = fopen(gpass, "r");
  } else if (is_directory) {
    mg_snprintf(name, sizeof(name), "%s%c%s", path, '/', PASSWORDS_FILE_NAME);
    fp = fopen(name, "r");
  } else {
    // Try to find .htpasswd in requested directory.
    if ((p = strrchr(path, '/')) == NULL) p = path;
    mg_snprintf(name, sizeof(name), "%.*s%c%s",
                (int) (p - path), path, '/', PASSWORDS_FILE_NAME);
    fp = fopen(name, "r");
  }

  return fp;
}

#if !defined(HAVE_MD5) && !defined(MONGOOSE_NO_AUTH)
typedef struct MD5Context {
  uint32_t buf[4];
  uint32_t bits[2];
  unsigned char in[64];
} MD5_CTX;

static void byteReverse(unsigned char *buf, unsigned longs) {
  uint32_t t;

  // Forrest: MD5 expect LITTLE_ENDIAN, swap if BIG_ENDIAN
  if (is_big_endian()) {
    do {
      t = (uint32_t) ((unsigned) buf[3] << 8 | buf[2]) << 16 |
        ((unsigned) buf[1] << 8 | buf[0]);
      * (uint32_t *) buf = t;
      buf += 4;
    } while (--longs);
  }
}

#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

#define MD5STEP(f, w, x, y, z, data, s) \
  ( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x )

// Start MD5 accumulation.  Set bit count to 0 and buffer to mysterious
// initialization constants.
static void MD5Init(MD5_CTX *ctx) {
  ctx->buf[0] = 0x67452301;
  ctx->buf[1] = 0xefcdab89;
  ctx->buf[2] = 0x98badcfe;
  ctx->buf[3] = 0x10325476;

  ctx->bits[0] = 0;
  ctx->bits[1] = 0;
}

static void MD5Transform(uint32_t buf[4], uint32_t const in[16]) {
  register uint32_t a, b, c, d;

  a = buf[0];
  b = buf[1];
  c = buf[2];
  d = buf[3];

  MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
  MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
  MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
  MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
  MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
  MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
  MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
  MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
  MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
  MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
  MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
  MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
  MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
  MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
  MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
  MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

  MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
  MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
  MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
  MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
  MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
  MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
  MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
  MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
  MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
  MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
  MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
  MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
  MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
  MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
  MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
  MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

  MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
  MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
  MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
  MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
  MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
  MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
  MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
  MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
  MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
  MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
  MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
  MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
  MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
  MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
  MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
  MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

  MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
  MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
  MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
  MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
  MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
  MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
  MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
  MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
  MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
  MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
  MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
  MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
  MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
  MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
  MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
  MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

  buf[0] += a;
  buf[1] += b;
  buf[2] += c;
  buf[3] += d;
}

static void MD5Update(MD5_CTX *ctx, unsigned char const *buf, unsigned len) {
  uint32_t t;

  t = ctx->bits[0];
  if ((ctx->bits[0] = t + ((uint32_t) len << 3)) < t)
    ctx->bits[1]++;
  ctx->bits[1] += len >> 29;

  t = (t >> 3) & 0x3f;

  if (t) {
    unsigned char *p = (unsigned char *) ctx->in + t;

    t = 64 - t;
    if (len < t) {
      memcpy(p, buf, len);
      return;
    }
    memcpy(p, buf, t);
    byteReverse(ctx->in, 16);
    MD5Transform(ctx->buf, (uint32_t *) ctx->in);
    buf += t;
    len -= t;
  }

  while (len >= 64) {
    memcpy(ctx->in, buf, 64);
    byteReverse(ctx->in, 16);
    MD5Transform(ctx->buf, (uint32_t *) ctx->in);
    buf += 64;
    len -= 64;
  }

  memcpy(ctx->in, buf, len);
}

static void MD5Final(unsigned char digest[16], MD5_CTX *ctx) {
  unsigned count;
  unsigned char *p;
  uint32_t *a;

  count = (ctx->bits[0] >> 3) & 0x3F;

  p = ctx->in + count;
  *p++ = 0x80;
  count = 64 - 1 - count;
  if (count < 8) {
    memset(p, 0, count);
    byteReverse(ctx->in, 16);
    MD5Transform(ctx->buf, (uint32_t *) ctx->in);
    memset(ctx->in, 0, 56);
  } else {
    memset(p, 0, count - 8);
  }
  byteReverse(ctx->in, 14);

  a = (uint32_t *)ctx->in;
  a[14] = ctx->bits[0];
  a[15] = ctx->bits[1];

  MD5Transform(ctx->buf, (uint32_t *) ctx->in);
  byteReverse((unsigned char *) ctx->buf, 4);
  memcpy(digest, ctx->buf, 16);
  memset((char *) ctx, 0, sizeof(*ctx));
}
#endif // !HAVE_MD5



// Stringify binary data. Output buffer must be twice as big as input,
// because each byte takes 2 bytes in string representation
static void bin2str(char *to, const unsigned char *p, size_t len) {
  static const char *hex = "0123456789abcdef";

  for (; len--; p++) {
    *to++ = hex[p[0] >> 4];
    *to++ = hex[p[0] & 0x0f];
  }
  *to = '\0';
}

// Return stringified MD5 hash for list of strings. Buffer must be 33 bytes.
char *mg_md5(char buf[33], ...) {
  unsigned char hash[16];
  const char *p;
  va_list ap;
  MD5_CTX ctx;

  MD5Init(&ctx);

  va_start(ap, buf);
  while ((p = va_arg(ap, const char *)) != NULL) {
    MD5Update(&ctx, (const unsigned char *) p, (unsigned) strlen(p));
  }
  va_end(ap);

  MD5Final(hash, &ctx);
  bin2str(buf, hash, sizeof(hash));
  return buf;
}

// Check the user's password, return 1 if OK
static int check_password(const char *method, const char *ha1, const char *uri,
                          const char *nonce, const char *nc, const char *cnonce,
                          const char *qop, const char *response) {
  char ha2[32 + 1], expected_response[32 + 1];

#if 0
  // Check for authentication timeout
  if ((unsigned long) time(NULL) - (unsigned long) to64(nonce) > 3600 * 2) {
    return 0;
  }
#endif

  mg_md5(ha2, method, ":", uri, NULL);
  mg_md5(expected_response, ha1, ":", nonce, ":", nc,
      ":", cnonce, ":", qop, ":", ha2, NULL);

  return mg_strcasecmp(response, expected_response) == 0 ? MG_TRUE : MG_FALSE;
}


// Authorize against the opened passwords file. Return 1 if authorized.
int mg_authorize_digest(struct mg_connection *c, FILE *fp) {
  struct connection *conn = MG_CONN_2_CONN(c);
  const char *hdr;
  char line[256], f_user[256], ha1[256], f_domain[256], user[100], nonce[100],
       uri[MAX_REQUEST_SIZE], cnonce[100], resp[100], qop[100], nc[100];

  if (c == NULL || fp == NULL) return 0;
  if ((hdr = mg_get_header(c, "Authorization")) == NULL ||
      mg_strncasecmp(hdr, "Digest ", 7) != 0) return 0;
  if (!mg_parse_header(hdr, "username", user, sizeof(user))) return 0;
  if (!mg_parse_header(hdr, "cnonce", cnonce, sizeof(cnonce))) return 0;
  if (!mg_parse_header(hdr, "response", resp, sizeof(resp))) return 0;
  if (!mg_parse_header(hdr, "uri", uri, sizeof(uri))) return 0;
  if (!mg_parse_header(hdr, "qop", qop, sizeof(qop))) return 0;
  if (!mg_parse_header(hdr, "nc", nc, sizeof(nc))) return 0;
  if (!mg_parse_header(hdr, "nonce", nonce, sizeof(nonce))) return 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    if (sscanf(line, "%[^:]:%[^:]:%s", f_user, f_domain, ha1) == 3 &&
        !strcmp(user, f_user) &&
        // NOTE(lsm): due to a bug in MSIE, we do not compare URIs
        !strcmp(conn->server->config_options[AUTH_DOMAIN], f_domain))
      return check_password(c->request_method, ha1, uri,
                            nonce, nc, cnonce, qop, resp);
  }
  return MG_FALSE;
}


// Return 1 if request is authorised, 0 otherwise.
static int is_authorized(struct connection *conn, const char *path,
                         int is_directory) {
  FILE *fp;
  int authorized = MG_TRUE;

  if ((fp = open_auth_file(conn, path, is_directory)) != NULL) {
    authorized = mg_authorize_digest(&conn->mg_conn, fp);
    fclose(fp);
  }

  return authorized;
}

static int is_authorized_for_dav(struct connection *conn) {
  const char *auth_file = conn->server->config_options[DAV_AUTH_FILE];
  const char *method = conn->mg_conn.request_method;
  FILE *fp;
  int authorized = MG_FALSE;

  // If dav_auth_file is not set, allow non-authorized PROPFIND
  if (method != NULL && !strcmp(method, "PROPFIND") && auth_file == NULL) {
    authorized = MG_TRUE;
  } else if (auth_file != NULL && (fp = fopen(auth_file, "r")) != NULL) {
    authorized = mg_authorize_digest(&conn->mg_conn, fp);
    fclose(fp);
  }

  return authorized;
}

static int is_dav_request(const struct connection *conn) {
  const char *s = conn->mg_conn.request_method;
  return !strcmp(s, "PUT") || !strcmp(s, "DELETE") ||
    !strcmp(s, "MKCOL") || !strcmp(s, "PROPFIND");
}
#endif // MONGOOSE_NO_AUTH

static int parse_header(const char *str, int str_len, const char *var_name,
                        char *buf, size_t buf_size) {
  int ch = ' ', len = 0, n = strlen(var_name);
  const char *p, *end = str + str_len, *s = NULL;

  if (buf != NULL && buf_size > 0) buf[0] = '\0';

  // Find where variable starts
  for (s = str; s != NULL && s + n < end; s++) {
    if ((s == str || s[-1] == ' ' || s[-1] == ',') && s[n] == '=' &&
        !memcmp(s, var_name, n)) break;
  }

  if (s != NULL && &s[n + 1] < end) {
    s += n + 1;
    if (*s == '"' || *s == '\'') ch = *s++;
    p = s;
    while (p < end && p[0] != ch && p[0] != ',' && len < (int) buf_size) {
      if (p[0] == '\\' && p[1] == ch) p++;
      buf[len++] = *p++;
    }
    if (len >= (int) buf_size || (ch != ' ' && *p != ch)) {
      len = 0;
    } else {
      if (len > 0 && s[len - 1] == ',') len--;
      if (len > 0 && s[len - 1] == ';') len--;
      buf[len] = '\0';
    }
  }

  return len;
}

int mg_parse_header(const char *s, const char *var_name, char *buf,
                    size_t buf_size) {
  return parse_header(s, s == NULL ? 0 : strlen(s), var_name, buf, buf_size);
}

static void proxy_request(struct ns_connection *pc, struct mg_connection *c) {
  int i, sent_close_header = 0;

  ns_printf(pc, "%s %s%s%s HTTP/%s\r\n", c->request_method, c->uri,
            c->query_string ? "?" : "",
            c->query_string ? c->query_string : "",
            c->http_version);
  for (i = 0; i < c->num_headers; i++) {
    if (mg_strcasecmp(c->http_headers[i].name, "Connection") == 0) {
      // Force connection close, cause we don't parse proxy replies
      // therefore we don't know message boundaries
      ns_printf(pc, "%s: %s\r\n", "Connection", "close");
      sent_close_header = 1;
    } else {
      ns_printf(pc, "%s: %s\r\n", c->http_headers[i].name,
                c->http_headers[i].value);
    }
  }
  if (!sent_close_header) {
    ns_printf(pc, "%s: %s\r\n", "Connection", "close");
  }
  ns_printf(pc, "%s", "\r\n");
  ns_send(pc, c->content, c->content_len);

}

int mg_forward(struct mg_connection *c, const char *addr) {
  static const char ok[] = "HTTP/1.1 200 OK\r\n\r\n";
  struct connection *conn = MG_CONN_2_CONN(c);
  struct ns_connection *pc;

  if ((pc = ns_connect(&conn->server->ns_mgr, addr,
      mg_ev_handler, conn)) == NULL) {
    conn->ns_conn->flags |= NSF_CLOSE_IMMEDIATELY;
    return 0;
  }

  // Interlink two connections
  pc->flags |= MG_PROXY_CONN;
  conn->endpoint_type = EP_PROXY;
  conn->endpoint.nc = pc;
  DBG(("%p [%s] [%s] -> %p %p", conn, c->uri, addr, pc, conn->ns_conn->ssl));

  if (strcmp(c->request_method, "CONNECT") == 0) {
    // For CONNECT request, reply with 200 OK. Tunnel is established.
    // TODO(lsm): check for send() failure
    (void) send(conn->ns_conn->sock, ok, sizeof(ok) - 1, 0);
  } else {
    // Strip "http://host:port" part from the URI
    if (memcmp(c->uri, "http://", 7) == 0) c->uri += 7;
    while (*c->uri != '\0' && *c->uri != '/') c->uri++;
    proxy_request(pc, c);
  }
  return 1;
}

static void proxify_connection(struct connection *conn) {
  char proto[10], host[500], cert[500], addr[1000];
  unsigned short port = 80;
  struct mg_connection *c = &conn->mg_conn;
  int n = 0;
  const char *url = c->uri;

  proto[0] = host[0] = cert[0] = '\0';
  if (sscanf(url, "%499[^: ]:%hu%n", host, &port, &n) != 2 &&
      sscanf(url, "%9[a-z]://%499[^: ]:%hu%n", proto, host, &port, &n) != 3 &&
      sscanf(url, "%9[a-z]://%499[^/ ]%n", proto, host, &n) != 2) {
    n = 0;
  }

  snprintf(addr, sizeof(addr), "%s://%s:%hu",
           conn->ns_conn->ssl != NULL ? "ssl" : "tcp", host, port);
  if (n <= 0 || !mg_forward(c, addr)) {
    conn->ns_conn->flags |= NSF_CLOSE_IMMEDIATELY;
  }
}

#ifndef MONGOOSE_NO_FILESYSTEM
void mg_send_file_internal(struct mg_connection *c, const char *file_name,
                           file_stat_t *st, int exists,
                           const char *extra_headers) {
  struct connection *conn = MG_CONN_2_CONN(c);
  char path[MAX_PATH_SIZE];
  const int is_directory = S_ISDIR(st->st_mode);
  const char *cgi_pat = DEFAULT_CGI_PATTERN;
#ifndef MONGOOSE_NO_DIRECTORY_LISTING
  const char *dir_lst = conn->server->config_options[ENABLE_DIRECTORY_LISTING];
#else
  const char *dir_lst = "yes";
#endif

  mg_snprintf(path, sizeof(path), "%s", file_name);

  if (!exists || must_hide_file(conn, path)) {
    send_http_error(conn, 404, NULL);
  } else if (is_directory &&
             conn->mg_conn.uri[strlen(conn->mg_conn.uri) - 1] != '/') {
    conn->mg_conn.status_code = 301;
    mg_printf(&conn->mg_conn, "HTTP/1.1 301 Moved Permanently\r\n"
              "Location: %s/\r\n\r\n", conn->mg_conn.uri);
    close_local_endpoint(conn);
  } else if (is_directory && !find_index_file(conn, path, sizeof(path), st)) {
    if (!mg_strcasecmp(dir_lst, "yes")) {
#ifndef MONGOOSE_NO_DIRECTORY_LISTING
      send_directory_listing(conn, path);
#else
      send_http_error(conn, 501, NULL);
#endif
    } else {
      send_http_error(conn, 403, NULL);
    }
  } else if (mg_match_prefix(cgi_pat, strlen(cgi_pat), path) > 0) {
    send_http_error(conn, 501, NULL);
  } else if (is_not_modified(conn, st)) {
    send_http_error(conn, 304, NULL);
  } else if ((conn->endpoint.fd = open(path, O_RDONLY | O_BINARY, 0)) != -1) {
    // O_BINARY is required for Windows, otherwise in default text mode
    // two bytes \r\n will be read as one.
    open_file_endpoint(conn, path, st, extra_headers);
  } else {
    send_http_error(conn, 404, NULL);
  }
}
void mg_send_file(struct mg_connection *c, const char *file_name,
                  const char *extra_headers) {
  file_stat_t st;
  const int exists = stat(file_name, &st) == 0;
  mg_send_file_internal(c, file_name, &st, exists, extra_headers);
}
#endif  // !MONGOOSE_NO_FILESYSTEM

static void open_local_endpoint(struct connection *conn, int skip_user) {
#ifndef MONGOOSE_NO_FILESYSTEM
  char path[MAX_PATH_SIZE];
  file_stat_t st;
  int exists = 0;
#endif

  // If EP_USER was set in a prev call, reset it
  conn->endpoint_type = EP_NONE;

#ifndef MONGOOSE_NO_AUTH
  if (conn->server->event_handler && call_user(conn, MG_AUTH) == MG_FALSE) {
    mg_send_digest_auth_request(&conn->mg_conn);
    return;
  }
#endif

  // Call URI handler if one is registered for this URI
  if (skip_user == 0 && conn->server->event_handler != NULL) {
    conn->endpoint_type = EP_USER;
#if MONGOOSE_POST_SIZE_LIMIT > 1
    {
      const char *cl = mg_get_header(&conn->mg_conn, "Content-Length");
      if ((strcmp(conn->mg_conn.request_method, "POST") == 0 ||
           strcmp(conn->mg_conn.request_method, "PUT") == 0) &&
          (cl == NULL || to64(cl) > MONGOOSE_POST_SIZE_LIMIT)) {
        send_http_error(conn, 500, "POST size > %lu",
                        (unsigned long) MONGOOSE_POST_SIZE_LIMIT);
      }
    }
#endif
    return;
  }

  if (strcmp(conn->mg_conn.request_method, "CONNECT") == 0 ||
      mg_strncasecmp(conn->mg_conn.uri, "http", 4) == 0) {
    const char *enp = conn->server->config_options[ENABLE_PROXY];
    if (enp == NULL || strcmp(enp, "yes") != 0) {
      send_http_error(conn, 405, NULL);
    } else {
      proxify_connection(conn);
    }
    return;
  }

  if (!strcmp(conn->mg_conn.request_method, "OPTIONS")) {
    send_options(conn);
    return;
  }

#ifdef MONGOOSE_NO_FILESYSTEM
  send_http_error(conn, 404, NULL);
#else
  exists = convert_uri_to_file_name(conn, path, sizeof(path), &st);

  if (!strcmp(conn->mg_conn.request_method, "OPTIONS")) {
    send_options(conn);
  } else if (conn->server->config_options[DOCUMENT_ROOT] == NULL) {
    send_http_error(conn, 404, NULL);
#ifndef MONGOOSE_NO_AUTH
  } else if ((!is_dav_request(conn) && !is_authorized(conn, path,
               exists && S_ISDIR(st.st_mode))) ||
             (is_dav_request(conn) && !is_authorized_for_dav(conn))) {
    mg_send_digest_auth_request(&conn->mg_conn);
    close_local_endpoint(conn);
#endif
  } else {
    mg_send_file_internal(&conn->mg_conn, path, &st, exists, NULL);
  }
#endif  // MONGOOSE_NO_FILESYSTEM
}

static void send_continue_if_expected(struct connection *conn) {
  static const char expect_response[] = "HTTP/1.1 100 Continue\r\n\r\n";
  const char *expect_hdr = mg_get_header(&conn->mg_conn, "Expect");

  if (expect_hdr != NULL && !mg_strcasecmp(expect_hdr, "100-continue")) {
    ns_send(conn->ns_conn, expect_response, sizeof(expect_response) - 1);
  }
}

// Conform to http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html#sec5.1.2
static int is_valid_uri(const char *uri) {
  unsigned short n;
  return uri[0] == '/' ||
    strcmp(uri, "*") == 0 ||            // OPTIONS method can use asterisk URI
    mg_strncasecmp(uri, "http", 4) == 0 || // Naive check for the absolute URI
    sscanf(uri, "%*[^ :]:%hu", &n) > 0; // CONNECT method can use host:port
}

static void try_parse(struct connection *conn) {
  struct iobuf *io = &conn->ns_conn->recv_iobuf;

  if (conn->request_len == 0 &&
      (conn->request_len = get_request_len(io->buf, io->len)) > 0) {
    // If request is buffered in, remove it from the iobuf. This is because
    // iobuf could be reallocated, and pointers in parsed request could
    // become invalid.
    conn->request = (char *) NS_MALLOC(conn->request_len);
    memcpy(conn->request, io->buf, conn->request_len);
    //DBG(("%p [%.*s]", conn, conn->request_len, conn->request));
    iobuf_remove(io, conn->request_len);
    conn->request_len = parse_http_message(conn->request, conn->request_len,
                                           &conn->mg_conn);
    if (conn->request_len > 0) {
      const char *cl_hdr = mg_get_header(&conn->mg_conn, "Content-Length");
      conn->cl = cl_hdr == NULL ? 0 : to64(cl_hdr);
      conn->mg_conn.content_len = (size_t) conn->cl;
    }
  }
}

static void do_proxy(struct connection *conn) {
  if (0 && conn->request_len == 0) {
    try_parse(conn);
    DBG(("%p parsing -> %d", conn, conn->request_len));
    if (conn->request_len > 0 && call_user(conn, MG_REQUEST) == MG_FALSE) {
      proxy_request(conn->endpoint.nc, &conn->mg_conn);
    } else if (conn->request_len < 0) {
      ns_forward(conn->ns_conn, conn->endpoint.nc);
    }
  } else {
    DBG(("%p forwarding", conn));
    ns_forward(conn->ns_conn, conn->endpoint.nc);
  }
}

static void on_recv_data(struct connection *conn) {
  struct iobuf *io = &conn->ns_conn->recv_iobuf;
  int n;

  if (conn->endpoint_type == EP_PROXY) {
    if (conn->endpoint.nc != NULL) do_proxy(conn);
    return;
  }

  try_parse(conn);
  DBG(("%p %d %lu %d", conn, conn->request_len, (unsigned long)io->len,
       conn->ns_conn->flags));
  if (conn->request_len < 0 ||
      (conn->request_len > 0 && !is_valid_uri(conn->mg_conn.uri))) {
    send_http_error(conn, 400, NULL);
  } else if (conn->request_len == 0 && io->len > MAX_REQUEST_SIZE) {
    send_http_error(conn, 413, NULL);
  } else if (conn->request_len > 0 &&
             strcmp(conn->mg_conn.http_version, "1.0") != 0 &&
             strcmp(conn->mg_conn.http_version, "1.1") != 0) {
    send_http_error(conn, 505, NULL);
  } else if (conn->request_len > 0 && conn->endpoint_type == EP_NONE) {
    send_continue_if_expected(conn);
    open_local_endpoint(conn, 0);
  }

  if (conn->endpoint_type == EP_USER) {
    conn->mg_conn.content = io->buf;
    conn->mg_conn.content_len = io->len;
    n = call_user(conn, MG_RECV);
    if (n < 0) {
      conn->ns_conn->flags |= NSF_FINISHED_SENDING_DATA;
    } else if ((size_t) n <= io->len) {
      iobuf_remove(io, n);
    }
    call_request_handler_if_data_is_buffered(conn);
  }
}

static void call_http_client_handler(struct connection *conn) {
  //conn->mg_conn.status_code = code;
  // For responses without Content-Lengh, use the whole buffer
  if (conn->cl == 0) {
    conn->mg_conn.content_len = conn->ns_conn->recv_iobuf.len;
  }
  conn->mg_conn.content = conn->ns_conn->recv_iobuf.buf;
  if (call_user(conn, MG_REPLY) == MG_FALSE) {
    conn->ns_conn->flags |= NSF_CLOSE_IMMEDIATELY;
  }
  iobuf_remove(&conn->ns_conn->recv_iobuf, conn->mg_conn.content_len);
  conn->mg_conn.status_code = 0;
  conn->cl = conn->num_bytes_recv = conn->request_len = 0;
  NS_FREE(conn->request);
  conn->request = NULL;
}

static void process_response(struct connection *conn) {
  struct iobuf *io = &conn->ns_conn->recv_iobuf;

  try_parse(conn);
  DBG(("%p %d %lu", conn, conn->request_len, (unsigned long)io->len));
  if (conn->request_len < 0 ||
      (conn->request_len == 0 && io->len > MAX_REQUEST_SIZE)) {
    call_http_client_handler(conn);
  } else if ((int64_t) io->len >= conn->cl) {
    call_http_client_handler(conn);
  }
}

struct mg_connection *mg_connect(struct mg_server *server, const char *addr) {
  struct ns_connection *nsconn;
  struct connection *conn;

  nsconn = ns_connect(&server->ns_mgr, addr, mg_ev_handler, NULL);
  if (nsconn == NULL) return 0;

  if ((conn = (struct connection *) NS_CALLOC(1, sizeof(*conn))) == NULL) {
    nsconn->flags |= NSF_CLOSE_IMMEDIATELY;
    return 0;
  }

  // Interlink two structs
  conn->ns_conn = nsconn;
  nsconn->user_data = conn;

  conn->server = server;
  conn->endpoint_type = EP_CLIENT;
  //conn->handler = handler;
  conn->mg_conn.server_param = server->ns_mgr.user_data;
  conn->ns_conn->flags = NSF_CONNECTING;

  return &conn->mg_conn;
}

#ifndef MONGOOSE_NO_LOGGING
static void log_header(const struct mg_connection *conn, const char *header,
                       FILE *fp) {
  const char *header_value;

  if ((header_value = mg_get_header(conn, header)) == NULL) {
    (void) fprintf(fp, "%s", " -");
  } else {
    (void) fprintf(fp, " \"%s\"", header_value);
  }
}

static void log_access(const struct connection *conn, const char *path) {
  const struct mg_connection *c = &conn->mg_conn;
  FILE *fp = (path == NULL) ?  NULL : fopen(path, "a+");
  char date[64], user[100];
  time_t now;

  if (fp == NULL) return;
  now = time(NULL);
  strftime(date, sizeof(date), "%d/%b/%Y:%H:%M:%S %z", localtime(&now));

  flockfile(fp);
  mg_parse_header(mg_get_header(&conn->mg_conn, "Authorization"), "username",
                  user, sizeof(user));
  fprintf(fp, "%s - %s [%s] \"%s %s%s%s HTTP/%s\" %d 0",
          c->remote_ip, user[0] == '\0' ? "-" : user, date,
          c->request_method ? c->request_method : "-",
          c->uri ? c->uri : "-", c->query_string ? "?" : "",
          c->query_string ? c->query_string : "",
          c->http_version, c->status_code);
  log_header(c, "Referer", fp);
  log_header(c, "User-Agent", fp);
  fputc('\n', fp);
  fflush(fp);

  funlockfile(fp);
  fclose(fp);
}
#endif

static void close_local_endpoint(struct connection *conn) {
  struct mg_connection *c = &conn->mg_conn;
  // Must be done before free()
  int keep_alive = should_keep_alive(&conn->mg_conn) &&
    (conn->endpoint_type == EP_FILE || conn->endpoint_type == EP_USER);
  DBG(("%p %d %d %d", conn, conn->endpoint_type, keep_alive,
       conn->ns_conn->flags));

  switch (conn->endpoint_type) {
    case EP_PUT:
    case EP_FILE:
      close(conn->endpoint.fd);
      break;
    case EP_CGI:
    case EP_PROXY:
      if (conn->endpoint.nc != NULL) {
        DBG(("%p %p %p :-)", conn, conn->ns_conn, conn->endpoint.nc));
        conn->endpoint.nc->flags |= NSF_CLOSE_IMMEDIATELY;
        conn->endpoint.nc->user_data = NULL;
      }
      break;
    default: break;
  }

#ifndef MONGOOSE_NO_LOGGING
  if (c->status_code > 0 && conn->endpoint_type != EP_CLIENT &&
      c->status_code != 400) {
    log_access(conn, conn->server->config_options[ACCESS_LOG_FILE]);
  }
#endif

  // Gobble possible POST data sent to the URI handler
  iobuf_free(&conn->ns_conn->recv_iobuf);
  NS_FREE(conn->request);
  NS_FREE(conn->path_info);
  conn->endpoint.nc = NULL;
  conn->request = conn->path_info = NULL;

  conn->endpoint_type = EP_NONE;
  conn->cl = conn->num_bytes_recv = conn->request_len = 0;
  conn->ns_conn->flags &= ~(NSF_FINISHED_SENDING_DATA |
                            NSF_BUFFER_BUT_DONT_SEND | NSF_CLOSE_IMMEDIATELY |
                            MG_HEADERS_SENT | MG_LONG_RUNNING);

  // Do not memset() the whole structure, as some of the fields
  // (IP addresses & ports, server_param) must survive. Nullify the rest.
  c->request_method = c->uri = c->http_version = c->query_string = NULL;
  c->num_headers = c->status_code = c->is_websocket = c->content_len = 0;
  c->connection_param = c->callback_param = NULL;

  if (keep_alive) {
    on_recv_data(conn);  // Can call us recursively if pipelining is used
  } else {
    conn->ns_conn->flags |= conn->ns_conn->send_iobuf.len == 0 ?
      NSF_CLOSE_IMMEDIATELY : NSF_FINISHED_SENDING_DATA;
  }
}

static void transfer_file_data(struct connection *conn) {
  char buf[IOBUF_SIZE];
  int n;

  // If output buffer is too big, don't send anything. Wait until
  // mongoose drains already buffered data to the client.
  if (conn->ns_conn->send_iobuf.len > sizeof(buf) * 2) return;

  // Do not send anyt
  n = read(conn->endpoint.fd, buf, conn->cl < (int64_t) sizeof(buf) ?
           (int) conn->cl : (int) sizeof(buf));

  if (n <= 0) {
    close_local_endpoint(conn);
  } else if (n > 0) {
    conn->cl -= n;
    ns_send(conn->ns_conn, buf, n);
    if (conn->cl <= 0) {
      close_local_endpoint(conn);
    }
  }
}

int mg_poll_server(struct mg_server *server, int milliseconds) {
  return (int)ns_mgr_poll(&server->ns_mgr, milliseconds);
}

void mg_destroy_server(struct mg_server **server) {
  if (server != NULL && *server != NULL) {
    struct mg_server *s = *server;
    int i;

    ns_mgr_free(&s->ns_mgr);
    for (i = 0; i < (int) ARRAY_SIZE(s->config_options); i++) {
      NS_FREE(s->config_options[i]);  // It is OK to free(NULL)
    }
    NS_FREE(s);
    *server = NULL;
  }
}

struct mg_connection *mg_next(struct mg_server *s, struct mg_connection *c) {
  struct ns_connection *nc = ns_next(&s->ns_mgr, c == NULL ? NULL :
                                     MG_CONN_2_CONN(c)->ns_conn);
  if (nc != NULL && nc->user_data != NULL) {
    return & ((struct connection *) nc->user_data)->mg_conn;
  } else {
    return NULL;
  }
}

static int get_var(const char *data, size_t data_len, const char *name,
                   char *dst, size_t dst_len) {
  const char *p, *e, *s;
  size_t name_len;
  int len;

  if (dst == NULL || dst_len == 0) {
    len = -2;
  } else if (data == NULL || name == NULL || data_len == 0) {
    len = -1;
    dst[0] = '\0';
  } else {
    name_len = strlen(name);
    e = data + data_len;
    len = -1;
    dst[0] = '\0';

    // data is "var1=val1&var2=val2...". Find variable first
    for (p = data; p + name_len < e; p++) {
      if ((p == data || p[-1] == '&') && p[name_len] == '=' &&
          !mg_strncasecmp(name, p, name_len)) {

        // Point p to variable value
        p += name_len + 1;

        // Point s to the end of the value
        s = (const char *) memchr(p, '&', (size_t)(e - p));
        if (s == NULL) {
          s = e;
        }
        assert(s >= p);

        // Decode variable into destination buffer
        len = mg_url_decode(p, (size_t)(s - p), dst, dst_len, 1);

        // Redirect error code from -1 to -2 (destination buffer too small).
        if (len == -1) {
          len = -2;
        }
        break;
      }
    }
  }

  return len;
}

int mg_get_var(const struct mg_connection *conn, const char *name,
               char *dst, size_t dst_len) {
  int len = get_var(conn->query_string, conn->query_string == NULL ? 0 :
                    strlen(conn->query_string), name, dst, dst_len);
  if (len < 0) {
    len = get_var(conn->content, conn->content_len, name, dst, dst_len);
  }
  return len;
}

static int get_line_len(const char *buf, int buf_len) {
  int len = 0;
  while (len < buf_len && buf[len] != '\n') len++;
  return buf[len] == '\n' ? len + 1: -1;
}

int mg_parse_multipart(const char *buf, int buf_len,
                       char *var_name, int var_name_len,
                       char *file_name, int file_name_len,
                       const char **data, int *data_len) {
  static const char cd[] = "Content-Disposition: ";
  //struct mg_connection c;
  int hl, bl, n, ll, pos, cdl = sizeof(cd) - 1;
  //char *p;

  if (buf == NULL || buf_len <= 0) return 0;
  if ((hl = get_request_len(buf, buf_len)) <= 0) return 0;
  if (buf[0] != '-' || buf[1] != '-' || buf[2] == '\n') return 0;

  // Get boundary length
  bl = get_line_len(buf, buf_len);

  // Loop through headers, fetch variable name and file name
  var_name[0] = file_name[0] = '\0';
  for (n = bl; (ll = get_line_len(buf + n, hl - n)) > 0; n += ll) {
    if (mg_strncasecmp(cd, buf + n, cdl) == 0) {
      parse_header(buf + n + cdl, ll - (cdl + 2), "name",
                   var_name, var_name_len);
      parse_header(buf + n + cdl, ll - (cdl + 2), "filename",
                   file_name, file_name_len);
    }
  }

  // Scan body, search for terminating boundary
  for (pos = hl; pos + (bl - 2) < buf_len; pos++) {
    if (buf[pos] == '-' && !memcmp(buf, &buf[pos], bl - 2)) {
      if (data_len != NULL) *data_len = (pos - 2) - hl;
      if (data != NULL) *data = buf + hl;
      return pos;
    }
  }

  return 0;
}

const char **mg_get_valid_option_names(void) {
  return static_config_options;
}

void mg_copy_listeners(struct mg_server *s, struct mg_server *to) {
  struct ns_connection *c;
  for (c = ns_next(&s->ns_mgr, NULL); c != NULL; c = ns_next(&s->ns_mgr, c)) {
    struct ns_connection *tmp;
    if ((c->flags & NSF_LISTENING) &&
        (tmp = (struct ns_connection *) NS_MALLOC(sizeof(*tmp))) != NULL) {
      memcpy(tmp, c, sizeof(*tmp));
      tmp->mgr = &to->ns_mgr;
      ns_add_conn(tmp->mgr, tmp);
    }
  }
}

static int get_option_index(const char *name) {
  int i;

  for (i = 0; static_config_options[i * 2] != NULL; i++) {
    if (strcmp(static_config_options[i * 2], name) == 0) {
      return i;
    }
  }
  return -1;
}

static void set_default_option_values(char **opts) {
  const char *value, **all_opts = mg_get_valid_option_names();
  int i;

  for (i = 0; all_opts[i * 2] != NULL; i++) {
    value = all_opts[i * 2 + 1];
    if (opts[i] == NULL && value != NULL) {
      opts[i] = mg_strdup(value);
    }
  }
}

const char *mg_set_option(struct mg_server *server, const char *name,
                          const char *value) {
  int ind = get_option_index(name);
  const char *error_msg = NULL;
  char **v = NULL;

  if (ind < 0) return  "No such option";
  v = &server->config_options[ind];

  // Return success immediately if setting to the same value
  if ((*v == NULL && value == NULL) ||
      (value != NULL && *v != NULL && !strcmp(value, *v))) {
    return NULL;
  }

  if (*v != NULL) {
    NS_FREE(*v);
    *v = NULL;
  }

  if (value == NULL || value[0] == '\0') return NULL;

  *v = mg_strdup(value);
  DBG(("%s [%s]", name, *v));

  if (ind == LISTENING_PORT) {
    struct vec vec;
    while ((value = next_option(value, &vec, NULL)) != NULL) {
      struct ns_connection *c = ns_bind(&server->ns_mgr, vec.ptr,
                                        mg_ev_handler, NULL);
      if (c== NULL) {
        error_msg = "Cannot bind to port";
        break;
      } else {
        char buf[100];
        ns_sock_to_str(c->sock, buf, sizeof(buf), 2);
        NS_FREE(*v);
        *v = mg_strdup(buf);
      }
    }
#ifndef MONGOOSE_NO_FILESYSTEM
  } else if (ind == HEXDUMP_FILE) {
    server->ns_mgr.hexdump_file = *v;
#endif
#if !defined(_WIN32) && !defined(MONGOOSE_NO_USER)
  } else if (ind == RUN_AS_USER) {
    struct passwd *pw;
    if ((pw = getpwnam(value)) == NULL) {
      error_msg = "Unknown user";
    } else if (setgid(pw->pw_gid) != 0) {
      error_msg = "setgid() failed";
    } else if (setuid(pw->pw_uid) != 0) {
      error_msg = "setuid() failed";
    }
#endif
  }

  return error_msg;
}

static void set_ips(struct ns_connection *nc, int is_rem) {
  struct connection *conn = (struct connection *) nc->user_data;
  struct mg_connection *c = &conn->mg_conn;
  char buf[100];

  ns_sock_to_str(nc->sock, buf, sizeof(buf), is_rem ? 7 : 3);
  sscanf(buf, "%47[^:]:%hu",
         is_rem ? c->remote_ip : c->local_ip,
         is_rem ? &c->remote_port : &c->local_port);
  //DBG(("%p %s %s", conn, is_rem ? "rem" : "loc", buf));
}

static void on_accept(struct ns_connection *nc, union socket_address *sa) {
  struct mg_server *server = (struct mg_server *) nc->mgr;
  struct connection *conn;

  if (!check_acl(server->config_options[ACCESS_CONTROL_LIST],
                 ntohl(* (uint32_t *) &sa->sin.sin_addr)) ||
      (conn = (struct connection *) NS_CALLOC(1, sizeof(*conn))) == NULL) {
    nc->flags |= NSF_CLOSE_IMMEDIATELY;
  } else {
    // Circularly link two connection structures
    nc->user_data = conn;
    conn->ns_conn = nc;

    // Initialize the rest of connection attributes
    conn->server = server;
    conn->mg_conn.server_param = nc->mgr->user_data;
    set_ips(nc, 1);
    set_ips(nc, 0);
  }
}

static void process_udp(struct ns_connection *nc) {
  struct iobuf *io = &nc->recv_iobuf;
  struct connection conn;

  memset(&conn, 0, sizeof(conn));
  conn.ns_conn = nc;
  conn.server = (struct mg_server *) nc->mgr;
  conn.request_len = parse_http_message(io->buf, io->len, &conn.mg_conn);
  on_recv_data(&conn);
  //ns_printf(nc, "%s", "HTTP/1.0 200 OK\r\n\r\n");
}

static void mg_ev_handler(struct ns_connection *nc, int ev, void *p) {
  struct connection *conn = (struct connection *) nc->user_data;

  // Send NS event to the handler. Note that call_user won't send an event
  // if conn == NULL. Therefore, repeat this for NS_ACCEPT event as well.
#ifdef MONGOOSE_SEND_NS_EVENTS
  {
    struct connection *conn = (struct connection *) nc->user_data;
    void *param[2] = { nc, p };
    if (conn != NULL) conn->mg_conn.callback_param = param;
    call_user(conn, (enum mg_event) ev);
  }
#endif

  switch (ev) {
    case NS_ACCEPT:
      on_accept(nc, (union socket_address *) p);
#ifdef MONGOOSE_SEND_NS_EVENTS
      {
        struct connection *conn = (struct connection *) nc->user_data;
        void *param[2] = { nc, p };
        if (conn != NULL) conn->mg_conn.callback_param = param;
        call_user(conn, (enum mg_event) ev);
      }
#endif
      break;

    case NS_CONNECT:
      if (nc->user_data != NULL) {
        set_ips(nc, 1);
        set_ips(nc, 0);
      }
      conn->mg_conn.status_code = * (int *) p;
      if (conn->mg_conn.status_code != 0 ||
          (!(nc->flags & MG_PROXY_CONN) &&
           call_user(conn, MG_CONNECT) == MG_FALSE)) {
        nc->flags |= NSF_CLOSE_IMMEDIATELY;
      }
      break;

    case NS_RECV:
      if (conn != NULL) {
        conn->num_bytes_recv += * (int *) p;
      }

      if (nc->flags & NSF_UDP) {
        process_udp(nc);
      } else if (nc->listener != NULL) {
        on_recv_data(conn);
      } else if (nc->flags & MG_PROXY_CONN) {
        if (conn != NULL) {
          ns_forward(nc, conn->ns_conn);
        }
      } else {
        process_response(conn);
      }
      break;

    case NS_SEND:
      break;

    case NS_CLOSE:
      nc->user_data = NULL;
      if (nc->flags & (MG_CGI_CONN | MG_PROXY_CONN)) {
        DBG(("%p %p closing cgi/proxy conn", conn, nc));
        if (conn && conn->ns_conn) {
          conn->ns_conn->flags &= ~NSF_BUFFER_BUT_DONT_SEND;
          conn->ns_conn->flags |= conn->ns_conn->send_iobuf.len > 0 ?
            NSF_FINISHED_SENDING_DATA : NSF_CLOSE_IMMEDIATELY;
          conn->endpoint.nc = NULL;
        }
      } else if (conn != NULL) {
        DBG(("%p %p %d closing", conn, nc, conn->endpoint_type));

        if (conn->endpoint_type == EP_CLIENT && nc->recv_iobuf.len > 0) {
          call_http_client_handler(conn);
        }

        call_user(conn, MG_CLOSE);
        close_local_endpoint(conn);
        conn->ns_conn = NULL;
        NS_FREE(conn);
      }
      break;

    case NS_POLL:
      if (conn != NULL) {
        if (call_user(conn, MG_POLL) == MG_TRUE) {
          if (conn->ns_conn->flags & MG_HEADERS_SENT) {
            write_terminating_chunk(conn);
          }
          close_local_endpoint(conn);
        }

        if (conn->endpoint_type == EP_FILE) {
          transfer_file_data(conn);
        }
      }

      // Expire idle connections
      {
        time_t current_time = * (time_t *) p;

        if (conn != NULL && conn->mg_conn.is_websocket) {
          ping_idle_websocket_connection(conn, current_time);
        }

        if (nc->listener != NULL &&
            nc->last_io_time + MONGOOSE_IDLE_TIMEOUT_SECONDS < current_time) {
          mg_ev_handler(nc, NS_CLOSE, NULL);
          nc->flags |= NSF_CLOSE_IMMEDIATELY;
        }
      }
      break;

    default:
      break;
  }
}

static void iter2(struct ns_connection *nc, int ev, void *param) {
  mg_handler_t func = NULL;
  struct connection *conn = (struct connection *) nc->user_data;
  const char *msg = (const char *) param;
  int n;
  (void) ev;

  //DBG(("%p [%s]", conn, msg));
  if (sscanf(msg, "%p %n", &func, &n) && func != NULL && conn != NULL) {
    conn->mg_conn.callback_param = (void *) (msg + n);
    func(&conn->mg_conn, MG_POLL);
  }
}

void mg_wakeup_server_ex(struct mg_server *server, mg_handler_t cb,
                         const char *fmt, ...) {
  va_list ap;
  char buf[8 * 1024];
  int len;

  // Encode callback (cb) into a buffer
  len = snprintf(buf, sizeof(buf), "%p ", cb);
  va_start(ap, fmt);
  len += vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
  va_end(ap);

  // "len + 1" is to include terminating \0 in the message
  ns_broadcast(&server->ns_mgr, iter2, buf, len + 1);
}

void mg_wakeup_server(struct mg_server *server) {
  ns_broadcast(&server->ns_mgr, NULL, (void *) "", 0);
}

const char *mg_get_option(const struct mg_server *server, const char *name) {
  const char **opts = (const char **) server->config_options;
  int i = get_option_index(name);
  return i == -1 ? NULL : opts[i] == NULL ? "" : opts[i];
}

struct mg_server *mg_create_server(void *server_data, mg_handler_t handler) {
  struct mg_server *server = (struct mg_server *) NS_CALLOC(1, sizeof(*server));
  ns_mgr_init(&server->ns_mgr, server_data);
  set_default_option_values(server->config_options);
  server->event_handler = handler;
  return server;
}

#ifdef WIN32
#	pragma warning(default:4267)
#	pragma warning(default:4244)
#endif
