/* exec: run a command without waiting for it.
 *   {"do": "exec", "cmd": "notify-send hi"}            via /bin/sh -c
 *   {"do": "exec", "argv": ["notify-send", "hi"]}      no shell
 * The child is reaped by the loop (SIGCHLD); a non-zero exit is logged. Under
 * --user it runs as that user, in that user's session environment. */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <json-c/json.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "grime/action.h"
#include "spec.h"
#include "grime/log.h"

extern char **environ;

struct exec_state {
	char **argv; /* NULL-terminated */
};

static void exec_free(void *state)
{
	struct exec_state *s = state;
	for (char **a = s->argv; *a; a++)
		free(*a);
	free(s->argv);
	free(s);
}

static int compile(json_object *spec, void **out, char *err, size_t errlen)
{
	const char *cmd = grime_spec_str(spec, "cmd");
	bool bad;
	json_object *argv = grime_spec_array(spec, "argv", err, errlen, &bad);
	if (bad)
		return -1;
	size_t n = argv ? json_object_array_length(argv) : 0;
	if (!cmd && !n) {
		snprintf(err, errlen, "needs \"cmd\" (string) or \"argv\" (non-empty array)");
		return -1;
	}
	struct exec_state *s = calloc(1, sizeof *s);
	if (!s) {
		snprintf(err, errlen, "out of memory");
		return -1;
	}
	if (cmd) {
		s->argv = calloc(4, sizeof(char *));
		s->argv[0] = strdup("/bin/sh");
		s->argv[1] = strdup("-c");
		s->argv[2] = strdup(cmd);
	} else {
		s->argv = calloc(n + 1, sizeof(char *));
		for (size_t i = 0; i < n; i++) {
			json_object *e = json_object_array_get_idx(argv, i);
			if (!json_object_is_type(e, json_type_string)) {
				snprintf(err, errlen, "\"argv\" must hold strings");
				exec_free(s);
				return -1;
			}
			s->argv[i] = strdup(json_object_get_string(e));
		}
	}
	*out = s;
	return 0;
}

static void on_exit_cb(grime_loop *loop, pid_t pid, int status, void *ud)
{
	(void)loop;
	(void)ud;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		LOG_DEBUG("exec: pid %d done", pid);
	else
		LOG_WARN("exec: pid %d exited with status %d", pid,
			 WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
}

/* argv[0] as execve wants it: found on the child's PATH, not grime's. */
static bool resolve(const char *cmd, char **env, char *out, size_t outlen)
{
	if (strchr(cmd, '/')) {
		snprintf(out, outlen, "%s", cmd);
		return true;
	}
	const char *path = "/usr/local/bin:/usr/bin:/bin";
	for (char **e = env; *e; e++)
		if (!strncmp(*e, "PATH=", 5))
			path = *e + 5;
	while (*path) {
		size_t n = strcspn(path, ":");
		snprintf(out, outlen, "%.*s/%s", (int)n, path, cmd);
		if (n && access(out, X_OK) == 0)
			return true;
		path += n + (path[n] == ':');
	}
	return false;
}

/* posix_spawn cannot change user, so this forks. The child may only make
 * async-signal-safe calls: grime has threads. */
static pid_t spawn_as(grime_runtime *rt, char **argv)
{
	char path[4096];
	if (!resolve(argv[0], rt->child_env, path, sizeof path)) {
		LOG_ERR("exec %s: not found", argv[0]);
		return -1;
	}
	gid_t groups[64];
	int ngroups = sizeof groups / sizeof *groups;
	struct passwd *pw = getpwuid(rt->child_uid);
	if (!pw || getgrouplist(pw->pw_name, rt->child_gid, groups, &ngroups) < 0) {
		groups[0] = rt->child_gid;
		ngroups = 1;
	}
	const char *home = pw ? pw->pw_dir : "/";

	pid_t pid = fork();
	if (pid < 0)
		LOG_ERR("exec %s: %s", argv[0], strerror(errno));
	if (pid != 0)
		return pid;
	sigset_t none;
	sigemptyset(&none);
	sigprocmask(SIG_SETMASK, &none, NULL);
	setsid();
	int null = open("/dev/null", O_RDONLY);
	if (null >= 0)
		dup2(null, 0);
	if (setgroups(ngroups, groups) < 0 || setgid(rt->child_gid) < 0 ||
	    setuid(rt->child_uid) < 0)
		_exit(126);
	if (chdir(home) < 0 && chdir("/") < 0)
		_exit(126);
	execve(path, argv, rt->child_env);
	_exit(127);
}

static void run(grime_runtime *rt, void *state)
{
	struct exec_state *s = state;
	if (rt->dry_run) {
		LOG_INFO("exec (dry run): %s", s->argv[s->argv[1] && !strcmp(s->argv[1], "-c") ? 2 : 0]);
		return;
	}
	if (rt->child_env) {
		pid_t pid = spawn_as(rt, s->argv);
		if (pid < 0)
			return;
		LOG_DEBUG("exec: spawned pid %d as uid %d", pid, rt->child_uid);
		grime_loop_watch_child(rt->loop, pid, on_exit_cb, NULL);
		return;
	}
	posix_spawnattr_t attr;
	posix_spawn_file_actions_t fa;
	sigset_t none, all;
	sigemptyset(&none);
	sigfillset(&all);
	posix_spawnattr_init(&attr);
	/* the loop blocks signals for signalfd; don't let children inherit that */
	posix_spawnattr_setsigmask(&attr, &none);
	posix_spawnattr_setsigdefault(&attr, &all);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSID);
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);

	pid_t pid;
	int rc = posix_spawnp(&pid, s->argv[0], &fa, &attr, s->argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&attr);
	if (rc) {
		LOG_ERR("exec %s: %s", s->argv[0], strerror(rc));
		return;
	}
	LOG_DEBUG("exec: spawned pid %d", pid);
	grime_loop_watch_child(rt->loop, pid, on_exit_cb, NULL);
}

static const grime_action_type exec_action = {"exec", compile, run, exec_free};
GRIME_REGISTER_ACTION(exec_action)
