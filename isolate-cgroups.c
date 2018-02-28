#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <error.h>

#include "isolate.h"

#define LINESIZ 256

static int
mountpoint(const char *path)
{
	dev_t st_dev;
	struct stat st;
	char *path0 = NULL;

	if (lstat(path, &st) < 0) {
		if (errno != ENOENT)
			error(EXIT_FAILURE, errno, "lstat: %s", path);
		return 0;
	}

	st_dev = st.st_dev;

	xasprintf(&path0, "%s/..", path);

	if (lstat(path0, &st) < 0)
		error(EXIT_FAILURE, errno, "lstat: %s", path0);

	xfree(path0);

	return (st_dev != st.st_dev);
}

static void
make_directory(const char *path)
{
	struct stat st = {};

	if (lstat(path, &st) < 0) {
		if (errno != ENOENT)
			error(EXIT_FAILURE, errno, "lstat: %s", path);

		if (mkdir(path, 0700) < 0)
			error(EXIT_FAILURE, errno, "mkdir: %s", path);

	} else if ((st.st_mode & S_IFMT) != S_IFDIR) {
		error(EXIT_FAILURE, errno, "not directory: %s", path);
	}
}

int
cgroup_create(struct cgroups *cg)
{
	size_t i   = 0;
	char *path = NULL;

	if (!cg->rootdir)
		return 1;

	xasprintf(&path, "%s/%s", cg->rootdir, cg->group);
	make_directory(path);

	path = xfree(path);

	while (cg->controller && cg->controller[i]) {
		xasprintf(&path, "%s/%s/%s", cg->rootdir, cg->group, cg->controller[i]);
		make_directory(path);

		if (!mountpoint(path) && mount("cgroup", path, "cgroup", 0, cg->controller[i]) < 0)
			error(EXIT_FAILURE, errno, "mount(cgroup,%s): %s", cg->controller[i], path);

		path = xfree(path);

		xasprintf(&path, "%s/%s/%s/%s", cg->rootdir, cg->group, cg->controller[i], cg->name);
		if (mkdir(path, 0700) < 0) {
			if (errno != EEXIST)
				error(EXIT_FAILURE, errno, "mkdir: %s", path);

			if (rmdir(path) < 0) {
				if (errno == EBUSY)
					error(EXIT_FAILURE, 0, "%s: directory already exists, unable to re-create", path);
				error(EXIT_FAILURE, errno, "rmdir: %s", path);
			}

			if (mkdir(path, 0700) < 0)
				error(EXIT_FAILURE, errno, "mkdir: %s", path);
		}

		path = xfree(path);

		i++;
	}

	return 0;
}

int
cgroup_destroy(struct cgroups *cg)
{
	size_t i   = 0;
	char *path = NULL;

	if (!cg->rootdir)
		return 1;

	while (cg->controller && cg->controller[i]) {
		xasprintf(&path, "%s/%s/%s/%s", cg->rootdir, cg->group, cg->controller[i], cg->name);

		if (rmdir(path) < 0)
			error(EXIT_SUCCESS, errno, "rmdir: %s", path);

		path = xfree(path);

		xasprintf(&path, "%s/%s/%s", cg->rootdir, cg->group, cg->controller[i]);

		if (!umount(path) && rmdir(path) < 0 && errno != EBUSY)
			error(EXIT_SUCCESS, errno, "rmdir: %s", path);

		path = xfree(path);

		cg->controller[i] = xfree(cg->controller[i]);
		i++;
	}

	cg->controller = xfree(cg->controller);

	return 0;
}

int
cgroup_add(struct cgroups *cg, pid_t pid)
{
	size_t i   = 0;
	char *path = NULL;

	if (!cg->rootdir)
		return 1;

	while (cg->controller && cg->controller[i]) {
		int fd;

		xasprintf(&path, "%s/%s/%s/%s/tasks", cg->rootdir, cg->group, cg->controller[i], cg->name);

		if ((fd = open(path, O_WRONLY | O_NOFOLLOW | O_TRUNC | O_CREAT | O_CLOEXEC, 0666)) < 0)
			error(EXIT_FAILURE, errno, "open: %s", path);

		if (dprintf(fd, "%d", pid) <= 0)
			error(EXIT_FAILURE, errno, "dprintf(pid=%d): %s", pid, path);

		close(fd);
		path = xfree(path);

		i++;
	}

	return 0;
}

static void
cgroup_state(struct cgroups *cg, const char *state)
{
	int fd;
	char *path = NULL;

	xasprintf(&path, "%s/%s/freezer/%s/freezer.state", cg->rootdir, cg->group, cg->name);

	if ((fd = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC)) < 0)
		error(EXIT_FAILURE, errno, "open: %s", path);

	if (dprintf(fd, state) <= 0)
		error(EXIT_FAILURE, errno, "dprintf(%s): %s", state, path);

	while (1) {
		char buf[LINESIZ];
		ssize_t i;

		if (lseek(fd, 0, SEEK_SET) < 0)
			error(EXIT_FAILURE, errno, "lseek: %s", path);

		i = TEMP_FAILURE_RETRY(read(fd, buf, sizeof(buf) - 1));

		if (i <= 0)
			error(EXIT_FAILURE, errno, "dprintf: %s", path);
		buf[i] = '\0';

		if (i > 0 && buf[i - 1] == '\n')
			buf[i - 1] = '\0';

		if (!strcmp(state, buf) || !strcmp("THAWED", buf))
			break;

		usleep(500);
	}

	close(fd);
	xfree(path);
}

void
cgroup_freeze(struct cgroups *cg)
{
	cgroup_state(cg, "FROZEN");
}

void
cgroup_unfreeze(struct cgroups *cg)
{
	cgroup_state(cg, "THAWED");
}

size_t
cgroup_signal(struct cgroups *cg, int signum)
{
	char s[LINESIZ];
	char *a, *nline, *path = NULL;
	size_t procs        = 0;
	struct mapfile pids = {};

	if (!cg->rootdir)
		return 1;

	xasprintf(&path, "%s/%s/freezer/%s/tasks", cg->rootdir, cg->group, cg->name);
	open_map(path, &pids, 1);

	if (!pids.size) {
		close_map(&pids);
		xfree(path);
		return 0;
	}

	a = pids.map;
	while (a && a[0]) {
		pid_t pid;

		nline = strchr(a, '\n');

		if (!nline)
			nline = a + strlen(a);

		if ((nline - a) > LINESIZ)
			error(EXIT_FAILURE, 0, "%s: string too long", path);

		strncpy(s, a, (size_t)(nline - a));
		s[nline - a] = '\0';

		a = nline + 1;

		if (sscanf(s, "%u", &pid) != 1)
			error(EXIT_FAILURE, errno, "unable to read pid: %s", path);

		if (kill(pid, signum) < 0)
			error(EXIT_FAILURE, errno, "Could not send signal %d to pid %d", signum, pid);

		procs += 1;
	}

	close_map(&pids);
	xfree(path);

	return procs;
}

void
cgroup_controller(struct cgroups *cg, const char *controller)
{
	size_t i;

	i = 0;
	while (cg->controller && cg->controller[i]) {
		if (!strcmp(cg->controller[i], controller))
			return;
		i++;
	}

	cg->controller = xrealloc(cg->controller, (i + 2), sizeof(char *));

	cg->controller[i]     = xstrdup(controller);
	cg->controller[i + 1] = NULL;
}

void
cgroup_split_controllers(struct cgroups *cg, const char *opts)
{
	char *s, *str, *token, *saveptr;

	s = str = xstrdup(opts);

	while (1) {
		if (!(token = strtok_r(s, ",", &saveptr)))
			break;
		cgroup_controller(cg, token);
		s = NULL;
	}

	xfree(str);
}
