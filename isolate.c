#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <grp.h> // setgroups

#include "isolate.h"

int verbose = 0;
char *pidfile = NULL;
const char *program_subname;

struct container {
	char *name;
	char **argv;
	char *root;
	char *hostname;
	char *devfile;
	char *envfile;
	char *input;
	char *output;
	cap_t caps;
	int nice;
	int no_new_privs;
	int unshare_flags;
	uint64_t remap_uid;
	uint64_t remap_gid;
	struct mntent **mounts;
	struct cgroups *cgroups;
};

const char short_opts[]         = "vVhA:b:D:n:U:u:g:c:m:e:p:";
const struct option long_opts[] = {
	{ "name", required_argument, NULL, 'n' },
	{ "cap-add", required_argument, NULL, 'A' },
	{ "cap-drop", required_argument, NULL, 'D' },
	{ "cgroups", required_argument, NULL, 'c' },
	{ "background", no_argument, NULL, 'b' },
	{ "mount", required_argument, NULL, 'm' },
	{ "setenv", required_argument, NULL, 'e' },
	{ "uid", required_argument, NULL, 'u' },
	{ "gid", required_argument, NULL, 'g' },
	{ "pidfile", required_argument, NULL, 'p' },
	{ "unshare", required_argument, NULL, 'U' },
	{ "help", no_argument, NULL, 'h' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "version", no_argument, NULL, 'V' },
	{ "no-new-privs", no_argument, NULL, 1 },
	{ "nice", required_argument, NULL, 2 },
	{ "devices", required_argument, NULL, 3 },
	{ "environ", required_argument, NULL, 4 },
	{ "hostname", required_argument, NULL, 5 },
	{ "cgroup-dir", required_argument, NULL, 6 },
	{ "input", required_argument, NULL, 7 },
	{ "output", required_argument, NULL, 8 },
	{ NULL, 0, NULL, 0 }
};

static void __attribute__((noreturn))
usage(int code)
{
	dprintf(STDOUT_FILENO,
	        "Usage: %s [options] [--] newroot COMMAND [arguments...]\n"
	        "\n"
	        "Utility allows to isolate process inside predefined environment.\n"
	        "\n"
	        "The COMMAND must be accessable within newroot.\n"
	        "\n"
	        "Options:\n"
	        " --no-new-privs        run container with PR_SET_NO_NEW_PRIVS flag\n"
	        " --nice=NUM            change process priority\n"
	        " --environ=FILE        set environment variables listed in the FILE\n"
	        " --devices=FILE        create devices listed in the FILE\n"
	        " --hostname=STR        UTS name (hostname) of the container\n"
	        " --cgroup-dir=DIR      location of cgroup FS\n"
	        " --input=FILE          use FILE as stdin of the container\n"
	        " --output=FILE         use FILE as stdout and stderr of the container\n"
	        " -U, --unshare=STR     unshare everything I know\n"
	        " -A, --cap-add=list    add Linux capabilities\n"
	        " -D, --cap-drop=list   drop Linux capabilities\n"
	        " -c, --cgroups=list    add the container's process pid to the cgroups\n"
	        " -u, --uid=UID         exposes the mapping of user IDs\n"
	        " -g, --gid=GID         exposes the mapping of group IDs\n"
	        " -e, --setenv=FILE     sets new environ from file\n"
	        " -m, --mount=FSTAB     mounts filesystems using FSTAB file\n"
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

static inline void __attribute__((noreturn))
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

static void
my_error_print_progname(void)
{
	fprintf(stderr, "%s: ", program_invocation_short_name);
}

static void
print_program_subname(void)
{
	fprintf(stderr, "%s: %s: ", program_invocation_short_name, program_subname);
}

static void
append_pid(const char *filename, pid_t pid)
{
	int fd;

	if ((fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644)) == -1)
		error(EXIT_FAILURE, errno, "open: %s", filename);

	if (!dprintf(fd, "%d\n", pid))
		error(EXIT_FAILURE, errno, "dprintf: %s", filename);

	fsync(fd);
	close(fd);
}

static void
write_pipe(const int fd, pid_t v, const char *stage)
{
	//error(EXIT_SUCCESS, 0, "stage: %s", stage);
	if (write(fd, &v, sizeof(v)) < 0)
		error(EXIT_FAILURE, errno, "write_pipe(%s)", stage);
}

static pid_t
read_pipe(const int fd, const char *stage)
{
	pid_t x = 0;
	//error(EXIT_SUCCESS, 0, "stage: %s", stage);
	if (read(fd, &x, sizeof(x)) < 0)
		error(EXIT_FAILURE, errno, "read_pipe(%s)", stage);
	return x;
}

static void
free_data(struct container *data)
{
	size_t i;

	for (i = 0; data->mounts && data->mounts[i]; i++)
		free_mntent(data->mounts[i]);

	if (data->mounts)
		xfree(data->mounts);

	if (data->caps)
		cap_free(data->caps);

	xfree(data->name);
}

static void
kill_container(struct container *data)
{
	int signum = SIGPWR;

	while (cgroup_signal(data->cgroups, 0) > 0) {
		cgroup_freeze(data->cgroups);
		cgroup_signal(data->cgroups, signum);
		cgroup_unfreeze(data->cgroups);
		switch (signum) {
			case SIGPWR:
				signum = SIGTERM;
				break;
			case SIGTERM:
				signum = SIGKILL;
				break;
		}
		usleep(500);
	}
}

static int
container_parent(struct container *data, int child)
{
	int i, rc;
	pid_t init_pid, prehook_pid, hook_pid;
	sigset_t mask;
	int fd_ep, fd_signal;
	int ep_timeout = 0;

	program_subname      = "parent";
	error_print_progname = print_program_subname;

	prehook_pid = hook_pid = 0;

	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	sigdelset(&mask, SIGABRT);
	sigdelset(&mask, SIGSEGV);

	if ((fd_signal = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC)) < 0)
		error(EXIT_FAILURE, errno, "signalfd");

	fd_ep = epollin_init();
	epollin_add(fd_ep, fd_signal);

	write_pipe(child, 1, "ready for clients");

	init_pid = -1;
	if (read(child, &init_pid, sizeof(init_pid)) != sizeof(init_pid))
		error(EXIT_FAILURE, errno, "read init pid");

	if (!read_pipe(child, "waiting for client")) {
		data->cgroups = NULL;
		rc            = EXIT_FAILURE;
		goto done;
	}

	// enforce freezer controller
	cgroup_controller(data->cgroups, "freezer");

	// prepare cgroups
	cgroup_create(data->cgroups);
	cgroup_add(data->cgroups, init_pid);

	if (data->remap_uid || data->remap_gid) {
		uid_t real_euid = geteuid();
		gid_t real_egid = getegid();

		/* since Linux 3.19 unprivileged writing of /proc/self/gid_map
		 * has s been disabled unless /proc/self/setgroups is written
		 * first to permanently disable the ability to call setgroups
		 * in that user namespace.
		 */
		setgroups_control(init_pid, "deny");

		map_id("uid_map", init_pid, data->remap_uid, real_euid);
		map_id("gid_map", init_pid, data->remap_gid, real_egid);
	}

	setenvf("CONTAINER_NAME", "%s", data->name);
	setenvf("CONTAINER_PID", "%d", init_pid);

	rc = EXIT_SUCCESS;
	while (1) {
		struct epoll_event ev[42];
		int fdcount;
		ssize_t size;

		errno = 0;
		if ((fdcount = epoll_wait(fd_ep, ev, ARRAY_SIZE(ev), ep_timeout)) < 0) {
			if (errno == EINTR)
				continue;

			error(EXIT_FAILURE, errno, "epoll_wait");
		}

		if (!fdcount) {
			if (!prehook_pid) {
				prehook_pid = run_hook("PRERUN");
				continue;

			} else if (prehook_pid == -2) {
				prehook_pid = -3;
				write_pipe(child, 1, "allow client to start");
				continue;
			}

			if (!init_pid && hook_pid <= 0)
				goto done;

			ep_timeout = 1000;

		} else
			for (i = 0; i < fdcount; i++) {
				if (!(ev[i].events & EPOLLIN)) {
					continue;

				} else if (ev[i].data.fd == fd_signal) {
					pid_t pid;
					struct signalfd_siginfo fdsi;
					int status = 0;

					size = TEMP_FAILURE_RETRY(read(fd_signal, &fdsi, sizeof(struct signalfd_siginfo)));

					if (size != sizeof(struct signalfd_siginfo)) {
						error(EXIT_SUCCESS, 0, "unable to read signal info");
						continue;
					}

					switch (fdsi.ssi_signo) {
						case SIGCHLD:
							if ((pid = waitpid(-1, &status, 0)) < 0)
								error(EXIT_FAILURE, errno, "waitpid");

							if (pid == init_pid) {
								init_pid = 0;
								hook_pid = run_hook("POSTRUN");
								rc       = WEXITSTATUS(status);
							} else if (pid == prehook_pid) {
								prehook_pid = -2;
							} else if (pid == hook_pid) {
								hook_pid = 0;
							}
							break;
						case SIGUSR1:
							hook_pid = run_hook("USR1");
							break;
						case SIGTERM:
							kill_container(data);
							break;
						case SIGINT:
						case SIGHUP:
							break;
					}
				}
			}
	}
done:
	if (fd_ep >= 0) {
		epollin_remove(fd_ep, fd_signal);
		close(fd_ep);
	}

	close(child);

	if (pidfile)
		unlink(pidfile);

	cgroup_destroy(data->cgroups);
	free_data(data);

	return rc;
}

static int
conatainer_child(struct container *data, int parent)
{
	struct mapfile envs = {};
	struct mapfile devs = {};

	program_subname      = "child";
	error_print_progname = print_program_subname;

	if (data->input)
		reopen_fd(data->input, STDIN_FILENO);

	if (data->output) {
		reopen_fd(data->output, STDOUT_FILENO);
		reopen_fd(data->output, STDERR_FILENO);
	}

	if (data->unshare_flags & CLONE_NEWNS) {
		if (mount("/", "/", "none", MS_PRIVATE | MS_REC, NULL) < 0 && errno != EINVAL)
			error(EXIT_FAILURE, errno, "mount(MS_PRIVATE): %s", data->root);

		if (data->mounts) {
			do_mount(data->root, data->mounts);
			free(data->mounts);
		}
	}

	if (data->unshare_flags & CLONE_NEWUSER) {
		if (unshare(CLONE_NEWUSER) < 0)
			error(EXIT_FAILURE, errno, "unshare(CLONE_NEWUSER)");

		if (verbose)
			unshare_print_flags(CLONE_NEWUSER);
	}

	if (data->unshare_flags & CLONE_NEWNET)
		setup_network();

	if (data->hostname && sethostname(data->hostname, strlen(data->hostname)) < 0)
		error(EXIT_FAILURE, errno, "sethostname");

	if (data->devfile && open_map(data->devfile, &devs, 0) < 0) {
		write_pipe(parent, 0, "NOT ready to run");
		return EXIT_FAILURE;
	}

	if (data->envfile && open_map(data->envfile, &envs, 0) < 0) {
		write_pipe(parent, 0, "NOT ready to run");
		return EXIT_FAILURE;
	}

	write_pipe(parent, 1, "ready to run");

	if (!read_pipe(parent, "waiting for permission to start"))
		return EXIT_FAILURE;

	if (chroot(data->root) < 0)
		error(EXIT_FAILURE, errno, "chroot");

	if (chdir("/") < 0)
		error(EXIT_FAILURE, errno, "chdir");

	if (setsid() < 0)
		error(EXIT_FAILURE, errno, "setsid");

	if (nice(data->nice) < 0)
		error(EXIT_FAILURE, errno, "nice: %d", data->nice);

	if (data->devfile)
		make_devices(&devs);

	if (data->caps) {
		if (verbose) {
			char *caps = cap_to_text(data->caps, NULL);
			dprintf(STDOUT_FILENO, "New capabilities: %s\n", caps);
			cap_free(caps);
		}
		apply_caps(data->caps);
	}

	if (prctl(PR_SET_KEEPCAPS, 1) < 0)
		error(EXIT_FAILURE, errno, "prctl(PR_SET_KEEPCAPS)");

	if (data->no_new_privs && prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
		error(EXIT_FAILURE, errno, "prctl(PR_SET_NO_NEW_PRIVS)");

	if (verbose)
		dprintf(STDOUT_FILENO, "welcome to chroot: %s\n", data->argv[0]);

	clearenv();

	if (data->envfile)
		load_environ(&envs);

	cloexec_fds();

	execvp(data->argv[0], data->argv);
	error(EXIT_FAILURE, errno, "execvp");

	return EXIT_FAILURE;
}

static int
fork_child(struct container *data, int parent)
{
	char c = 'x';
	int pipe[2];
	pid_t pid;

	if (!read_pipe(parent, "waiting for permission to fork"))
		return EXIT_FAILURE;

	if (pipe2(pipe, O_DIRECT) < 0)
		error(EXIT_FAILURE, errno, "pipe2");

	if ((pid = fork()) < 0)
		error(EXIT_FAILURE, errno, "fork");

	if (pid > 0) {
		if (write(parent, &pid, sizeof(pid)) != sizeof(pid))
			error(EXIT_FAILURE, errno, "transfer pid");

		if (write(pipe[1], &c, sizeof(c)) < 0)
			error(EXIT_FAILURE, errno, "write");
		close(pipe[0]);
		close(pipe[1]);

		free_data(data);

		return EXIT_SUCCESS;
	}

	if (read(pipe[0], &c, sizeof(c)) < 0)
		error(EXIT_FAILURE, errno, "read");
	close(pipe[0]);
	close(pipe[1]);

	return conatainer_child(data, parent);
}

int
main(int argc, char **argv)
{
	int c;
	int background = 0;
	pid_t pid;
	struct container data = {};
	struct cgroups cg     = {};

	int sv[2];

	data.cgroups       = &cg;
	data.unshare_flags = CLONE_FS;

	cg.group   = (char *) "isolate";
	cg.rootdir = (char *) "/sys/fs/cgroup";

	error_print_progname = my_error_print_progname;

	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != EOF) {
		switch (c) {
			case 'h':
				usage(EXIT_SUCCESS);
				break;
			case 1:
				data.no_new_privs = 1;
				break;
			case 2:
				errno     = 0;
				data.nice = (int) strtol(optarg, NULL, 10);
				if (errno == ERANGE)
					error(EXIT_FAILURE, 0, "bad value: %s", optarg);
				break;
			case 3:
				data.devfile = optarg;
				break;
			case 4:
				data.envfile = optarg;
				break;
			case 5:
				data.unshare_flags |= CLONE_NEWUTS;
				data.hostname = optarg;
				break;
			case 6:
				cg.rootdir = optarg;
				break;
			case 7:
				data.input = optarg;
				break;
			case 8:
				data.output = optarg;
				break;
			case 'n':
				data.name = xfree(data.name);
				data.name = xstrdup(optarg);
				break;
			case 'A':
				if (cap_parse_arg(&data.caps, optarg, CAP_SET) < 0)
					return EXIT_FAILURE;
				break;
			case 'D':
				if (cap_parse_arg(&data.caps, optarg, CAP_CLEAR) < 0)
					return EXIT_FAILURE;
				break;
			case 'c':
				if (strlen(optarg) > 0)
					cgroup_split_controllers(&cg, optarg);
				break;
			case 'b':
				background = 1;
				break;
			case 'p':
				pidfile = optarg;
				break;
			case 'u':
				data.unshare_flags |= CLONE_NEWUSER;
				data.remap_uid = strtoul(optarg, NULL, 10);
				break;
			case 'g':
				data.unshare_flags |= CLONE_NEWUSER;
				data.remap_gid = strtoul(optarg, NULL, 10);
				break;
			case 'U':
				parse_unshare_flags(&data.unshare_flags, optarg);
				break;
			case 'm':
				data.unshare_flags |= CLONE_NEWNS;
				data.mounts = parse_fstab(optarg);
				break;
			case 'v':
				verbose = 1;
				break;
			case 'V':
				print_version_and_exit();
				break;
			case '?':
				usage(EXIT_FAILURE);
		}
	}

	if (argc == optind) {
		error(0, 0, "New root directory required");
		usage(EXIT_FAILURE);
	}

	data.root = argv[optind++];
	data.argv = &argv[optind];

	if (argc == optind)
		error(EXIT_FAILURE, 0, "Command required");

	if (chdir("/") < 0)
		error(EXIT_FAILURE, errno, "chdir(/)");

	if (access(data.root, R_OK | X_OK) < 0)
		error(EXIT_FAILURE, errno, "access: %s", data.root);

	sanitize_fds();

	if (background && daemon(1, 1) < 0)
		error(EXIT_FAILURE, errno, "daemon");

	if (setgroups((size_t) 0, NULL) < 0)
		error(EXIT_FAILURE, errno, "setgroups");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0)
		error(EXIT_FAILURE, errno, "socketpair");

	if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
		error(EXIT_FAILURE, errno, "prctl(PR_SET_CHILD_SUBREAPER)");

	if ((pid = fork()) < 0)
		error(EXIT_FAILURE, errno, "fork");

	if (pid > 0) {
		if (pidfile)
			append_pid(pidfile, getpid());

		if (!data.name)
			xasprintf(&data.name, "container-%lu", pid);

		cg.name = data.name;

		return container_parent(&data, sv[0]);
	}

	if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0)
		error(EXIT_FAILURE, errno, "prctl(PR_SET_PDEATHSIG)");

	if (unshare(data.unshare_flags & ~CLONE_NEWUSER) < 0)
		error(EXIT_FAILURE, errno, "unshare");

	if (verbose)
		unshare_print_flags(data.unshare_flags);

	return fork_child(&data, sv[1]);
}
