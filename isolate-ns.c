#include <sys/param.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>

#include "isolate.h"

extern int verbose;

static struct {
	const char *name;
	const char *clone_name;
	const int flag;
} const clone_flags[] = {
	{ "mount", "CLONE_NEWNS", CLONE_NEWNS },
	{ "uts", "CLONE_NEWUTS", CLONE_NEWUTS },
	{ "ipc", "CLONE_NEWIPC", CLONE_NEWIPC },
	{ "net", "CLONE_NEWNET", CLONE_NEWNET },
	{ "pid", "CLONE_NEWPID", CLONE_NEWPID },
	{ "cgroup", "CLONE_NEWCGROUP", CLONE_NEWCGROUP },
	{ "sysvsem", "CLONE_SYSVSEM", CLONE_SYSVSEM },
	{ "filesystem", "CLONE_FS", CLONE_FS },
};

static int
add_flag(int *flags, const char *name)
{
	size_t i, len;

	len = strlen(name);

	if (!len) {
		info("empty namespace");
		return -1;
	}

	if (!strncasecmp("all", name, 3)) {
		for (i = 0; i < ARRAY_SIZE(clone_flags); i++)
			*flags |= clone_flags[i].flag;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(clone_flags); i++) {
		if (!strncasecmp(name, clone_flags[i].name, strlen(clone_flags[i].name))) {
			*flags |= clone_flags[i].flag;
			return 0;
		}
	}

	info("unknown unshare flag: %s", name);
	return 0;
}

int
parse_unshare_flags(int *flags, char *arg)
{
	size_t i = 0;
	char *str, *token, *saveptr;

	for (i = 1, str = arg;; i++, str = NULL) {
		token = strtok_r(str, ",", &saveptr);

		if (!token)
			break;

		if (add_flag(flags, token) < 0)
			return -1;
	}

	return 0;
}

void
unshare_flags(const int flags)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(clone_flags); i++) {
		if (flags & clone_flags[i].flag) {
			if (verbose > 2)
				info("unshare namespace %s (%s)", clone_flags[i].name, clone_flags[i].clone_name);

			if (unshare(clone_flags[i].flag) < 0)
				myerror(EXIT_FAILURE, errno, "unshare(%s)", clone_flags[i].clone_name);
		}
	}
}
