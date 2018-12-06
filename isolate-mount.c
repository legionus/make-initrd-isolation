#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mntent.h>

#include <dirent.h>
#include <fcntl.h>

#include "isolate.h"

#define MNTBUFSIZ 1024

extern int verbose;

static const char *const mountflag_names[] = {
	"ro", "rw",                     // MS_RDONLY
	"noatime", "atime",             // MS_NOATIME
	"nodev", "dev",                 // MS_NODEV
	"nodiratime", "diratime",       // MS_NODIRATIME
	"noexec", "exec",               // MS_NOEXEC
	"nosuid", "suid",               // MS_NOSUID
	"sync", "async",                // MS_SYNCHRONOUS
	"relatime", "norelatime",       // MS_RELATIME
	"strictatime", "nostrictatime", // MS_STRICTATIME
	"dirsync", "nodirsync",         // MS_DIRSYNC
	"lazytime", "nolazytime",       // MS_LAZYTIME
	"mand", "nomand",               // MS_MANDLOCK
	"silent", "loud",               // ignore
	"defaults", "nodefaults",       // ignore
	"auto", "noauto",               // ignore
	"comment",                      // ignore, systemd uses this in fstab
	"_netdev",                      // ignore
	"loop",                         // ignore
	"rec",                          // MS_REC
	"bind",                         // MS_BIND
	"rbind",                        // MS_BIND | MS_REC
	"move",                         // MS_MOVE
	"remount",                      // MS_REMOUNT
	"shared",                       // MS_SHARED
	"rshared",                      // MS_SHARED | MS_REC
	"slave",                        // MS_SLAVE
	"rslave",                       // MS_SLAVE | MS_REC
	NULL
};

#define E(x) \
	{ x, 0 }, { x, 1 }

static struct {
	const unsigned long id;
	const short invert;
} const mountflag_values[] = {
	E(MS_RDONLY),
	E(MS_NOATIME),
	E(MS_NODEV),
	E(MS_NODIRATIME),
	E(MS_NOEXEC),
	E(MS_NOSUID),
	E(MS_SYNCHRONOUS),
	E(MS_RELATIME),
	E(MS_STRICTATIME),
	E(MS_DIRSYNC),
	E(MS_LAZYTIME),
	E(MS_MANDLOCK),
	E(MS_SILENT),
	E(0),
	E(0),
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ MS_REC, 0 },
	{ MS_BIND, 0 },
	{ MS_BIND | MS_REC, 0 },
	{ MS_MOVE, 0 },
	{ MS_REMOUNT, 0 },
	{ MS_SHARED, 0 },
	{ MS_SHARED | MS_REC, 0 },
	{ MS_SLAVE, 0 },
	{ MS_SLAVE | MS_REC, 0 },
};

#undef E

static struct {
	const unsigned long mount_flag;
	const long int vfs_flag;
} const mountPairs[] = {
	{ MS_MANDLOCK, ST_MANDLOCK },
	{ MS_NOATIME, ST_NOATIME },
	{ MS_NODEV, ST_NODEV },
	{ MS_NODIRATIME, ST_NODIRATIME },
	{ MS_NOEXEC, ST_NOEXEC },
	{ MS_NOSUID, ST_NOSUID },
	{ MS_RDONLY, ST_RDONLY },
	{ MS_RELATIME, ST_RELATIME },
	{ MS_SYNCHRONOUS, ST_SYNCHRONOUS },
};

struct mountflags {
	unsigned long vfs_opts;
	char *data;
	mode_t mkdir;
};

void
free_mntent(struct mntent *ent)
{
	xfree(ent->mnt_fsname);
	xfree(ent->mnt_dir);
	xfree(ent->mnt_type);
	xfree(ent->mnt_opts);
	xfree(ent);
}

static mode_t
str2umask(const char *name, const char *value)
{
	char *p = 0;
	unsigned long n;

	if (!*value)
		myerror(EXIT_FAILURE, 0, "empty value for \"%s\" option", name);

	n = strtoul(value, &p, 8);
	if (!p || *p || n > 0777)
		myerror(EXIT_FAILURE, 0, "invalid value for \"%s\" option: %s", name, value);

	return (mode_t) n;
}

static struct mountflags *
parse_mountopts(const char *opts, struct mountflags *flags)
{
	char *s, *subopts, *value;
	int i;

	flags->vfs_opts = 0;
	flags->data = NULL;

	s = subopts = xstrdup(opts);

	while (*subopts != '\0') {
		value = NULL;

		if ((i = getsubopt(&subopts, (char **) mountflag_names, &value)) < 0) {
			if (!strncasecmp("x-mount.mkdir", value, 13)) {
				flags->mkdir = (!strncasecmp("x-mount.mkdir=", value, 14))
				                   ? str2umask("x-mount.mkdir", value + 14)
				                   : 0755;
				continue;
			}

			if (!strncasecmp("x-", value, 2))
				continue;

			if (flags->data) {
				size_t datalen = strlen(flags->data) + strlen(value) + 1;
				flags->data = xrealloc(flags->data, datalen, 1);
				snprintf(flags->data, datalen, "%s,%s", flags->data, value);
			} else {
				flags->data = xstrdup(value);
			}
			continue;
		}

		if (mountflag_values[i].invert)
			flags->vfs_opts &= ~mountflag_values[i].id;
		else
			flags->vfs_opts |= mountflag_values[i].id;
	}

	xfree(s);
	return flags;
}

struct mntent **
parse_fstab(const char *fstabname)
{
	FILE *fstab;
	struct mntent mt;
	char *buf;

	struct mntent **result = NULL;
	size_t n_ents = 0;

	fstab = setmntent(fstabname, "r");
	if (!fstab)
		myerror(EXIT_FAILURE, errno, "setmntent: %s", fstabname);

	buf = xmalloc(MNTBUFSIZ);

	while (getmntent_r(fstab, &mt, buf, MNTBUFSIZ)) {
		result = xrealloc(result, (n_ents + 1), sizeof(void *));

		result[n_ents] = xcalloc(1, sizeof(struct mntent));
		result[n_ents]->mnt_fsname = xstrdup(mt.mnt_fsname);
		result[n_ents]->mnt_dir = xstrdup(mt.mnt_dir);
		result[n_ents]->mnt_type = xstrdup(mt.mnt_type);
		result[n_ents]->mnt_opts = xstrdup(mt.mnt_opts);
		result[n_ents]->mnt_freq = mt.mnt_freq;
		result[n_ents]->mnt_passno = mt.mnt_passno;

		n_ents++;
	}

	result = xrealloc(result, (n_ents + 1), sizeof(void *));
	result[n_ents] = NULL;

	endmntent(fstab);
	xfree(buf);

	return result;
}

static int
_bindents(const char *source, const char *target)
{
	int rc = -1;
	DIR *d;
	char *spath, *tpath;

	if (!(d = opendir(source))) {
		errmsg("opendir: %s", source);
		return rc;
	}

	spath = tpath = NULL;

	while (1) {
		struct dirent *ent;

		if (!(ent = readdir(d))) {
			if (errno) {
				errmsg("readdir: %s", source);
				break;
			}
			rc = 0;
			break;
		}

		if (!strcmp(".", ent->d_name) || !strcmp("..", ent->d_name))
			continue;

		xasprintf(&spath, "%s/%s", source, ent->d_name);
		xasprintf(&tpath, "%s/%s", target, ent->d_name);

		if (ent->d_type == DT_DIR) {
			if (mkdir(tpath, 0755) < 0) {
				errmsg("mkdir: %s", tpath);
				break;
			}
		} else {
			int fd = creat(tpath, 0644);
			if (fd < 0) {
				errmsg("open: %s", tpath);
				break;
			}
			close(fd);
		}

		if (mount(spath, tpath, "none", MS_BIND | MS_REC, NULL) < 0) {
			errmsg("mount: %s", tpath);
			break;
		}

		spath = xfree(spath);
		tpath = xfree(tpath);
	}

	xfree(spath);
	xfree(tpath);

	if (closedir(d) < 0) {
		errmsg("closedir: %s", source);
		return -1;
	}

	return rc;
}

static void
remount_ro(const char *mpoint)
{
	struct statfs st;

	if (TEMP_FAILURE_RETRY(statfs(mpoint, &st)) < 0)
		myerror(EXIT_FAILURE, errno, "statfs: %s", mpoint);

	if (st.f_flags & ST_RDONLY)
		return;

	unsigned long new_flags = MS_REMOUNT | MS_RDONLY | MS_BIND;

	for (size_t i = 0; i < ARRAY_SIZE(mountPairs); i++) {
		if (st.f_flags & mountPairs[i].vfs_flag)
			new_flags |= mountPairs[i].mount_flag;
	}

	if (mount(mpoint, mpoint, "none", new_flags, 0) < 0)
		myerror(EXIT_FAILURE, errno, "mount(remount,ro): %s", mpoint);
}

void
do_mount(const char *newroot, struct mntent **mounts)
{
	size_t i = 0;

	if (verbose)
		info("changing mountpoints");

	while (mounts && mounts[i]) {
		char *mpoint;
		struct mountflags mflags = { 0 };

		parse_mountopts(mounts[i]->mnt_opts, &mflags);

		if ((strlen(newroot) + strlen(mounts[i]->mnt_dir)) > MAXPATHLEN)
			myerror(EXIT_FAILURE, 0, "mountpoint name too long");

		xasprintf(&mpoint, "%s%s", newroot, mounts[i]->mnt_dir);

		if (mflags.mkdir && mkdir(mpoint, mflags.mkdir) < 0 && errno != EEXIST)
			myerror(EXIT_FAILURE, errno, "mkdir: %s", mpoint);

		if (access(mpoint, F_OK) < 0) {
			if (verbose)
				info("WARNING: mountpoint not found in the isolation: %s", mounts[i]->mnt_dir);
			goto next;
		}

		if (!strncasecmp("_bindents", mounts[i]->mnt_type, 9)) {
			if (verbose > 2)
				info("mount(bind) content into the isolation: %s", mpoint);

			if (mount("tmpfs", mpoint, "tmpfs", mflags.vfs_opts, mflags.data) < 0)
				myerror(EXIT_FAILURE, errno, "mount(_bindents): %s", mpoint);

			if (_bindents(mounts[i]->mnt_fsname, mpoint) < 0)
				myerror(EXIT_FAILURE, 0, "_bindents: %s", mpoint);

			goto next;
		}

		if (!strncasecmp("_umount", mounts[i]->mnt_type, 7)) {
			if (verbose > 2)
				info("umount from the isolation: %s", mpoint);

			if (umount2(mpoint, MNT_DETACH) < 0)
				myerror(EXIT_FAILURE, errno, "umount2: %s", mpoint);

			goto next;
		}

		if (verbose > 2) {
			if (mflags.vfs_opts & MS_BIND)
				info("mount(bind) into the isolation: %s", mpoint);
			else if (mflags.vfs_opts & MS_MOVE)
				info("mount(move) into the isolation: %s", mpoint);
			else
				info("mount into the isolation: %s", mpoint);
		}

		if (mount(mounts[i]->mnt_fsname, mpoint, mounts[i]->mnt_type, mflags.vfs_opts, mflags.data) < 0)
			myerror(EXIT_FAILURE, errno, "mount: %s", mpoint);

		if (mflags.vfs_opts & MS_RDONLY)
			remount_ro(mpoint);
	next:

		xfree(mflags.data);
		xfree(mpoint);

		free_mntent(mounts[i]);
		i++;
	}
}
