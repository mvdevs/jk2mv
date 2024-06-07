#include <stdio.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <mongoose.h>
#include "q_shared.h"
#include "qcommon.h"
#include <mv_setup.h>

#define POLL_MSEC 100

static size_t mgstr2str(char *out, size_t outlen, const struct mg_str *in) {
	size_t cpylen = in->len;
	if (cpylen > outlen - 1) cpylen = outlen - 1;

	memcpy(out, in->ptr, cpylen);
	out[cpylen] = '\0';

	return cpylen;
}

/*
========================================================
Webserver
========================================================
*/
#define HTTPSRV_STDPORT 18200
#define HTTPSRV_CONN_LIMIT 64 // simultaneous connections limit
#define HTTPSRV_READ_LIMIT 2048	// HTTP request size limit
#define HTTPSRV_TIMEOUT_MS 10000

static struct {
	std::thread thread;
	std::atomic_bool end_poll_loop;

	struct mg_mgr mgr;
	struct mg_connection *con;
	bool running;
	int port;

	struct {
		std::mutex mutex;
		std::condition_variable cv_processed;
		bool processed;

		char reqPath[MAX_OSPATH];
		char filePath[MAX_OSPATH];
		bool allowed;
	} event;

	// connected clients. NA_BAD means slot is not used
	std::mutex m_clients;
	netadr_t clients[MAX_CLIENTS];

	// debug
	bool debug;
	unsigned int poll_delay_ms;
} srv;

static void NET_HTTP_ServerProcessEvent() {
	std::unique_lock<std::mutex> lk(srv.event.mutex);

	if (!srv.event.processed) {
		const char *filePath = FS_MV_VerifyDownloadPath(srv.event.reqPath);
		if (filePath) {
			srv.event.allowed = true;
			Q_strncpyz(srv.event.filePath, filePath, sizeof(srv.event.filePath));
		} else {
			srv.event.allowed = false;
		}

		// notify thread about processed event
		srv.event.processed = true;
		lk.unlock();
		srv.event.cv_processed.notify_one();
	}
}

void NET_HTTP_AllowClient(int clientNum, netadr_t addr) {
	if ( addr.type == NA_IP ) {
		std::lock_guard<std::mutex> lk(srv.m_clients);
		srv.clients[clientNum] = addr;
	}
}

void NET_HTTP_DenyClient(int clientNum) {
	std::lock_guard<std::mutex> lk(srv.m_clients);
	srv.clients[clientNum].type = NA_BAD;
}

static bool NET_HTTP_IsMgAddrAllowed(const struct mg_addr *addr) {
	const uint8_t loopback[4] = {127, 0, 0, 1};
	std::lock_guard<std::mutex> lk(srv.m_clients);

	if (!memcmp(addr->ip, loopback, 4)) {
		return true;
	}

	for (unsigned int i = 0; i < ARRAY_LEN(srv.clients); i++) {
		if (srv.clients[i].type == NA_IP &&
			!memcmp(addr->ip, srv.clients[i].ip, 4)) {
			return true;
		}
	}

	return false;
}

static int NET_HTTP_CountMgConnections(const struct mg_mgr *mgr) {
		int numconns = 0;
		for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
			numconns++;
		}
		return numconns;
}

static void NET_HTTP_ServerEvent(struct mg_connection *nc, int ev, void *ev_data) {
	switch(ev) {
	case MG_EV_ERROR: {
		MG_ERROR(("EV_ERROR: %s", (char *)ev_data));
		break;
	}
	case MG_EV_POLL: {
		if (nc->is_accepted && !nc->is_resp && !nc->is_draining) {
			int64_t last_progress;
			memcpy(&last_progress, nc->data, sizeof(last_progress));
			int64_t now = mg_millis();

			if (now - last_progress > HTTPSRV_TIMEOUT_MS) {
				MG_INFO(("Connection closed: timeout"));
				mg_http_reply(nc, 408, "Connection: close\r\n", "");
				nc->is_draining = 1;
			}
		}

		if (srv.poll_delay_ms > 0) {
			// debug rate limiting
			std::this_thread::sleep_for(std::chrono::milliseconds(srv.poll_delay_ms));
		}
		break;
	}
	case MG_EV_ACCEPT: {
		if (nc->rem.is_ip6) {
			MG_INFO(("Connection dropped: IPv6 not allowed"));
			mg_http_reply(nc, 403, NULL, ""); // 403 - Forbidden
			nc->is_draining = 1;
			return;
		}

		if (NET_HTTP_CountMgConnections(nc->mgr) > HTTPSRV_CONN_LIMIT + 1) {
			MG_INFO(("Connection dropped: Too many connections"));
			mg_http_reply(nc, 503, NULL, ""); // 503 - Service Unavailable
			nc->is_draining = 1;
			return;
		}

		if (!NET_HTTP_IsMgAddrAllowed(&nc->rem) && !srv.debug) {
			MG_INFO(("Connection dropped: IP not connected to game server"));
			mg_http_reply(nc, 403, NULL, ""); // 403 - Forbidden
			nc->is_draining = 1;
			return;
		}

		// store last progress time in nc->data
		int64_t now = mg_millis();
		memcpy(nc->data, &now, sizeof(now));
		break;
	}
	case MG_EV_READ: {
		// MG_EV_READ events are received on socket reads.
		// MG_EV_HTTP_MSG is only received once HTTP request has been
		// read fully .
		if (nc->recv.len > HTTPSRV_READ_LIMIT) {
			MG_ERROR(("Msg too large"));
			nc->is_draining = 1;
		}

		// store last progress time in nc->data
		int64_t now = mg_millis();
		memcpy(nc->data, &now, sizeof(now));
		break;
	}
	case MG_EV_HTTP_MSG: {
		struct mg_http_message *hm = (struct mg_http_message *)ev_data;

		// wait for free event, set event, wait for result, free event
		std::unique_lock<std::mutex> lk(srv.event.mutex);
		srv.event.processed = false;

		mgstr2str(srv.event.reqPath, sizeof(srv.event.reqPath), &hm->uri);
		memmove(srv.event.reqPath, srv.event.reqPath + 1, strlen(srv.event.reqPath));

		srv.event.cv_processed.wait(lk, [] { return srv.event.processed; });

		if (srv.event.allowed) {
			struct mg_http_serve_opts opts = {};
			opts.mime_types = "pk3=application/octet-stream";

			mg_http_serve_file(nc, hm, srv.event.filePath, &opts);
		} else {
			mg_http_reply(nc, 403, NULL, "");
			nc->is_draining = 1;
		}
		break;
	}
	}
}

static void NET_HTTP_ServerPollLoop() {
	for (;;) {
		mg_mgr_poll(&srv.mgr, POLL_MSEC);

		if (srv.end_poll_loop.load()) {
			return;
		}
	}
}

/*
====================
NET_HTTP_StartServer
====================
*/
int NET_HTTP_StartServer(int port) {
	if (srv.running)
		return srv.port;

	srv.debug = Cvar_VariableIntegerValue("mg_debug");
	srv.poll_delay_ms = Cvar_VariableIntegerValue("mg_throttle");

	mg_mgr_init(&srv.mgr);

	if (port) {
		srv.con = mg_http_listen(&srv.mgr, va("0.0.0.0:%i", port), NET_HTTP_ServerEvent, NULL);
	} else {
		for (port = HTTPSRV_STDPORT; port <= HTTPSRV_STDPORT + 15; port++) {
			srv.con = mg_http_listen(&srv.mgr, va("0.0.0.0:%i", port), NET_HTTP_ServerEvent, NULL);
			if (srv.con) break;
		}
	}

	if (srv.con) {
		// reset event
		srv.event.processed = true;

		for (unsigned int i = 0; i < ARRAY_LEN(srv.clients); i++) {
			srv.clients[i].type = NA_BAD;
		}

		// start polling thread
		srv.end_poll_loop = false;
		srv.thread = std::thread(NET_HTTP_ServerPollLoop);

		Com_Printf("HTTP Downloads: webserver running on port %i...\n", port);
		srv.running = true;
		srv.port = port;
		return port;
	} else {
		mg_mgr_free(&srv.mgr);
		Com_Error(ERR_DROP, "HTTP Downloads: webserver startup failed.");
		return 0;
	}
}

/*
====================
NET_HTTP_StopServer
====================
*/
void NET_HTTP_StopServer() {
	if (!srv.running)
		return;

	Com_Printf("HTTP Downloads: shutting down webserver...\n");

	srv.end_poll_loop = true;
	srv.thread.join();

	mg_mgr_free(&srv.mgr);
	srv.running = false;
	srv.port = 0;
}

#ifndef DEDICATED
/*
========================================================
Clientside Downloads
========================================================
*/
#define MAX_PARALLEL_DOWNLOADS 8

static std::mutex m_cldls;
static struct clientDL_t {
	struct mg_mgr mgr;
	struct mg_connection *con;

	bool inuse;
	bool downloading;

	std::thread thread;
	std::atomic_bool end_poll_loop;

	FILE *file;
	size_t total_bytes, downloaded_bytes;
	dl_ended_callback ended_callback;
	dl_status_callback status_callback;

	char url[MAX_STRING_CHARS];

	bool error;
	char err_msg[256];
} cldls[MAX_PARALLEL_DOWNLOADS];

static void NET_HTTP_DownloadProcessEvent() {
	m_cldls.lock();

	for (size_t i = 0; i < ARRAY_LEN(cldls); i++) {
		clientDL_t *cldl = &cldls[i];

		if (cldl->inuse) {
			if (cldl->downloading) {
				if (cldl->total_bytes > 0 && cldl->status_callback) {
					cldl->status_callback(cldl->total_bytes, cldl->downloaded_bytes);
				}
			} else {
				// download ended
				NET_HTTP_StopDownload((dlHandle_t)i);

				m_cldls.unlock();
				cldl->ended_callback((dlHandle_t)i, (qboolean)(!cldl->error), cldl->err_msg);
				m_cldls.lock();
			}
		}
	}

	m_cldls.unlock();
}

static void NET_HTTP_DownloadRecvData(struct mg_iobuf *io, struct mg_connection *nc, std::unique_lock<std::mutex> *lock) {
	clientDL_t *cldl = (clientDL_t *)nc->fn_data;

	size_t bytesAvailable = io->len;
	if (cldl->downloaded_bytes + bytesAvailable > cldl->total_bytes) {
		bytesAvailable = cldl->total_bytes - cldl->downloaded_bytes;
	}

	if (bytesAvailable > 0) {
		size_t wrote = 0;
		size_t total = 0;
		FILE *f = cldl->file;

		// Handle short writes
		// Unlock mutex while writing to disk
		lock->unlock();
		while ((wrote = fwrite(io->buf + total, 1, bytesAvailable - total, f)) > 0) {
			total += wrote;
		}
		lock->lock();

		if (total < bytesAvailable) {
			strcpy(cldl->err_msg, "HTTP Error: 0 bytes written to file\n");
			cldl->error = true;
			nc->is_closing = 1;
			return;
		}

		cldl->downloaded_bytes += bytesAvailable;
		mg_iobuf_del(io, 0, bytesAvailable);
	}

	if (cldl->downloaded_bytes == cldl->total_bytes) {
		nc->is_closing = 1;
	}
}

static void NET_HTTP_DownloadEvent(struct mg_connection *nc, int ev, void *ev_data) {
	clientDL_t *cldl = (clientDL_t *)nc->fn_data;

	// mg_connect() sends MG_EV_OPEN synchronously and this would
	// cause a deadlock on m_cldls if m_cldls was locked here

	switch (ev) {
	case MG_EV_ERROR: {
		std::unique_lock<std::mutex> lk(m_cldls);
		strncpy(cldl->err_msg, (char *)ev_data, sizeof(cldl->err_msg));
		cldl->err_msg[sizeof(cldl->err_msg) - 1] = '\0';
		cldl->error = true;
		break;
	}
	case MG_EV_CONNECT: {
		std::unique_lock<std::mutex> lk(m_cldls);
		struct mg_str host = mg_url_host(cldl->url);
		mg_printf(nc,
			"GET %s HTTP/1.0\r\n"
			"Host: %.*s\r\n"
			"User-Agent: " Q3_VERSION "\r\n"
			"\r\n",
			mg_url_uri(cldl->url), (int) host.len, host.ptr);
		break;
	} case MG_EV_READ: {
		std::unique_lock<std::mutex> lk(m_cldls);
		struct mg_iobuf *io = &nc->recv;
		struct mg_http_message msg;

		if (!cldl->total_bytes && mg_http_parse((char *)io->buf, io->len, &msg)) {
			if (mg_vcmp(&msg.uri, "200")) {
				snprintf(cldl->err_msg, sizeof(cldl->err_msg), "HTTP Error: %.*s %.*s", (int)msg.uri.len, msg.uri.ptr, (int)msg.proto.len, msg.proto.ptr);
				cldl->err_msg[sizeof(cldl->err_msg) - 1] = '\0';
				cldl->error = true;
				nc->is_closing = 1;
				return;
			}

			if (msg.body.len && (char *)io->buf + io->len >= msg.body.ptr) {
				cldl->total_bytes = msg.body.len;

				mg_iobuf_del(io, 0, msg.body.ptr - (char *)io->buf);
				NET_HTTP_DownloadRecvData(io, nc, &lk);
			}
		} else {
			NET_HTTP_DownloadRecvData(io, nc, &lk);
		}
		break;
	} case MG_EV_CLOSE: {
		std::unique_lock<std::mutex> lk(m_cldls);
		cldl->downloading = false;
		break;
	} default:
		break;
	}
}

static void NET_HTTP_DownloadPollLoop(clientDL_t *cldl) {
	for (;;) {
		mg_mgr_poll(&cldl->mgr, POLL_MSEC);

		if (cldl->end_poll_loop.load()) {
			return;
		}
	}
}

/*
====================
NET_HTTP_StartDownload
====================
*/
dlHandle_t NET_HTTP_StartDownload(const char *url, const char *toPath, dl_ended_callback ended_callback, dl_status_callback status_callback) {
	m_cldls.lock(); // Manually handle lock, because we don't return from Com_Error

	// search for free dl slot
	clientDL_t *cldl = NULL;
	for (size_t i = 0; i < ARRAY_LEN(cldls); i++) {
		cldl = &cldls[i];

		if (!cldl->inuse) {
			cldl = &cldls[i];
			break;
		}
	}

	if (!cldl) {
		m_cldls.unlock();
		return -1;
	}

	cldl->file = fopen(toPath, "wb");
	if (!cldl->file) {
		m_cldls.unlock();
		Com_Error(ERR_DROP, "could not open file %s for writing.", toPath);
		return -1;
	}

	cldl->inuse = true;
	cldl->downloading = true;
	cldl->error = false;
	cldl->downloaded_bytes = cldl->total_bytes = 0;
	cldl->ended_callback = ended_callback;
	cldl->status_callback = status_callback;
	Q_strncpyz(cldl->url, url, sizeof(cldl->url));

	mg_mgr_init(&cldl->mgr);

	// mg_http_connect() reads whole response to memory first and then
	// sends MG_EV_HTTP_MSG with whole body. Its advantage is that it
	// can read chunked responses automatically, but we don't need it
	// as we limit ourselves to HTTP/1.0

	// we want to stream data to file with each socket read so use
	// mg_connect() and MG_EV_READ events instead.
	cldl->con = mg_connect(&cldl->mgr, cldl->url, NET_HTTP_DownloadEvent, (void *)cldl);

	cldl->end_poll_loop = false;
	cldl->thread = std::thread(NET_HTTP_DownloadPollLoop, cldl);

	m_cldls.unlock();
	return cldl - cldls;
}

/*
====================
NET_HTTP_StopDownload
====================
*/
void NET_HTTP_StopDownload(dlHandle_t handle) {
	assert(handle >= 0);
	assert(handle < (dlHandle_t)ARRAY_LEN(cldls));

	clientDL_t *cldl = &cldls[handle];
	if (!cldl->inuse) {
		return;
	}

	cldl->inuse = false;
	
	cldl->end_poll_loop = true;
	if (cldl->thread.joinable())
		cldl->thread.join();

	mg_mgr_free(&cldl->mgr);

	cldl->downloading = false;

	fclose(cldl->file);
	cldl->file = NULL;
}

#endif
/*
========================================================
========================================================
*/

/*
====================
NET_HTTP_Init
====================
*/
void NET_HTTP_Init() {
	Com_DPrintf("Mongoose: " MG_VERSION "\n");
	int loglevel = Cvar_VariableIntegerValue("mg_loglevel", qfalse);
	mg_log_set(loglevel);
}

/*
====================
NET_HTTP_ProcessEvents
====================
*/
void NET_HTTP_ProcessEvents() {
	NET_HTTP_ServerProcessEvent();

#ifndef DEDICATED
	NET_HTTP_DownloadProcessEvent();
#endif
}

/*
====================
NET_HTTP_Shutdown
====================
*/
void NET_HTTP_Shutdown() {
	NET_HTTP_StopServer();

#ifndef DEDICATED
	for (size_t i = 0; i < ARRAY_LEN(cldls); i++) {
		NET_HTTP_StopDownload((dlHandle_t)i);
	}
#endif
}
