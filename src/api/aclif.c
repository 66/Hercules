/**
 * This file is part of Hercules.
 * http://herc.ws - http://github.com/HerculesWS/Hercules
 *
 * Copyright (C) 2012-2020 Hercules Dev Team
 * Copyright (C) Athena Dev Teams
 *
 * Hercules is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define HERCULES_CORE

#include "config/core.h" // ANTI_MAYAP_CHEAT, RENEWAL, SECURE_NPCTIMEOUT
#include "api/aclif.h"

#include "common/HPM.h"
#include "common/cbasetypes.h"
#include "common/conf.h"
#include "common/ers.h"
#include "common/grfio.h"
#include "common/memmgr.h"
#include "common/mmo.h" // NEW_CARTS, char_achievements
#include "common/nullpo.h"
#include "common/packets.h"
#include "common/random.h"
#include "common/showmsg.h"
#include "common/socket.h"
#include "common/strlib.h"
#include "common/timer.h"
#include "common/utils.h"
#include "api/apisessiondata.h"
#include "api/handlers.h"
#include "api/httpparser.h"
#include "api/httpsender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static struct aclif_interface aclif_s;
struct aclif_interface *aclif;

static bool aclif_setip(const char *ip)
{
	char ip_str[16];
	nullpo_retr(false, ip);
	aclif->api_ip = sockt->host2ip(ip);
	if (!aclif->api_ip) {
		ShowWarning("Failed to resolve Api server address! (%s)\n", ip);
		return false;
	}

	safestrncpy(aclif->api_ip_str, ip, sizeof(aclif->api_ip_str));
	ShowInfo("Api server IP address : '"CL_WHITE"%s"CL_RESET"' -> '"CL_WHITE"%s"CL_RESET"'.\n", ip, sockt->ip2str(aclif->api_ip, ip_str));
	return true;
}

static bool aclif_setbindip(const char *ip)
{
	nullpo_retr(false, ip);
	aclif->bind_ip = sockt->host2ip(ip);
	if (aclif->bind_ip) {
		char ip_str[16];
		ShowInfo("Api Server Bind IP Address : '"CL_WHITE"%s"CL_RESET"' -> '"CL_WHITE"%s"CL_RESET"'.\n", ip, sockt->ip2str(aclif->bind_ip, ip_str));
		return true;
	}
	ShowWarning("Failed to Resolve Api Server Address! (%s)\n", ip);
	return false;
}

/*==========================================
 * Sets api port to 'port'
 * is run from api.c upon loading api server configuration
 *------------------------------------------*/
static void aclif_setport(uint16 port)
{
	aclif->api_port = port;
}

/*==========================================
 * Main client packet processing function
 *------------------------------------------*/
static int aclif_parse(int fd)
{
	ShowInfo("parse called: %d\n", fd);
	struct api_session_data *sd = sockt->session[fd]->session_data;
	nullpo_ret(sd);
	if (!httpparser->parse(fd))
	{
		ShowError("http parser error: %d\n", fd);
		sockt->eof(fd);
		sockt->close(fd);
		return 0;
	}
	if (sockt->session[fd] == NULL || sockt->session[fd]->flag.eof != 0) {
		aclif->terminate_connection(fd);
		return 0;
	}
	if (sd->parser.nread > MAX_REQUEST_SIZE) {
		ShowError("http request too big %d: %u\n", fd, sd->parser.nread);
		sockt->eof(fd);
		sockt->close(fd);
		return 0;
	}
	if (sd->flag.message_complete == 1) {
		aclif->parse_request(fd, sd);
		return 0;
	}

	return 0;
}

static int aclif_parse_request(int fd, struct api_session_data *sd)
{
	if (sd->handler == NULL) {
		ShowError("http handler is NULL: %d\n", fd);
		sockt->close(fd);
		return 0;
	}
	if (sd->handler->func == NULL) {
		ShowError("http handler function is NULL: %d\n", fd);
		sockt->close(fd);
		return 0;
	}
	if (!sd->handler->func(fd, sd)) {
		aclif->reportError(fd, sd);
		sockt->close(fd);
		return 0;
	}
	if (sockt->session[fd] == NULL || sockt->session[fd]->flag.eof != 0) {
		aclif->terminate_connection(fd);
		return 0;
	}
	if ((sd->handler->flags & REQ_AUTO_CLOSE) != 0)
		sockt->close(fd);
	return 0;
}

static void aclif_terminate_connection(int fd)
{
	ShowInfo("closed: %d\n", fd);
	sockt->close(fd);
}

static int aclif_connected(int fd)
{
	ShowInfo("connected called: %d\n", fd);

	nullpo_ret(sockt->session[fd]);
	struct api_session_data *sd = NULL;
	CREATE(sd, struct api_session_data, 1);
	sd->fd = fd;
	sd->headers_db = strdb_alloc(DB_OPT_BASE | DB_OPT_RELEASE_BOTH, MAX_HEADER_NAME_SIZE);
	sockt->session[fd]->session_data = sd;
	httpparser->init_parser(fd, sd);
	return 0;
}

static int aclif_session_delete(int fd)
{
	nullpo_ret(sockt->session[fd]);
	struct api_session_data *sd = sockt->session[fd]->session_data;
	nullpo_ret(sd);
	aFree(sd->url);
	sd->url = NULL;
	aFree(sd->temp_header);
	sd->temp_header = NULL;
	db_destroy(sd->headers_db);
	sd->headers_db = NULL;
	aFree(sd->body);
	sd->body = NULL;

	httpparser->delete_parser(fd);
	return 0;
}

static void aclif_load_handlers(void)
{
	for (int i = 0; i < HTTP_MAX_PROTOCOL; i ++) {
		aclif->handlers_db[i] = strdb_alloc(DB_OPT_BASE | DB_OPT_RELEASE_DATA, MAX_URL_SIZE);
	}
#define handler(method, url, func, flags) aclif->add_handler(method, url, handlers->parse_ ## func, flags)
#include "api/urlhandlers.h"
#undef handler
}

static void aclif_add_handler(enum http_method method, const char *url, HttpParseHandler func, int flags)
{
	nullpo_retv(url);
	nullpo_retv(func);
	Assert_retv(method >= 0 && method < HTTP_MAX_PROTOCOL);

	ShowWarning("Add url: %s\n", url);
	struct HttpHandler *handler = aCalloc(1, sizeof(struct HttpHandler));
	handler->method = method;
	handler->func = func;
	handler->flags = flags;

	strdb_put(aclif->handlers_db[method], url, handler);
}

void aclif_set_url(int fd, enum http_method method, const char *url, size_t size)
{
	nullpo_retv(url);
	Assert_retv(method >= 0 && method < HTTP_MAX_PROTOCOL);

	if (size > MAX_URL_SIZE) {
		ShowWarning("Url size too big %d: %lu\n", fd, size);
		sockt->eof(fd);
		return;
	}
	struct api_session_data *sd = sockt->session[fd]->session_data;
	nullpo_retv(sd);

	aFree(sd->url);
	sd->url = aMalloc(size + 1);
	safestrncpy(sd->url, url, size + 1);

	struct HttpHandler *handler = strdb_get(aclif->handlers_db[method], sd->url);
	if (handler == NULL) {
		ShowWarning("Unhandled url %d: %s\n", fd, sd->url);
		sockt->eof(fd);
		return;
	}
	if (handler->func == NULL) {
		ShowError("found NULL handler for url %d: %s\n", fd, url);
		Assert_report(0);
		sockt->eof(fd);
		return;
	}

	sd->flag.url = 1;
	sd->handler = handler;

	ShowWarning("url: %s\n", sd->url);
}

void aclif_set_body(int fd, const char *body, size_t size)
{
	nullpo_retv(body);

	if (size > MAX_BODY_SIZE) {
		ShowWarning("Body size too big %d: %lu\n", fd, size);
		sockt->eof(fd);
		return;
	}
	struct api_session_data *sd = sockt->session[fd]->session_data;
	nullpo_retv(sd);

	aFree(sd->body);
	sd->body = aMalloc(size + 1);
	memcpy(sd->body, body, size);
	sd->body[size] = 0;
	sd->body_size = size;
}

void aclif_set_header_name(int fd, const char *name, size_t size)
{
	nullpo_retv(name);

	if (size > MAX_HEADER_NAME_SIZE) {
		ShowWarning("Header name size too big %d: %lu\n", fd, size);
		sockt->eof(fd);
		return;
	}
	struct api_session_data *sd = sockt->session[fd]->session_data;
	nullpo_retv(sd);

	if (sd->headers_count >= MAX_HEADER_COUNT) {
		ShowWarning("Header count too big %d: %d\n", fd, sd->headers_count);
		sockt->eof(fd);
		return;
	}

	aFree(sd->temp_header);
	sd->temp_header = aStrndup(name, size);
}

void aclif_set_header_value(int fd, const char *value, size_t size)
{
	nullpo_retv(value);

	if (size > MAX_HEADER_VALUE_SIZE) {
		ShowWarning("Header value size too big %d: %lu\n", fd, size);
		sockt->eof(fd);
		return;
	}
	struct api_session_data *sd = sockt->session[fd]->session_data;
	nullpo_retv(sd);
	strdb_put(sd->headers_db, sd->temp_header, aStrndup(value, size));
	sd->temp_header = NULL;
	sd->headers_count ++;
}

void aclif_check_headers(int fd, struct api_session_data *sd)
{
	nullpo_retv(sd);
	Assert_retv(sd->flag.headers_complete == 1);

	const char *size_str = strdb_get(sd->headers_db, "Content-Length");
	if (size_str != NULL) {
		const size_t sz = atoll(size_str);
		if (sz > MAX_BODY_SIZE) {
			ShowError("Body size too big: %d", fd);
			sockt->eof(fd);
			return;
		}
	}
}

void aclif_reportError(int fd, struct api_session_data *sd)
{
}

static int do_init_aclif(bool minimal)
{
	if (minimal)
		return 0;

	sockt->set_defaultparse(aclif->parse);
	sockt->set_default_client_connected(aclif->connected);
	sockt->set_default_delete(aclif->session_delete);
	sockt->validate = false;
	if (sockt->make_listen_bind(aclif->bind_ip, aclif->api_port) == -1) {
		ShowFatalError("Failed to bind to port '"CL_WHITE"%d"CL_RESET"'\n", aclif->api_port);
		exit(EXIT_FAILURE);
	}

	aclif->load_handlers();

	return 0;
}

static void do_final_aclif(void)
{
	for (int i = 0; i < HTTP_MAX_PROTOCOL; i ++) {
		db_destroy(aclif->handlers_db[i]);
		aclif->handlers_db[i] = NULL;
	}
}

void aclif_defaults(void)
{
	aclif = &aclif_s;
	/* vars */
	aclif->bind_ip = INADDR_ANY;
	aclif->api_port = 3000;
	for (int i = 0; i < HTTP_MAX_PROTOCOL; i ++) {
		aclif->handlers_db[i] = NULL;
	}
	/* core */
	aclif->init = do_init_aclif;
	aclif->final = do_final_aclif;
	aclif->setip = aclif_setip;
	aclif->setbindip = aclif_setbindip;
	aclif->setport = aclif_setport;
	aclif->parse = aclif_parse;
	aclif->parse_request = aclif_parse_request;
	aclif->terminate_connection = aclif_terminate_connection;
	aclif->connected = aclif_connected;
	aclif->session_delete = aclif_session_delete;
	aclif->load_handlers = aclif_load_handlers;
	aclif->add_handler = aclif_add_handler;
	aclif->set_url = aclif_set_url;
	aclif->set_body = aclif_set_body;
	aclif->set_header_name = aclif_set_header_name;
	aclif->set_header_value = aclif_set_header_value;
	aclif->check_headers = aclif_check_headers;

	aclif->reportError = aclif_reportError;
}
