#include <linux/limits.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

#include "isolate.h"

extern int verbose;

static int
get_open_max(void)
{
	long int i = sysconf(_SC_OPEN_MAX);

	if (i < NR_OPEN)
		i = NR_OPEN;
	if (i > INT_MAX)
		i = INT_MAX;

	return (int) i;
}

int
sanitize_fds(void)
{
	struct stat st;
	int fd, max_fd;

	umask(0);

	for (fd = STDIN_FILENO; fd <= STDERR_FILENO; ++fd) {
		if (fstat(fd, &st) < 0) {
			errmsg("fstat");
			return -1;
		}
	}

	max_fd = get_open_max();

	for (; fd < max_fd; ++fd)
		(void) close(fd);

	errno = 0;
	return 0;
}

void
cloexec_fds(void)
{
	int fd, max_fd = get_open_max();

	/* Set close-on-exec flag on all non-standard descriptors. */
	for (fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
		int flags = fcntl(fd, F_GETFD, 0);

		if (flags < 0)
			continue;

		int newflags = flags | FD_CLOEXEC;

		if (flags != newflags && fcntl(fd, F_SETFD, newflags))
			myerror(EXIT_FAILURE, errno, "fcntl F_SETFD");
	}

	errno = 0;
}

void
reopen_fd(const char *filename, int fileno)
{
	int fd = open(filename, O_RDWR | O_CREAT, 0644);

	if (fd < 0)
		myerror(EXIT_FAILURE, errno, "open: %s", filename);

	if (fd != fileno) {
		if (dup2(fd, fileno) != fileno)
			myerror(EXIT_FAILURE, errno, "dup2(%d, %d)", fd, fileno);
		if (close(fd) < 0)
			myerror(EXIT_FAILURE, errno, "close(%d)", fd);
	}

	if (verbose > 2)
		info("open %s as %d", filename, fileno);
}

int
open_map(char *filename, struct mapfile *f, int quiet)
{
	struct stat sb;

	if ((f->fd = open(filename, O_RDONLY | O_CLOEXEC)) < 0) {
		errmsg("open: %s", filename);
		return -1;
	}

	if (fstat(f->fd, &sb) < 0) {
		errmsg("fstat: %s", filename);
		return -1;
	}

	f->size = (size_t) sb.st_size;

	if (!sb.st_size) {
		close(f->fd);
		f->fd = -1;
		if (!quiet)
			info("file %s is empty", filename);
		return 0;
	}

	if ((f->map = mmap(NULL, f->size, PROT_READ, MAP_PRIVATE, f->fd, 0)) == MAP_FAILED) {
		errmsg("mmap: %s", filename);
		return -1;
	}

	f->filename = filename;
	return 0;
}

void
close_map(struct mapfile *f)
{
	if (!f->filename)
		return;

	if (munmap(f->map, f->size) < 0)
		myerror(EXIT_FAILURE, errno, "munmap: %s", f->filename);

	close(f->fd);
	f->filename = NULL;
}
