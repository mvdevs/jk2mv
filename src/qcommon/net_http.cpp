#include <mongoose.h>
#include "../server/server.h"
#include "../client/client.h"

#define HTTPSRV_STDPORT 18200
#define POLLS 20

size_t mgstr2str(char *out, size_t outlen, const struct mg_str *in);

static struct mg_mgr http_mgr;
static struct mg_connection *http_srv, *http_dl;
static size_t dl_bytesWritten, dl_fileSize;
static qboolean dl_abortFlag;

/*
====================
NET_HTTP_Init
====================
*/
void NET_HTTP_Init() {
	Com_Printf("HTTP Engine initialized\n");
	mg_mgr_init(&http_mgr, NULL);
}

/*
====================
NET_HTTP_Shutdown
====================
*/
void NET_HTTP_Shutdown() {
	Com_Printf("HTTP Engine: shutting down...\n");
	mg_mgr_free(&http_mgr);
}

/*
====================
NET_HTTP_Poll
====================
*/
void NET_HTTP_Poll(int msec) {
	mg_mgr_poll(&http_mgr, msec);

	for (int i = 0; i < POLLS; i++) {
		mg_mgr_poll(&http_mgr, 0);
	}
}

/*
====================
NET_HTTP_RecvData
====================
*/
#ifndef DEDICATED
void NET_HTTP_RecvData(struct mbuf *io, struct mg_connection *nc) {
	if (dl_abortFlag) {
		nc->flags |= MG_F_CLOSE_IMMEDIATELY;
		return;
	}

	size_t bytesAvailable = io->len;
	if (dl_bytesWritten + bytesAvailable > dl_fileSize) {
		bytesAvailable = dl_fileSize - dl_bytesWritten;
	}

	CL_ParseHTTPDownload(io->buf, bytesAvailable);
	dl_bytesWritten += bytesAvailable;
	CL_ProgressHTTPDownload(dl_fileSize, dl_bytesWritten);
	mbuf_remove(io, bytesAvailable);

	if (dl_bytesWritten == dl_fileSize) {
		nc->flags |= MG_F_CLOSE_IMMEDIATELY;
	}
}
#endif

/*
====================
NET_HTTP_Event
====================
*/
static void NET_HTTP_Event(struct mg_connection *nc, int ev, void *ev_data) {
	if (http_srv && nc->listener == http_srv) {
		if (ev == MG_EV_HTTP_REQUEST) {
			struct http_message *hm = (struct http_message *)ev_data;

			char reqPath[MAX_OSPATH];
			mgstr2str(reqPath, sizeof(reqPath), &hm->uri);

			const char *rootPath = FS_MV_VerifyDownloadPath(reqPath + 1);
			if (rootPath) {
				struct mg_serve_http_opts opts = {
					rootPath
				};

				mg_serve_http(nc, hm, opts);
			} else {
				mg_printf(nc, "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\n\r\n"
					"<html><body><h1>403 Forbidden</h1></body></html>");
				nc->flags |= MG_F_SEND_AND_CLOSE;
			}
		}
	}
#ifndef DEDICATED
	else if (http_dl && nc == http_dl) {
		switch (ev) {
		case MG_EV_CONNECT: {
			if (*(int *)ev_data != 0) {
				Com_Error(ERR_DROP, "connecting failed: %s\n", strerror(*(int *)ev_data));
				return;
			}
			break;
		} case MG_EV_RECV: {
			struct mbuf *io = &nc->recv_mbuf;

			struct http_message msg;
			if (!dl_fileSize && mg_parse_http(io->buf, (int)io->len, &msg, 0)) {
				if (msg.resp_code != 200) {
					char err_msg[128];

					mgstr2str(err_msg, sizeof(err_msg), &msg.resp_status_msg);
					nc->flags |= MG_F_CLOSE_IMMEDIATELY;
					Com_Error(ERR_DROP, "%i %s\n", msg.resp_code, err_msg);
					return;
				}

				if (msg.body.len && io->buf + io->len >= msg.body.p) {
					dl_fileSize = msg.body.len;

					mbuf_remove(io, msg.body.p - io->buf);
					NET_HTTP_RecvData(io, nc);
				}
			} else {
				NET_HTTP_RecvData(io, nc);
			}
			break;
		} case MG_EV_CLOSE: {
			if (!dl_fileSize || dl_bytesWritten != dl_fileSize ) {
				CL_EndHTTPDownload(qtrue);

				if (!dl_abortFlag)
					Com_Error(ERR_DROP, "connection closed by remote host\n");

				return;
			}

			CL_EndHTTPDownload(qfalse);
			break;
		} default:
			break;
		}
	}
#endif
}

size_t mgstr2str(char *out, size_t outlen, const struct mg_str *in) {
	size_t cpylen = in->len;
	if (cpylen > outlen - 1) cpylen = outlen - 1;

	memcpy(out, in->p, cpylen);
	out[cpylen] = '\0';

	return cpylen;
}

/*
====================
NET_HTTP_StartServer
====================
*/
int NET_HTTP_StartServer(int port) {
	if (http_srv) {
		return 0;
	}

	if (port) {
		http_srv = mg_bind(&http_mgr, va("%i", port), NET_HTTP_Event);
	} else {
		for (port = HTTPSRV_STDPORT; port <= HTTPSRV_STDPORT + 15; port++) {
			http_srv = mg_bind(&http_mgr, va("%i", port), NET_HTTP_Event);
			if (http_srv) break;
		}
	}

	if (http_srv) {
		mg_set_protocol_http_websocket(http_srv);
		Com_Printf("HTTP Downloads: webserver running on port %i...\n", port);

		return port;
	} else {
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
	if (!http_srv) {
		return;
	}

	Com_Printf("HTTP Downloads: shutting down webserver...\n");

	mg_mgr_free(&http_mgr);
	mg_mgr_init(&http_mgr, NULL);
	http_srv = NULL;
}

#ifndef DEDICATED
/*
====================
NET_HTTP_StartDownload
====================
*/
void NET_HTTP_StartDownload(const char *url, const char *userAgent, const char *referer) {
	dl_bytesWritten = dl_fileSize = 0;
	dl_abortFlag = qfalse;

	char headers[1024];
	Com_sprintf(headers, sizeof(headers), "User-Agent: %s\r\nReferer: %s\r\n", userAgent, referer);

	http_dl = mg_connect_http(&http_mgr, NET_HTTP_Event, url, headers, NULL);
}

/*
====================
NET_HTTP_StopDownload
====================
*/
void NET_HTTP_StopDownload() {
	dl_abortFlag = qtrue;
}
#endif
