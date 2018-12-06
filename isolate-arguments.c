#include <sys/param.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "isolate.h"

int verbose = 0;
int background = 0;
char pidfile[MAXPATHLEN];
char *configfile = (char *) "/etc/isolate/config.ini";

const char short_opts[] = "vVhbc:p:";
const struct option long_opts[] = {
	{ "pidfile", required_argument, NULL, 'p' },
	{ "help", no_argument, NULL, 'h' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "version", no_argument, NULL, 'V' },
	{ "background", no_argument, NULL, 'b' },
	{ "config", required_argument, NULL, 'c' },
	{ "cgroups-dir", required_argument, NULL, 'C' },
	{ "name", required_argument, NULL, 2 },
	{ "root-dir", required_argument, NULL, 3 },
	{ "hostname", required_argument, NULL, 4 },
	{ "input", required_argument, NULL, 5 },
	{ "output", required_argument, NULL, 6 },
	{ "devices-file", required_argument, NULL, 7 },
	{ "environ-file", required_argument, NULL, 8 },
	{ "seccomp-file", required_argument, NULL, 9 },
	{ "fstab-file", required_argument, NULL, 10 },
	{ "cap-add", required_argument, NULL, 11 },
	{ "cap-drop", required_argument, NULL, 12 },
	{ "uid", required_argument, NULL, 13 },
	{ "gid", required_argument, NULL, 14 },
	{ "unshare", required_argument, NULL, 15 },
	{ "cgroups", required_argument, NULL, 16 },
	{ "nice", required_argument, NULL, 17 },
	{ "no-new-privs", required_argument, NULL, 18 },
	{ "init", required_argument, NULL, 19 },
	{ NULL, 0, NULL, 0 }
};

void __attribute__((noreturn))
usage(int code)
{
	dprintf(STDOUT_FILENO,
	        "Usage: %s [options] [--] (start|stop|status) NAME\n"
	        "\n"
	        "Utility allows to isolate process inside predefined environment.\n"
	        "\n"
	        "Options:\n"
	        " -p, --pidfile=FILE    write pid to FILE\n"
	        " -b, --background      run as a background process\n"
	        " -h, --help            display this help and exit\n"
	        " -v, --verbose         print a message for each action\n"
	        " -V, --version         output version information and exit\n"
	        "\n"
	        "Report bugs to authors.\n"
	        "\n",
	        program_invocation_short_name);
	exit(code);
}

void __attribute__((noreturn))
print_version_and_exit(void)
{
	dprintf(STDOUT_FILENO, "%s version %s\n", program_invocation_short_name, VERSION);
	dprintf(STDOUT_FILENO,
	        "Written by Alexey Gladkov.\n\n"
	        "Copyright (C) 2018  Alexey Gladkov <gladkov.alexey@gmail.com>\n"
	        "This is free software; see the source for copying conditions.  There is NO\n"
	        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
	exit(EXIT_SUCCESS);
}

void
parse_global_arguments(int argc, char **argv, struct container *data)
{
	int c;

	optind = opterr = optopt = 0;

	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != EOF) {
		switch (c) {
			case 'h':
				usage(EXIT_SUCCESS);
				break;
			case 'b':
				background = 1;
				break;
			case 'c':
				configfile = optarg;
				break;
			case 'C':
				set_cgroups_dir(data, optarg);
				break;
			case 'p':
				strncpy(pidfile, optarg, MAXPATHLEN);
				break;
			case 'v':
				verbose++;
				break;
			case 'V':
				print_version_and_exit();
				break;
		}
	}
}

void
parse_section_arguments(int argc, char **argv, struct container *data)
{
	int c, arg;

	optind = opterr = optopt = 0;

	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != EOF) {
		switch (c) {
			case 2:
				set_name(data, optarg);
				break;
			case 3:
				set_root_dir(data, optarg);
				break;
			case 4:
				set_hostname(data, optarg);
				break;
			case 5:
				set_input(data, optarg);
				break;
			case 6:
				set_output(data, optarg);
				break;
			case 7:
				set_devices_file(data, optarg);
				break;
			case 8:
				set_environ_file(data, optarg);
				break;
			case 9:
				set_seccomp_file(data, optarg);
				break;
			case 10:
				set_fstab_file(data, optarg);
				break;
			case 11:
				set_cap_add(data, optarg);
				break;
			case 12:
				set_cap_drop(data, optarg);
				break;
			case 13:
				errno = 0;
				arg = (int) strtol(optarg, NULL, 10);
				if (errno == ERANGE)
					myerror(EXIT_FAILURE, 0, "bad value: %s", optarg);
				set_uid(data, arg);
				break;
			case 14:
				errno = 0;
				arg = (int) strtol(optarg, NULL, 10);
				if (errno == ERANGE)
					myerror(EXIT_FAILURE, 0, "bad value: %s", optarg);
				set_gid(data, arg);
				break;
			case 15:
				set_unshare(data, optarg);
				break;
			case 16:
				set_cgroups(data, optarg);
				break;
			case 17:
				errno = 0;
				arg = (int) strtol(optarg, NULL, 10);
				if (errno == ERANGE)
					myerror(EXIT_FAILURE, 0, "bad value: %s", optarg);
				set_nice(data, arg);
				break;
			case 18:
				set_no_new_privs(data, 1);
				break;
			case 19:
				set_argv(data, optarg);
				break;
			case '?':
				usage(EXIT_FAILURE);
		}
	}
}
