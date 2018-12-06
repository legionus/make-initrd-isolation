#ifndef _CONTAINER_H_
#define _CONTAINER_H_

#include <sys/types.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct mapfile {
	int fd;
	size_t size;
	char *filename;
	char *map;
};

struct cgroups {
	char *rootdir;
	char *group;
	char *name;
	char **dirname;
	char **controller;
};

#include <sys/capability.h>

struct container {
	char *name;
	char **argv;
	char *root;
	char *hostname;
	char *devfile;
	char *envfile;
	char *seccomp;
	char *input;
	char *output;
	cap_t caps;
	int nice;
	int no_new_privs;
	int unshare_flags;
	uid_t uid;
	gid_t gid;
	struct mntent **mounts;
	struct cgroups *cgroups;
};

// isolate-arguments.c
void __attribute__((noreturn)) usage(int code);
void __attribute__((noreturn)) print_version_and_exit(void);
void parse_global_arguments(int argc, char **argv, struct container *data);
void parse_section_arguments(int argc, char **argv, struct container *data);

// isolate-env.c
void load_environ(struct mapfile *envs);

// isolate-mknod.c
void make_devices(const char *rootdir, struct mapfile *devs);

// isolate-fds.c
int open_map(char *filename, struct mapfile *file, int quiet);
void close_map(struct mapfile *file);
void reopen_fd(const char *filename, int fileno);
int sanitize_fds(void);
void cloexec_fds(void);

// isolate-ns.c
int parse_unshare_flags(int *flags, char *arg);
void unshare_flags(const int flags);

// isolate-netns.c
void setup_network(void);

#include <stdio.h>

// isolate-seccomp.c
void load_seccomp(FILE *fd, const char *filename);

#include <stdint.h>

// isolate-userns.c
void map_id(const char *type, const char *filename, const pid_t pid, const unsigned int from, const uint64_t to);
void setgroups_control(const pid_t pid, const char *value);

#include <mntent.h>

// isolate-mount.c
void do_mount(const char *newroot, struct mntent **mounts);
struct mntent **parse_fstab(const char *fstabname);
void free_mntent(struct mntent *ent);

#include <sys/epoll.h>

int epollin_init(void);
void epollin_add(int fd_ep, int fd);
void epollin_remove(int fd_ep, int fd);

#include <sys/capability.h>

// isolate-caps.c
int cap_parse_arg(cap_t *caps, char *arg, cap_flag_value_t value);
int cap_parse_capsset(cap_t *caps, char *arg);
void apply_caps(cap_t caps);

// isolate-cgroups.c
#define CGROUP_FREEZER "freezer0"

void cgroup_create(struct cgroups *cg);
void cgroup_destroy(struct cgroups *cg);
void cgroup_add(struct cgroups *cg, pid_t pid);
void cgroup_controller(struct cgroups *cg, const char *controller, const char *dirname);
void cgroup_split_controllers(struct cgroups *cg, const char *opts);
void cgroup_freeze(struct cgroups *cg);
void cgroup_unfreeze(struct cgroups *cg);
size_t cgroup_signal(struct cgroups *cg, int signum);

// isolate-common.c
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xfree(void *ptr);
void *xrealloc(void *ptr, size_t nmemb, size_t size);
char *xstrdup(const char *s);
int xasprintf(char **ptr, const char *fmt, ...);

void (*myerror_progname)(char **);
void __attribute__((format(printf, 3, 4))) myerror(const int exitnum, const int errnum, const char *fmt, ...);

#define info(...)   myerror(EXIT_SUCCESS, 0, __VA_ARGS__)
#define errmsg(...) myerror(EXIT_SUCCESS, errno, __VA_ARGS__)

// isolate-config.c
void set_cgroups_dir(struct container *data, char *arg);
void set_cgroups_group(struct container *data, char *arg);
void set_name(struct container *data, char *arg);
void set_root_dir(struct container *data, char *arg);
void set_hostname(struct container *data, char *arg);
void set_input(struct container *data, char *arg);
void set_output(struct container *data, char *arg);
void set_devices_file(struct container *data, char *arg);
void set_environ_file(struct container *data, char *arg);
void set_seccomp_file(struct container *data, char *arg);
void set_fstab_file(struct container *data, char *arg);
void set_cap_add(struct container *data, char *arg);
void set_cap_drop(struct container *data, char *arg);
void set_cap_caps(struct container *data, char *arg);
void set_uid(struct container *data, int arg);
void set_gid(struct container *data, int arg);
void set_unshare(struct container *data, char *arg);
void set_cgroups(struct container *data, char *arg);
void set_nice(struct container *data, int arg);
void set_no_new_privs(struct container *data, int arg);
void set_argv(struct container *data, char *arg);

void read_config(const char *filename, char *section, struct container *data);

// isolate-cmd-common.c
void myerror_progname_subname(char **out);
void free_data(struct container *data);
void kill_container(struct container *data);

// isolate-cmd-start.c
int cmd_start(struct container *data);

// isolate-cmd-stop.c
int cmd_stop(struct container *data);

// isolate-cmd-status.c
int cmd_status(struct container *data);

#endif /* _CONTAINER_H_ */
