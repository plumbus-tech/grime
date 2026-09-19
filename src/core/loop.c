/* epoll-based loop: fds, timerfd timers, signalfd for signals + child reaping. */
#include "grime/loop.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "grime/log.h"

struct handler {
	grime_fd_cb cb;
	void *ud;
};

struct child {
	pid_t pid;
	grime_child_cb cb;
	void *ud;
	struct child *next;
};

struct grime_loop {
	int epfd;
	int sigfd;
	bool running;
	struct handler *handlers; /* indexed by fd */
	int nhandlers;
	struct child *children;
	grime_signal_cb on_hup;
	void *on_hup_ud;
	sigset_t oldmask;
};

struct grime_timer {
	grime_loop *loop;
	int fd;
	grime_timer_cb cb;
	void *ud;
};

uint64_t grime_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint32_t to_epoll(int io)
{
	return (io & GRIME_IO_READ ? EPOLLIN : 0) | (io & GRIME_IO_WRITE ? EPOLLOUT : 0);
}

static void reap_children(grime_loop *loop)
{
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		for (struct child **pp = &loop->children; *pp; pp = &(*pp)->next) {
			if ((*pp)->pid == pid) {
				struct child *c = *pp;
				*pp = c->next;
				c->cb(loop, pid, status, c->ud);
				free(c);
				break;
			}
		}
	}
}

static void on_signal(grime_loop *loop, int fd, int io, void *ud)
{
	(void)io;
	(void)ud;
	struct signalfd_siginfo si;
	while (read(fd, &si, sizeof si) == sizeof si) {
		switch (si.ssi_signo) {
		case SIGINT:
		case SIGTERM:
			LOG_INFO("signal %d, stopping", si.ssi_signo);
			grime_loop_stop(loop);
			break;
		case SIGHUP:
			if (loop->on_hup)
				loop->on_hup(loop, SIGHUP, loop->on_hup_ud);
			break;
		case SIGCHLD:
			reap_children(loop);
			break;
		}
	}
}

grime_loop *grime_loop_new(void)
{
	grime_loop *loop = calloc(1, sizeof *loop);
	if (!loop)
		return NULL;
	loop->epfd = epoll_create1(EPOLL_CLOEXEC);
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &loop->oldmask);
	loop->sigfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
	if (loop->epfd < 0 || loop->sigfd < 0) {
		LOG_ERR("loop init: %s", strerror(errno));
		grime_loop_free(loop);
		return NULL;
	}
	grime_loop_add_fd(loop, loop->sigfd, GRIME_IO_READ, on_signal, NULL);
	return loop;
}

void grime_loop_free(grime_loop *loop)
{
	if (!loop)
		return;
	while (loop->children) {
		struct child *c = loop->children;
		loop->children = c->next;
		free(c);
	}
	if (loop->sigfd >= 0)
		close(loop->sigfd);
	if (loop->epfd >= 0)
		close(loop->epfd);
	sigprocmask(SIG_SETMASK, &loop->oldmask, NULL);
	free(loop->handlers);
	free(loop);
}

int grime_loop_add_fd(grime_loop *loop, int fd, int io, grime_fd_cb cb, void *ud)
{
	if (fd >= loop->nhandlers) {
		int n = fd + 16;
		struct handler *h = realloc(loop->handlers, n * sizeof *h);
		if (!h)
			return -1;
		memset(h + loop->nhandlers, 0, (n - loop->nhandlers) * sizeof *h);
		loop->handlers = h;
		loop->nhandlers = n;
	}
	struct epoll_event ev = {.events = to_epoll(io), .data.fd = fd};
	if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
		return -1;
	loop->handlers[fd] = (struct handler){cb, ud};
	return 0;
}

int grime_loop_mod_fd(grime_loop *loop, int fd, int io)
{
	struct epoll_event ev = {.events = to_epoll(io), .data.fd = fd};
	return epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev);
}

void grime_loop_del_fd(grime_loop *loop, int fd)
{
	epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL);
	if (fd < loop->nhandlers)
		loop->handlers[fd].cb = NULL;
}

int grime_loop_run(grime_loop *loop)
{
	loop->running = true;
	while (loop->running) {
		struct epoll_event evs[32];
		int n = epoll_wait(loop->epfd, evs, 32, -1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			LOG_ERR("epoll_wait: %s", strerror(errno));
			return -1;
		}
		for (int i = 0; i < n && loop->running; i++) {
			int fd = evs[i].data.fd;
			/* looked up per event: an earlier callback may have removed it */
			if (fd >= loop->nhandlers || !loop->handlers[fd].cb)
				continue;
			int io = (evs[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR) ? GRIME_IO_READ : 0) |
				 (evs[i].events & EPOLLOUT ? GRIME_IO_WRITE : 0);
			loop->handlers[fd].cb(loop, fd, io, loop->handlers[fd].ud);
		}
	}
	return 0;
}

void grime_loop_stop(grime_loop *loop)
{
	loop->running = false;
}

void grime_loop_on_sighup(grime_loop *loop, grime_signal_cb cb, void *ud)
{
	loop->on_hup = cb;
	loop->on_hup_ud = ud;
}

int grime_loop_watch_child(grime_loop *loop, pid_t pid, grime_child_cb cb, void *ud)
{
	struct child *c = malloc(sizeof *c);
	if (!c)
		return -1;
	*c = (struct child){pid, cb, ud, loop->children};
	loop->children = c;
	reap_children(loop); /* it may already be gone */
	return 0;
}

static void on_timer(grime_loop *loop, int fd, int io, void *ud)
{
	(void)io;
	grime_timer *t = ud;
	uint64_t expirations;
	if (read(fd, &expirations, sizeof expirations) != sizeof expirations)
		return;
	t->cb(loop, t->ud);
}

grime_timer *grime_timer_new(grime_loop *loop, grime_timer_cb cb, void *ud)
{
	grime_timer *t = malloc(sizeof *t);
	if (!t)
		return NULL;
	*t = (grime_timer){loop, timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), cb, ud};
	if (t->fd < 0 || grime_loop_add_fd(loop, t->fd, GRIME_IO_READ, on_timer, t) < 0) {
		if (t->fd >= 0)
			close(t->fd);
		free(t);
		return NULL;
	}
	return t;
}

void grime_timer_arm(grime_timer *t, uint64_t ms)
{
	struct itimerspec its = {0};
	its.it_value.tv_sec = ms / 1000;
	its.it_value.tv_nsec = (ms % 1000) * 1000000;
	if (ms == 0)
		its.it_value.tv_nsec = 1; /* all-zero would disarm */
	timerfd_settime(t->fd, 0, &its, NULL);
}

void grime_timer_disarm(grime_timer *t)
{
	struct itimerspec its = {0};
	timerfd_settime(t->fd, 0, &its, NULL);
}

void grime_timer_free(grime_timer *t)
{
	if (!t)
		return;
	grime_loop_del_fd(t->loop, t->fd);
	close(t->fd);
	free(t);
}
