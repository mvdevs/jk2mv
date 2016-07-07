#include <stdio.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <mongoose.h>
#include "../server/server.h"
#include "../client/client.h"

#define POLL_MSEC 100

static size_t mgstr2str(char *out, size_t outlen, const struct mg_str *in) {
	size_t cpylen = in->len;
	if (cpylen > outlen - 1) cpylen = outlen - 1;

	memcpy(out, in->p, cpylen);
	out[cpylen] = '\0';

	return cpylen;
}

/*
========================================================
Webserver
========================================================
*/
#define HTTPSRV_STDPORT 18200

static struct {
	std::thread thread;
	std::atomic_bool end_poll_loop;

	struct mg_mgr mgr;
	struct mg_connection *con;
	bool running;

	struct {
		std::mutex mutex;
		std::condition_variable cv_processed;
		bool processed;

		char reqPath[MAX_OSPATH];
		char rootPath[MAX_OSPATH];
		bool allowed;
	} event;
} srv;

static void NET_HTTP_ServerProcessEvent() {
	std::unique_lock<std::mutex> lk(srv.event.mutex);

	if (!srv.event.processed) {
		const char *rootPath = FS_MV_VerifyDownloadPath(srv.event.reqPath);
		if (rootPath) {
			srv.event.allowed = true;
			Q_strncpyz(srv.event.rootPath, rootPath, sizeof(srv.event.rootPath));
		} else {
			srv.event.allowed = false;
		}

		// notify thread about processed event
		srv.event.processed = true;
		lk.unlock();
		srv.event.cv_processed.notify_one();
	}
}

static void NET_HTTP_ServerEvent(struct mg_connection *nc, int ev, void *ev_data) {
	if (ev == MG_EV_HTTP_REQUEST) {
		struct http_message *hm = (struct http_message *)ev_data;

		// wait for free event, set event, wait for result, free event
		std::unique_lock<std::mutex> lk(srv.event.mutex);
		srv.event.processed = false;

		mgstr2str(srv.event.reqPath, sizeof(srv.event.reqPath), &hm->uri);
		memmove(srv.event.reqPath, srv.event.reqPath + 1, strlen(srv.event.reqPath));

		srv.event.cv_processed.wait(lk, [] { return srv.event.processed; });

		if (srv.event.allowed) {
			struct mg_serve_http_opts opts = {
				srv.event.rootPath
			};

			mg_serve_http(nc, hm, opts);
		} else {
			mg_printf(nc, "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\n\r\n"
				"<html><body><h1>403 Forbidden</h1></body></html>");
			nc->flags |= MG_F_SEND_AND_CLOSE;
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
		return 0;

	mg_mgr_init(&srv.mgr, NULL);

	if (port) {
		srv.con = mg_bind(&srv.mgr, va("%i", port), NET_HTTP_ServerEvent);
	} else {
		for (port = HTTPSRV_STDPORT; port <= HTTPSRV_STDPORT + 15; port++) {
			srv.con = mg_bind(&srv.mgr, va("%i", port), NET_HTTP_ServerEvent);
			if (srv.con) break;
		}
	}

	if (srv.con) {
		mg_set_protocol_http_websocket(srv.con);
		
		// reset event
		srv.event.processed = true;

		// start polling thread
		srv.end_poll_loop = false;
		srv.thread = std::thread(NET_HTTP_ServerPollLoop);

		Com_Printf("HTTP Downloads: webserver running on port %i...\n", port);
		srv.running = true;
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

	bool error;
	char err_msg[256];
} cldls[MAX_PARALLEL_DOWNLOADS];

static void NET_HTTP_DownloadProcessEvent() {
	std::lock_guard<std::mutex> lk(m_cldls);

	for (int i = 0; i < ARRAY_LEN(cldls); i++) {
		clientDL_t *cldl = &cldls[i];

		if (cldl->inuse) {
			if (cldl->downloading) {
				if (cldl->total_bytes > 0 && cldl->status_callback) {
					cldl->status_callback(cldl->total_bytes, cldl->downloaded_bytes);
				}
			} else {
				// download ended

				NET_HTTP_StopDownload((dlHandle_t)i);
				cldl->ended_callback((dlHandle_t)i, (qboolean)(!cldl->error), cldl->err_msg);
			}
		}
	}
}

static void NET_HTTP_DownloadRecvData(struct mbuf *io, struct mg_connection *nc, std::unique_lock<std::mutex> *lock) {
	clientDL_t *cldl = (clientDL_t *)nc->user_data;

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
			nc->flags |= MG_F_CLOSE_IMMEDIATELY;
			return;
		}

		cldl->downloaded_bytes += bytesAvailable;
		mbuf_remove(io, bytesAvailable);
	}

	if (cldl->downloaded_bytes == cldl->total_bytes) {
		nc->flags |= MG_F_CLOSE_IMMEDIATELY;
	}
}

static void NET_HTTP_DownloadEvent(struct mg_connection *nc, int ev, void *ev_data) {
	std::unique_lock<std::mutex> lk(m_cldls);
	clientDL_t *cldl = (clientDL_t *)nc->user_data;

	switch (ev) {
	case MG_EV_CONNECT: {
		if (*(int *)ev_data != 0) {
			snprintf(cldl->err_msg, sizeof(cldl->err_msg), "connecting failed: %s", strerror(*(int *)ev_data));
			cldl->error = true;
			nc->flags |= MG_F_CLOSE_IMMEDIATELY;
			return;
		}
		break;
	} case MG_EV_RECV: {
		struct mbuf *io = &nc->recv_mbuf;

		struct http_message msg;
		if (!cldl->total_bytes && mg_parse_http(io->buf, (int)io->len, &msg, 0)) {
			if (msg.resp_code != 200) {
				char tmp[128];

				mgstr2str(tmp, sizeof(tmp), &msg.resp_status_msg);
				snprintf(cldl->err_msg, sizeof(cldl->err_msg), "HTTP Error: %i %s", msg.resp_code, tmp);
				cldl->error = true;
				nc->flags |= MG_F_CLOSE_IMMEDIATELY;
				return;
			}

			if (msg.body.len && io->buf + io->len >= msg.body.p) {
				cldl->total_bytes = msg.body.len;

				mbuf_remove(io, msg.body.p - io->buf);
				NET_HTTP_DownloadRecvData(io, nc, &lk);
			}
		} else {
			NET_HTTP_DownloadRecvData(io, nc, &lk);
		}
		break;
	} case MG_EV_CLOSE: {
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
dlHandle_t NET_HTTP_StartDownload(const char *url, const char *toPath, dl_ended_callback ended_callback, dl_status_callback status_callback, const char *userAgent, const char *referer) {
	std::lock_guard<std::mutex> lk(m_cldls);

	// search for free dl slot
	clientDL_t *cldl = NULL;
	for (int i = 0; i < ARRAY_LEN(cldls); i++) {
		cldl = &cldls[i];

		if (!cldl->inuse) {
			cldl = &cldls[i];
			break;
		}
	}

	if (!cldl) {
		return -1;
	}

	cldl->file = fopen(toPath, "wb");
	if (!cldl->file) {
		Com_Error(ERR_DROP, "could not open file %s for writing.", toPath);
		return -1;
	}

	cldl->inuse = true;
	cldl->downloading = true;
	cldl->error = false;
	cldl->downloaded_bytes = cldl->total_bytes = 0;
	cldl->ended_callback = ended_callback;
	cldl->status_callback = status_callback;

	mg_mgr_init(&cldl->mgr, NULL);

	char headers[512];
	Com_sprintf(headers, sizeof(headers), "User-Agent: %s\r\nReferer: %s\r\n", userAgent, referer);
	cldl->con = mg_connect_http_opt(&cldl->mgr, NET_HTTP_DownloadEvent, {(void *)cldl}, url, headers, NULL);

	cldl->end_poll_loop = false;
	cldl->thread = std::thread(NET_HTTP_DownloadPollLoop, cldl);

	return cldl - cldls;
}

/*
====================
NET_HTTP_StopDownload
====================
*/
void NET_HTTP_StopDownload(dlHandle_t handle) {
	assert(handle >= 0);
	assert(handle < ARRAY_LEN(cldls));

	clientDL_t *cldl = &cldls[handle];
	if (!cldl->inuse) {
		return;
	}

	cldl->inuse = false;
	
	cldl->end_poll_loop = true;
	if (cldl->thread.joinable())
		cldl->thread.join();

	cldl->downloading = false;

	fclose(cldl->file);
	cldl->file = NULL;

	mg_mgr_free(&cldl->mgr);
}

#endif
/*
========================================================
========================================================
*/

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
	for (int i = 0; i < ARRAY_LEN(cldls); i++) {
		NET_HTTP_StopDownload((dlHandle_t)i);
	}
#endif
}
