#include <mongoose.h>
#include "../server/server.h"

#define HTTPSRV_STDPORT 18200
#define POLLS 20

static struct mg_mgr http_mgr;
static struct mg_connection *http_srv;

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
NET_HTTP_Event
====================
*/
static void NET_HTTP_Event(struct mg_connection *nc, int ev, void *ev_data) {
	struct http_message *hm = (struct http_message *)ev_data;

	if (nc->listener == http_srv) {
		if (ev == MG_EV_HTTP_REQUEST) {
			char reqPath[MAX_PATH];
			int cpylen = sizeof(reqPath);
			if (hm->uri.len < cpylen) cpylen = hm->uri.len;
			Q_strncpyz(reqPath, hm->uri.p + 1, cpylen);

			const char *rootPath = FS_MV_VerifyDownloadPath(reqPath);
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
