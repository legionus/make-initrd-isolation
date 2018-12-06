#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <grp.h>    // setgroups
#include <libgen.h> // dirname

#include "isolate.h"

extern int verbose;
extern int background;
extern char *configfile;

const char *program_subname;

void
myerror_progname_subname(char **out)
{
	xasprintf(out, "%s: %s: ", program_invocation_short_name, program_subname);
}

void
free_data(struct container *data)
{
	size_t i;

	if (data->mounts) {
		for (i = 0; data->mounts[i]; i++)
			free_mntent(data->mounts[i]);
		data->mounts = xfree(data->mounts);
	}

	if (data->argv) {
		for (i = 0; data->argv[i]; i++)
			xfree(data->argv[i]);
		data->argv = xfree(data->argv);
	}

	if (data->cgroups) {
		for (i = 0; data->cgroups->controller && data->cgroups->controller[i]; i++) {
			xfree(data->cgroups->controller[i]);
			xfree(data->cgroups->dirname[i]);
		}
		data->cgroups->controller = xfree(data->cgroups->controller);
		data->cgroups->dirname = xfree(data->cgroups->dirname);
		data->cgroups->rootdir = xfree(data->cgroups->rootdir);
		data->cgroups->group = xfree(data->cgroups->group);
		data->cgroups = xfree(data->cgroups);
	}

	if (data->caps) {
		cap_free(data->caps);
		data->caps = NULL;
	}

	data->name = xfree(data->name);
	data->root = xfree(data->root);
	data->hostname = xfree(data->hostname);
	data->devfile = xfree(data->devfile);
	data->envfile = xfree(data->envfile);
	data->seccomp = xfree(data->seccomp);
	data->input = xfree(data->input);
	data->output = xfree(data->output);
}

void
kill_container(struct container *data)
{
	int signum = SIGPWR;

	if (verbose > 1) {
		info("killing container by signal=%d", signum);
	} else if (verbose) {
		info("killing container");
	}

	while (cgroup_signal(data->cgroups, 0) > 0) {
		cgroup_freeze(data->cgroups);
		cgroup_signal(data->cgroups, signum);
		cgroup_unfreeze(data->cgroups);
		switch (signum) {
			case SIGPWR:
				signum = SIGTERM;
				if (verbose > 1)
					info("killing container by signal=%d", signum);
				break;
			case SIGTERM:
				signum = SIGKILL;
				if (verbose > 1)
					info("killing container by signal=%d", signum);
				break;
		}
		usleep(500);
	}
}
