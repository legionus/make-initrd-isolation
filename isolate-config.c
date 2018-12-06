#include <sys/param.h>
#include <sys/utsname.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sched.h>
#include <errno.h>

#include <iniparser.h>

#include "isolate.h"

extern int verbose;
extern char pidfile[MAXPATHLEN];

static int
is_isolate_section(char *name, const char *searchname)
{
	int loop = 0, quote = 0;

	if (strncmp(name, "isolate", 7)) {
		return 0;
	}

	char *ss = NULL;
	size_t sz = 0;

	while (loop < 2) {
		char *n = name + 7;

		if (!*n || !isspace(*n))
			return 0;

		while (*n && isspace(*n))
			n++;

		if (!loop && !quote && *n == '"') {
			quote++;
			n++;
		}

		ss = n;
		sz = strlen(n);

		while (*(n + 1))
			n++;

		if (!loop && quote && *n == '"') {
			quote--;
			sz--;
		}

		loop++;

		if (!quote)
			break;
	}

	return strlen(searchname) == sz && !strncmp(ss, searchname, sz);
}

static char **
split_argv(char *str)
{
	char *token;
	char seps[] = " \t";
	char **res = NULL;
	size_t n_spaces = 0;

	token = strtok(str, seps);
	if (token == NULL)
		return res;

	while (token) {
		res = xrealloc(res, ++n_spaces, sizeof(char *));
		res[n_spaces - 1] = xstrdup(token);
		token = strtok(NULL, seps);
	}

	res = xrealloc(res, (n_spaces + 1), sizeof(char *));
	res[n_spaces] = 0;

	return res;
}

static char *
str_replace(char *str, const char *rep, const char *with) {
	char *res, *ins, *tmp;
	size_t len_str, len_rep, len_with, len;
	size_t count = 0;

	if (!str)
		return NULL;

	if (!rep || !(len_rep = strlen(rep)) || !(len_str = strlen(str)))
		goto dup;

	ins = str;
	while ((ins = strstr(ins, rep)) != NULL) {
		ins += len_rep;
		count++;
	}

	if (!count)
		goto dup;

	len_with = with ? strlen(with) : 0;

	tmp = res = xcalloc(len_str + (len_with - len_rep) * count + 1, sizeof(char));

	while (count--) {
		ins = strstr(str, rep);
		len = (size_t) (ins - str);

		strncpy(tmp, str, len);
		tmp += len;

		if (len_with > 0) {
			strncpy(tmp, with, len_with);
			tmp += len_with;
		}

		str += len + len_rep;
		len_str -= len + len_rep;
	}

	if (len_str > 0)
		strncpy(tmp, str, len_str);

	return res;
dup:
	return xstrdup(str);
}

void
set_cgroups_dir(struct container *data, char *arg)
{
	data->cgroups->rootdir = xfree(data->cgroups->rootdir);
	if (strlen(arg) > 0)
		data->cgroups->rootdir = xstrdup(arg);
	else
		data->cgroups->rootdir = xstrdup("/sys/fs/cgroup");
}

void
set_cgroups_group(struct container *data, char *arg)
{
	data->cgroups->rootdir = xfree(data->cgroups->rootdir);
	if (strlen(arg) > 0)
		data->cgroups->group = xstrdup(arg);
	else
		data->cgroups->group = xstrdup("isolate");
}

void
set_name(struct container *data, char *arg)
{
	data->name = xfree(data->name);
	if (strlen(arg) > 0) {
		data->name = xstrdup(arg);
		data->cgroups->name = data->name;
	}
}

void
set_root_dir(struct container *data, char *arg)
{
	data->root = xfree(data->root);
	if (strlen(arg) > 0)
		data->root = xstrdup(arg);
}

void
set_hostname(struct container *data, char *arg)
{
	data->hostname = xfree(data->hostname);

	if (strlen(arg) > 0) {
		data->hostname = xstrdup(arg);
		data->unshare_flags |= CLONE_NEWUTS;
	} else {
		data->unshare_flags &= ~CLONE_NEWUTS;
	}
}

void
set_input(struct container *data, char *arg)
{
	data->input = xfree(data->input);
	if (strlen(arg) > 0)
		data->input = xstrdup(arg);
}

void
set_output(struct container *data, char *arg)
{
	data->output = xfree(data->output);
	if (strlen(arg) > 0)
		data->output = xstrdup(arg);
}

void
set_devices_file(struct container *data, char *arg)
{
	data->devfile = xfree(data->devfile);
	if (strlen(arg) > 0) {
		if (access(arg, R_OK) < 0)
			myerror(EXIT_FAILURE, errno, "access: %s", arg);
		data->devfile = xstrdup(arg);
	}
}

void
set_environ_file(struct container *data, char *arg)
{
	data->envfile = xfree(data->envfile);
	if (strlen(arg) > 0) {
		if (access(arg, R_OK) < 0)
			myerror(EXIT_FAILURE, errno, "access: %s", arg);
		data->envfile = xstrdup(arg);
	}
}

void
set_seccomp_file(struct container *data, char *arg)
{
	char *tmp;
	struct utsname buf = { 0 };

	data->seccomp = xfree(data->seccomp);

	if (!strlen(arg))
		return;

	errno = 0;
	if (!access(arg, R_OK)) {
		data->seccomp = xstrdup(arg);
		return;
	}

	if (errno != ENOENT)
		myerror(EXIT_FAILURE, errno, "access: %s", arg);

	errno = 0;
	if (uname(&buf) < 0)
		myerror(EXIT_FAILURE, errno, "uname");

	tmp = str_replace(arg, "$ARCH", buf.machine);
	arg = str_replace(tmp, "$RELEASE", buf.release);

	xfree(tmp);

	errno = 0;
	if (access(arg, R_OK) < 0)
		myerror(EXIT_FAILURE, errno, "access: %s", arg);

	data->seccomp = arg;
}

void
set_fstab_file(struct container *data, char *arg)
{
	int i;

	for (i = 0; data->mounts && data->mounts[i]; i++)
		free_mntent(data->mounts[i]);
	data->mounts = xfree(data->mounts);

	if (strlen(arg) > 0) {
		if (access(arg, R_OK) < 0)
			myerror(EXIT_FAILURE, errno, "access: %s", arg);
		data->mounts = parse_fstab(arg);
		data->unshare_flags |= CLONE_NEWNS;
	} else {
		data->unshare_flags &= ~CLONE_NEWNS;
	}
}

void
set_cap_add(struct container *data, char *arg)
{
	if (!strlen(arg))
		return;
	cap_parse_arg(&data->caps, arg, CAP_SET);
}

void
set_cap_drop(struct container *data, char *arg)
{
	if (!strlen(arg))
		return;
	cap_parse_arg(&data->caps, arg, CAP_CLEAR);
}

void
set_cap_caps(struct container *data, char *arg)
{
	if (!strlen(arg))
		return;
	cap_parse_capsset(&data->caps, arg);
}

void
set_uid(struct container *data, int arg)
{
	if (arg > 0) {
		data->uid = (uid_t) arg;
	} else {
		data->uid = 0;
	}
}

void
set_gid(struct container *data, int arg)
{
	if (arg > 0) {
		data->gid = (gid_t) arg;
	} else {
		data->gid = 0;
	}
}

void
set_unshare(struct container *data, char *arg)
{
	if (!strlen(arg))
		return;
	parse_unshare_flags(&data->unshare_flags, arg);
}

void
set_cgroups(struct container *data, char *arg)
{
	if (!strlen(arg))
		return;
	cgroup_split_controllers(data->cgroups, arg);
}

void
set_nice(struct container *data, int arg)
{
	data->nice = arg;
}

void
set_no_new_privs(struct container *data, int arg)
{
	data->no_new_privs = arg > 0;
}

void
set_argv(struct container *data, char *arg)
{
	int i;

	for (i = 0; data->argv && data->argv[i]; i++)
		xfree(data->argv[i]);
	data->argv = xfree(data->argv);

	if (strlen(arg) > 0)
		data->argv = split_argv(arg);
}

void
read_config(const char *filename, char *section, struct container *data)
{
	char key[1024];
	char *arg;
	char empty[] = "";
	int found = 0;

	snprintf(pidfile, MAXPATHLEN - 1, "/var/run/isolate/isolate-%s.pid", section);

	if (access(filename, R_OK) < 0)
		myerror(EXIT_FAILURE, errno, "access: %s", filename);

	dictionary *config = iniparser_load(filename);
	int n = iniparser_getnsec(config);

	for (int i = 0; i < n; i++) {
		char *name = iniparser_getsecname(config, i);

		if (!strcasecmp(name, "global")) {
			verbose = iniparser_getint(config, "global:verbose", 0);
			set_cgroups_dir(data, iniparser_getstring(config, "global:cgroups-dir", empty));

			arg = iniparser_getstring(config, "global:pid-dir", (char *) "/var/run/isolate");
			snprintf(pidfile, MAXPATHLEN - 1, "%s/isolate-%s.pid", arg, section);

		} else if (is_isolate_section(name, section)) {
			found = 1;

			if (!data->name)
				set_name(data, section);

			snprintf(key, sizeof(key), "%s:root-dir", name);
			set_root_dir(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:hostname", name);
			set_hostname(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:input", name);
			set_input(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:output", name);
			set_output(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:devices-file", name);
			set_devices_file(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:environ-file", name);
			set_environ_file(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:seccomp-file", name);
			set_seccomp_file(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:fstab-file", name);
			set_fstab_file(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:caps", name);
			set_cap_caps(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:uid", name);
			set_uid(data, iniparser_getint(config, (const char *) key, 0));

			snprintf(key, sizeof(key), "%s:gid", name);
			set_gid(data, iniparser_getint(config, (const char *) key, 0));

			snprintf(key, sizeof(key), "%s:unshare", name);
			set_unshare(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:cgroups", name);
			set_cgroups(data, iniparser_getstring(config, (const char *) key, empty));

			snprintf(key, sizeof(key), "%s:nice", name);
			set_nice(data, iniparser_getint(config, (const char *) key, 0));

			snprintf(key, sizeof(key), "%s:no-new-privs", name);
			set_no_new_privs(data, iniparser_getboolean(config, (const char *) key, 0));

			snprintf(key, sizeof(key), "%s:init", name);
			set_argv(data, iniparser_getstring(config, (const char *) key, (char *) "/bin/sh"));
		}
	}

	iniparser_freedict(config);

	if (!found)
		myerror(EXIT_FAILURE, 0, "section `%s' not found in %s", section, filename);
}
