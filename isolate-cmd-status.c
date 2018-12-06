#include <sys/types.h>
#include <sys/param.h>
#include <sys/file.h>

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>

#include "isolate.h"

extern char pidfile[MAXPATHLEN];

int
cmd_status(struct container *data __attribute__((unused)))
{
	FILE *fd;
	pid_t pid;
	int running = 0;
	int rc = EXIT_SUCCESS;

	if (!(fd = fopen(pidfile, "r"))) {
		if (errno == ENOENT)
			goto done;

		errmsg("fopen: %s", pidfile);
		rc = EXIT_FAILURE;
		goto done;
	}

	errno = 0;

	if (!flock(fileno(fd), LOCK_EX | LOCK_NB)) {
		flock(fileno(fd), LOCK_UN);
		goto done;
	}

	if (errno != EWOULDBLOCK) {
		errmsg("flock: %s", pidfile);
		rc = EXIT_FAILURE;
		goto done;
	}

	running = 1;

	if (fscanf(fd, "%d\n", &pid) != 1) {
		info("unable to read pid: %s", pidfile);
		rc = EXIT_FAILURE;
		goto done;
	}

	if (kill(pid, 0) < 0) {
		errmsg("kill");
		rc = EXIT_FAILURE;
	}
done:
	if (fd)
		fclose(fd);

	if (rc == EXIT_SUCCESS) {
		if (!running) {
			info("container is not running");
			rc = EXIT_FAILURE;
		} else {
			info("container is running");
		}
	}

	return rc;
}
