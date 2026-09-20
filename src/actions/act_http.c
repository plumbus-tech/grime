/* http: fire an HTTP request without blocking typing (libcurl multi on the loop).
 *   {"do": "http", "method": "POST", "url": "http://.../api/services/light/toggle",
 *    "headers": ["Authorization: Bearer ${HA_TOKEN}"], "body": "{...}", "timeout_ms": 5000}
 * ${VAR} in url/headers/body is taken from the environment when the key fires. */
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expand.h"
#include "grime/action.h"
#include "spec.h"
#include "grime/log.h"

struct http_state {
	char *method, *url, *body;
	char **headers;
	size_t nheaders;
	long timeout_ms;
};

/* one multi handle per process, created on first use */
static struct {
	CURLM *multi;
	grime_loop *loop;
	grime_timer *timer;
} g;

struct request {
	char *url, *body;
	struct curl_slist *headers;
};

static size_t discard(char *p, size_t sz, size_t n, void *ud)
{
	(void)p;
	(void)ud;
	return sz * n;
}

static void check_done(void)
{
	CURLMsg *msg;
	int left;
	while ((msg = curl_multi_info_read(g.multi, &left))) {
		if (msg->msg != CURLMSG_DONE)
			continue;
		CURL *h = msg->easy_handle;
		struct request *r;
		long status = 0;
		curl_easy_getinfo(h, CURLINFO_PRIVATE, (char **)&r);
		curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
		if (msg->data.result != CURLE_OK)
			LOG_WARN("http %s: %s", r->url, curl_easy_strerror(msg->data.result));
		else if (status >= 400)
			LOG_WARN("http %s: HTTP %ld", r->url, status);
		else
			LOG_DEBUG("http %s: HTTP %ld", r->url, status);
		curl_multi_remove_handle(g.multi, h);
		curl_easy_cleanup(h);
		curl_slist_free_all(r->headers);
		free(r->url);
		free(r->body);
		free(r);
	}
}

static void on_socket_ready(grime_loop *loop, int fd, int io, void *ud)
{
	(void)loop;
	(void)ud;
	int flags = (io & GRIME_IO_READ ? CURL_CSELECT_IN : 0) | (io & GRIME_IO_WRITE ? CURL_CSELECT_OUT : 0);
	int running;
	curl_multi_socket_action(g.multi, fd, flags, &running);
	check_done();
}

static void on_curl_timer(grime_loop *loop, void *ud)
{
	(void)loop;
	(void)ud;
	int running;
	curl_multi_socket_action(g.multi, CURL_SOCKET_TIMEOUT, 0, &running);
	check_done();
}

static int socket_cb(CURL *h, curl_socket_t fd, int what, void *ud, void *sockp)
{
	(void)h;
	(void)ud;
	if (what == CURL_POLL_REMOVE) {
		grime_loop_del_fd(g.loop, fd);
		curl_multi_assign(g.multi, fd, NULL);
		return 0;
	}
	int io = (what & CURL_POLL_IN ? GRIME_IO_READ : 0) | (what & CURL_POLL_OUT ? GRIME_IO_WRITE : 0);
	if (!sockp) {
		grime_loop_add_fd(g.loop, fd, io, on_socket_ready, NULL);
		curl_multi_assign(g.multi, fd, (void *)1);
	} else {
		grime_loop_mod_fd(g.loop, fd, io);
	}
	return 0;
}

static int timer_cb(CURLM *m, long ms, void *ud)
{
	(void)m;
	(void)ud;
	if (ms < 0)
		grime_timer_disarm(g.timer);
	else
		grime_timer_arm(g.timer, ms);
	return 0;
}

static int ensure_multi(grime_loop *loop)
{
	if (g.multi)
		return 0;
	curl_global_init(CURL_GLOBAL_DEFAULT);
	g.loop = loop;
	g.timer = grime_timer_new(loop, on_curl_timer, NULL);
	g.multi = curl_multi_init();
	if (!g.multi || !g.timer)
		return -1;
	curl_multi_setopt(g.multi, CURLMOPT_SOCKETFUNCTION, socket_cb);
	curl_multi_setopt(g.multi, CURLMOPT_TIMERFUNCTION, timer_cb);
	return 0;
}

static void http_free(void *state)
{
	struct http_state *s = state;
	for (size_t i = 0; i < s->nheaders; i++)
		free(s->headers[i]);
	free(s->headers);
	free(s->method);
	free(s->url);
	free(s->body);
	free(s);
}

static int compile(json_object *spec, void **out, char *err, size_t errlen)
{
	json_object *v;
	const char *url = grime_spec_str_req(spec, "url", err, errlen);
	if (!url)
		return -1;
	const char *method = grime_spec_str(spec, "method");
	bool bad;
	json_object *headers = grime_spec_array(spec, "headers", err, errlen, &bad);
	if (bad)
		return -1;
	struct http_state *s = calloc(1, sizeof *s);
	if (!s) {
		snprintf(err, errlen, "out of memory");
		return -1;
	}
	s->url = strdup(url);
	s->method = strdup(method ? method : "GET");
	if (json_object_object_get_ex(spec, "body", &v))
		s->body = strdup(json_object_is_type(v, json_type_string)
					 ? json_object_get_string(v)
					 : json_object_to_json_string_ext(v, JSON_C_TO_STRING_PLAIN));
	s->timeout_ms = json_object_object_get_ex(spec, "timeout_ms", &v) ? json_object_get_int(v) : 10000;
	for (size_t i = 0; headers && i < json_object_array_length(headers); i++) {
		json_object *e = json_object_array_get_idx(headers, i);
		if (!json_object_is_type(e, json_type_string)) {
			snprintf(err, errlen, "\"headers\" must hold strings");
			http_free(s);
			return -1;
		}
		if (!s->headers)
			s->headers = calloc(json_object_array_length(headers), sizeof(char *));
		s->headers[s->nheaders++] = strdup(json_object_get_string(e));
	}
	*out = s;
	return 0;
}

static void run(grime_runtime *rt, void *state)
{
	struct http_state *s = state;
	if (rt->dry_run) {
		LOG_INFO("http (dry run): %s %s", s->method, s->url);
		return;
	}
	if (ensure_multi(rt->loop) < 0) {
		LOG_ERR("http: curl init failed");
		return;
	}
	struct request *r = calloc(1, sizeof *r);
	r->url = grime_expand_env(s->url);
	if (s->body)
		r->body = grime_expand_env(s->body);
	for (size_t i = 0; i < s->nheaders; i++) {
		char *h = grime_expand_env(s->headers[i]);
		r->headers = curl_slist_append(r->headers, h);
		free(h);
	}

	CURL *h = curl_easy_init();
	curl_easy_setopt(h, CURLOPT_URL, r->url);
	curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, s->method);
	curl_easy_setopt(h, CURLOPT_HTTPHEADER, r->headers);
	if (r->body)
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, r->body);
	curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, s->timeout_ms);
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, discard);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_PRIVATE, r);
	LOG_DEBUG("http %s %s", s->method, r->url);
	curl_multi_add_handle(g.multi, h);
}

static const grime_action_type http_action = {"http", compile, run, http_free};
GRIME_REGISTER_ACTION(http_action)
