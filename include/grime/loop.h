/* loop.h — the single-threaded event loop everything async hangs off.
 * Nothing registered here may block: callbacks do a bit of work and return. */
#ifndef GRIME_LOOP_H
#define GRIME_LOOP_H

#include <stdint.h>
#include <sys/types.h>

typedef struct grime_loop grime_loop;
typedef struct grime_timer grime_timer;

enum { GRIME_IO_READ = 1, GRIME_IO_WRITE = 2 };

typedef void (*grime_fd_cb)(grime_loop *loop, int fd, int io, void *ud);
typedef void (*grime_timer_cb)(grime_loop *loop, void *ud);
typedef void (*grime_child_cb)(grime_loop *loop, pid_t pid, int status, void *ud);
typedef void (*grime_signal_cb)(grime_loop *loop, int sig, void *ud);

grime_loop *grime_loop_new(void);
void grime_loop_free(grime_loop *loop);
int grime_loop_run(grime_loop *loop); /* until grime_loop_stop */
void grime_loop_stop(grime_loop *loop);

int grime_loop_add_fd(grime_loop *loop, int fd, int io, grime_fd_cb cb, void *ud);
int grime_loop_mod_fd(grime_loop *loop, int fd, int io);
void grime_loop_del_fd(grime_loop *loop, int fd);

/* One-shot timers. arm() re-arms; 0 ms fires on the next loop iteration. */
grime_timer *grime_timer_new(grime_loop *loop, grime_timer_cb cb, void *ud);
void grime_timer_arm(grime_timer *t, uint64_t ms);
void grime_timer_disarm(grime_timer *t);
void grime_timer_free(grime_timer *t);

/* SIGINT/SIGTERM stop the loop by default; SIGHUP goes to this handler. */
void grime_loop_on_sighup(grime_loop *loop, grime_signal_cb cb, void *ud);

/* Called once when pid exits (SIGCHLD is handled by the loop). */
int grime_loop_watch_child(grime_loop *loop, pid_t pid, grime_child_cb cb, void *ud);

uint64_t grime_now_ms(void);

#endif
