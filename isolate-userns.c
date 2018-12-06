#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include "isolate.h"

#define PROC_ROOT "/proc"

extern int verbose;

void
map_id(const char *type, const char *filename, const pid_t pid, const unsigned int from, const uint64_t to)
{
	char *file = NULL;
	int fd;

	if (verbose > 1)
		info("remap %s %u to %lu (pid=%d)", type, from, to, pid);

	xasprintf(&file, PROC_ROOT "/%d/%s", pid, filename);

	if ((fd = open(file, O_WRONLY)) < 0)
		myerror(EXIT_FAILURE, errno, "open: %s", file);

	if (dprintf(fd, "%u %lu 1", from, to) < 0)
		myerror(EXIT_FAILURE, 0, "unable to write to %s", file);

	close(fd);
	xfree(file);
}

void
setgroups_control(const pid_t pid, const char *value)
{
	char *file;
	int fd;

	if (verbose > 1)
		info("set setgroups to %s (pid=%d)", value, pid);

	xasprintf(&file, PROC_ROOT "/%d/setgroups", pid);

	if ((fd = open(file, O_WRONLY)) < 0)
		myerror(EXIT_FAILURE, errno, "open: %s", file);

	if (dprintf(fd, "%s", value) < 0)
		myerror(EXIT_FAILURE, 0, "unable to write to %s", file);

	close(fd);
	xfree(file);
}
