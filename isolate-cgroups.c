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

#include "isolate.h"

#define LINESIZ 256

static int
mountpoint(const char *path)
{
	dev_t st_dev;
	struct stat st;
	char path0[MAXPATHLEN + 1];

	if (lstat(path, &st) < 0) {
		if (errno != ENOENT)
			myerror(EXIT_FAILURE, errno, "lstat: %s", path);
		return 0;
	}

	st_dev = st.st_dev;

	snprintf(path0, MAXPATHLEN, "%s/..", path);

	if (lstat(path0, &st) < 0)
		myerror(EXIT_FAILURE, errno, "lstat: %s", path0);

	return (st_dev != st.st_dev);
}

static void
make_directory(const char *path)
{
	struct stat st = {};

	if (lstat(path, &st) < 0) {
		if (errno != ENOENT)
			myerror(EXIT_FAILURE, errno, "lstat: %s", path);

		if (mkdir(path, 0700) < 0)
			myerror(EXIT_FAILURE, errno, "mkdir: %s", path);

	} else if ((st.st_mode & S_IFMT) != S_IFDIR) {
		myerror(EXIT_FAILURE, errno, "not directory: %s", path);
	}
}

void
cgroup_create(struct cgroups *cg)
{
	size_t i = 0;
	char path[MAXPATHLEN + 1];

	if (!cg)
		return;

	snprintf(path, MAXPATHLEN, "%s/%s", cg->rootdir, cg->group);
	make_directory(path);

	path[0] = '\0';

	while (cg->controller && cg->controller[i]) {
		char *dirname = cg->dirname[i];

		if (!dirname)
			dirname = cg->controller[i];

		snprintf(path, MAXPATHLEN, "%s/%s/%s", cg->rootdir, cg->group, dirname);
		make_directory(path);

		if (!mountpoint(path) && mount("cgroup", path, "cgroup", 0, cg->controller[i]) < 0)
			myerror(EXIT_FAILURE, errno, "mount(cgroup,%s): %s", cg->controller[i], path);

		path[0] = '\0';

		snprintf(path, MAXPATHLEN, "%s/%s/%s/%s", cg->rootdir, cg->group, dirname, cg->name);

		if (mkdir(path, 0700) < 0) {
			if (errno != EEXIST)
				myerror(EXIT_FAILURE, errno, "mkdir: %s", path);

			if (rmdir(path) < 0) {
				if (errno == EBUSY)
					myerror(EXIT_FAILURE, 0, "%s: directory already exists, unable to re-create", path);
				myerror(EXIT_FAILURE, errno, "rmdir: %s", path);
			}

			if (mkdir(path, 0700) < 0)
				myerror(EXIT_FAILURE, errno, "mkdir: %s", path);
		}

		path[0] = '\0';

		i++;
	}
}

void
cgroup_destroy(struct cgroups *cg)
{
	size_t i = 0;
	char path[MAXPATHLEN + 1];

	if (!cg)
		return;

	while (cg->controller && cg->controller[i]) {
		char *dirname = cg->dirname[i];

		if (!dirname)
			dirname = cg->controller[i];

		snprintf(path, MAXPATHLEN, "%s/%s/%s/%s", cg->rootdir, cg->group, dirname, cg->name);

		if (rmdir(path) < 0 && errno != ENOENT)
			errmsg("rmdir: %s", path);

		path[0] = '\0';

		snprintf(path, MAXPATHLEN, "%s/%s/%s", cg->rootdir, cg->group, dirname);

		if (!umount(path) && rmdir(path) < 0 && errno != EBUSY && errno != ENOENT)
			errmsg("rmdir: %s", path);

		path[0] = '\0';

		cg->controller[i] = xfree(cg->controller[i]);
		cg->dirname[i] = xfree(cg->dirname[i]);

		i++;
	}

	cg->controller = xfree(cg->controller);
	cg->dirname = xfree(cg->dirname);
}

void
cgroup_add(struct cgroups *cg, pid_t pid)
{
	size_t i = 0;
	char path[MAXPATHLEN + 1];

	if (!cg)
		return;

	while (cg->controller && cg->controller[i]) {
		int fd;
		char *dirname = cg->dirname[i];

		if (!dirname)
			dirname = cg->controller[i];

		snprintf(path, MAXPATHLEN, "%s/%s/%s/%s/tasks", cg->rootdir, cg->group, dirname, cg->name);

		if ((fd = open(path, O_WRONLY | O_NOFOLLOW | O_TRUNC | O_CREAT | O_CLOEXEC, 0666)) < 0)
			myerror(EXIT_FAILURE, errno, "open: %s", path);

		if (dprintf(fd, "%d", pid) <= 0)
			myerror(EXIT_FAILURE, errno, "dprintf(pid=%d): %s", pid, path);

		close(fd);
		path[0] = '\0';

		i++;
	}
}

static void
cgroup_state(struct cgroups *cg, const char *state)
{
	int fd;
	char path[MAXPATHLEN + 1];

	if (!cg)
		return;

	snprintf(path, MAXPATHLEN, "%s/%s/%s/%s/freezer.state", cg->rootdir, cg->group, CGROUP_FREEZER, cg->name);

	if ((fd = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC)) < 0)
		myerror(EXIT_FAILURE, errno, "open: %s", path);

	if (dprintf(fd, state) <= 0)
		myerror(EXIT_FAILURE, errno, "dprintf(%s): %s", state, path);

	while (1) {
		char buf[LINESIZ];
		ssize_t i;

		if (lseek(fd, 0, SEEK_SET) < 0)
			myerror(EXIT_FAILURE, errno, "lseek: %s", path);

		i = TEMP_FAILURE_RETRY(read(fd, buf, sizeof(buf) - 1));

		if (i <= 0)
			myerror(EXIT_FAILURE, errno, "dprintf: %s", path);
		buf[i] = '\0';

		if (i > 0 && buf[i - 1] == '\n')
			buf[i - 1] = '\0';

		if (!strcmp(state, buf) || !strcmp("THAWED", buf))
			break;

		usleep(500);
	}

	close(fd);
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
	FILE *fd;
	char path[MAXPATHLEN + 1];
	size_t procs = 0;

	if (!cg)
		return 0;

	snprintf(path, MAXPATHLEN, "%s/%s/%s/%s/tasks", cg->rootdir, cg->group, CGROUP_FREEZER, cg->name);

	errno = 0;
	if (!(fd = fopen(path, "r"))) {
		if (errno == ENOENT)
			return 0;
		myerror(EXIT_FAILURE, errno, "fopen: %s", path);
	}

	while (!feof(fd)) {
		pid_t pid;

		errno = 0;
		if (fscanf(fd, "%u\n", &pid) != 1) {
			if (errno)
				myerror(EXIT_FAILURE, errno, "unable to read pid: %s", path);
			break;
		}

		if (kill(pid, signum) < 0)
			myerror(EXIT_FAILURE, errno, "Could not send signal %d to pid %d", signum, pid);

		procs += 1;
	}

	fclose(fd);

	return procs;
}

void
cgroup_controller(struct cgroups *cg, const char *controller, const char *dirname)
{
	size_t i;

	i = 0;
	while (cg->controller && cg->controller[i]) {
		if (!strcmp(cg->controller[i], controller))
			return;
		i++;
	}

	cg->controller = xrealloc(cg->controller, (i + 2), sizeof(char *));

	cg->controller[i] = xstrdup(controller);
	cg->controller[i + 1] = NULL;

	cg->dirname = xrealloc(cg->dirname, (i + 2), sizeof(char *));
	cg->dirname[i] = NULL;
	if (dirname)
		cg->dirname[i] = xstrdup(dirname);
	cg->dirname[i + 1] = NULL;
}

void
cgroup_split_controllers(struct cgroups *cg, const char *opts)
{
	char *s, *str, *token, *saveptr;

	s = str = xstrdup(opts);

	while (1) {
		if (!(token = strtok_r(s, ",", &saveptr)))
			break;
		cgroup_controller(cg, token, NULL);
		s = NULL;
	}

	xfree(str);
}
