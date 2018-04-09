#ifndef _CONTAINER_H_
#define _CONTAINER_H_

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
	char **controller;
};

// isolate-env.c
int setenvf(const char *name, const char *fmt, ...);
void load_environ(struct mapfile *envs);

// isolate-mknod.c
void make_devices(const char *rootdir, struct mapfile *devs);

// isolate-fds.c
int open_map(char *filename, struct mapfile *file, int quiet);
void close_map(struct mapfile *file);
void reopen_fd(const char *filename, int fileno);
void sanitize_fds(void);
void cloexec_fds(void);

// isolate-hooks.c
int run_hook(const char *type);

// isolate-ns.c
void unshare_print_flags(const int flags);
int parse_unshare_flags(int *flags, char *arg);

// isolate-netns.c
void setup_network(void);

#include <stdint.h>

// isolate-userns.c
void map_id(const char *filename, const pid_t pid, const uint64_t from, const uint64_t to);
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
int cap_change_flag(cap_t caps, const char *capname, cap_flag_value_t value);
int cap_parse_arg(cap_t *caps, char *arg, cap_flag_value_t value);
void apply_caps(cap_t caps);

// isolate-cgroups.c
void cgroup_create(struct cgroups *cg);
void cgroup_destroy(struct cgroups *cg);
void cgroup_add(struct cgroups *cg, pid_t pid);
void cgroup_controller(struct cgroups *cg, const char *controller);
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

#endif /* _CONTAINER_H_ */
