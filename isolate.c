#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "isolate.h"

extern char *configfile;

const char *program_subname;

int
main(int argc, char **argv)
{
	int rc = EXIT_SUCCESS;

	struct container data = {};
	data.cgroups = xcalloc(1, sizeof(struct cgroups));

	set_cgroups_group(&data, (char *) "");
	set_cgroups_dir(&data, (char *) "");
	set_unshare(&data, (char *) "filesystem");

	// enforce freezer controller
	cgroup_controller(data.cgroups, "freezer", CGROUP_FREEZER);

	parse_global_arguments(argc, argv, &data);

	if ((argc - optind) < 2) {
		free_data(&data);
		info("more arguments required");
		usage(EXIT_FAILURE);
	}

	char *cmd = argv[optind++];
	char *name = argv[optind++];

	read_config(configfile, name, &data);
	parse_section_arguments(argc, argv, &data);

	if (chdir("/") < 0)
		myerror(EXIT_FAILURE, errno, "chdir(/)");

	if (!strcmp(cmd, "start"))
		rc = cmd_start(&data);
	else if (!strcmp(cmd, "stop"))
		rc = cmd_stop(&data);
	else if (!strcmp(cmd, "status"))
		rc = cmd_status(&data);
	else
		info("unknown command `%s'", cmd);

	free_data(&data);

	return rc;
}
