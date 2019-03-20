#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/param.h>
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
#include <syslog.h>
#include <grp.h>    // setgroups
#include <libgen.h> // dirname

#include "isolate.h"

extern int verbose;
extern int background;
extern int use_syslog;
extern char *configfile;
extern char pidfile[MAXPATHLEN];

const char *program_subname;

typedef enum {
	CMD_NONE = 0,
	CMD_FORK_CLIENT,
	CMD_CLIENT_PID,
	CMD_CLIENT_REPARENT,
	CMD_CLIENT_READY,
	CMD_CLIENT_EXEC
} cmd_t;

struct cmd {
	cmd_t type;
	uint64_t datalen;
};

static void
append_pid(pid_t pid)
{
	int fd;

	if ((fd = open(pidfile, O_RDWR | O_CREAT | O_TRUNC, 0644)) == -1)
		myerror(EXIT_FAILURE, errno, "open: %s", pidfile);

	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK) {
			info("container is already running");
			exit(1);
		}
		myerror(EXIT_FAILURE, errno, "flock: %s", pidfile);
	}

	if (!dprintf(fd, "%d\n", pid))
		myerror(EXIT_FAILURE, errno, "dprintf: %s", pidfile);

	fsync(fd);
}

static const char *
print_cmd(struct cmd *hdr)
{
	switch (hdr->type) {
		case CMD_NONE:
			return "CMD_NONE";
		case CMD_FORK_CLIENT:
			return "CMD_FORK_CLIENT";
		case CMD_CLIENT_PID:
			return "CMD_CLIENT_PID";
		case CMD_CLIENT_REPARENT:
			return "CMD_CLIENT_REPARENT";
		case CMD_CLIENT_READY:
			return "CMD_CLIENT_READY";
		case CMD_CLIENT_EXEC:
			return "CMD_CLIENT_EXEC";
	}
	return "UNKNOWN";
}

static int
send_cmd(int fd, cmd_t cmd, void *data, uint64_t len)
{
	struct cmd hdr = { 0 };

	hdr.type = cmd;
	hdr.datalen = len;

	if (verbose > 2)
		info("sending message: %s", print_cmd(&hdr));

	if (TEMP_FAILURE_RETRY(write(fd, &hdr, sizeof(hdr))) < 0) {
		errmsg("send_cmd(cmd=%d): write header", cmd);
		return -1;
	}

	if (len > 0 && TEMP_FAILURE_RETRY(write(fd, data, len)) < 0) {
		errmsg("send_cmd(cmd=%d): write data", cmd);
		return -1;
	}

	return 0;
}

static int
recv_cmd(int fd, cmd_t cmd)
{
	struct cmd hdr = { 0 };

	if (TEMP_FAILURE_RETRY(read(fd, &hdr, sizeof(hdr))) < 0) {
		errmsg("recv_cmd(cmd=%d): read header", cmd);
		return -1;
	}

	if (verbose > 2)
		info("received message: %s", print_cmd(&hdr));

	if (hdr.type != cmd) {
		info("recv_cmd(cmd=%d): got unexpected command %d", cmd, hdr.type);
		return -1;
	}

	return 0;
}

static int
get_pid_rc(int status)
{
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status))
			return WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	} else {
		return 255;
	}
	return EXIT_SUCCESS;
}

static int
container_parent(struct container *data, int child_sock, pid_t temp_pid)
{
	int i, rc, init_finished;
	pid_t init_pid;
	sigset_t mask;
	int fd_ep, fd_signal;
	int ep_timeout = 0;

	program_subname = "parent";
	myerror_progname = myerror_progname_subname;

	if (verbose > 2)
		info("started");

	cgroup_create(data->cgroups);

	if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0) < 0)
		myerror(EXIT_FAILURE, errno, "prctl(PR_SET_CHILD_SUBREAPER)");

	init_pid = init_finished = 0;

	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	sigdelset(&mask, SIGABRT);
	sigdelset(&mask, SIGSEGV);

	if ((fd_signal = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC)) < 0)
		myerror(EXIT_FAILURE, errno, "signalfd");

	fd_ep = epollin_init();
	epollin_add(fd_ep, fd_signal);
	epollin_add(fd_ep, child_sock);

	rc = EXIT_SUCCESS;

	while (1) {
		struct epoll_event ev[42];
		int fdcount;
		ssize_t size;

		errno = 0;
		if ((fdcount = epoll_wait(fd_ep, ev, ARRAY_SIZE(ev), ep_timeout)) < 0) {
			if (errno == EINTR)
				continue;
			myerror(EXIT_FAILURE, errno, "epoll_wait");
		}

		if (!fdcount) {
			if (!init_finished) {
				if (!init_pid) {
					if (send_cmd(child_sock, CMD_FORK_CLIENT, NULL, 0) < 0) {
						rc = EXIT_FAILURE;
						goto done;
					}
					init_pid = -1;
				} else if (init_pid < 0) {
					if (init_pid < -5) {
						info("waiting for client's pid for too long");
						rc = EXIT_FAILURE;
						goto done;
					}
					init_pid--;
				}
			}

			ep_timeout = 1000;
			continue;
		}

		for (i = 0; i < fdcount; i++) {
			if (!(ev[i].events & EPOLLIN)) {
				continue;
			}

			if (ev[i].data.fd == fd_signal) {
				pid_t pid;
				struct signalfd_siginfo fdsi;
				int status = 0;

				size = TEMP_FAILURE_RETRY(read(fd_signal, &fdsi, sizeof(struct signalfd_siginfo)));

				if (size != sizeof(struct signalfd_siginfo)) {
					info("unable to read signal info");
					continue;
				}

				if (fdsi.ssi_signo != SIGCHLD)
					goto done;

				if ((pid = waitpid(-1, &status, 0)) < 0) {
					errmsg("waitpid");
					rc = EXIT_FAILURE;
					goto done;
				}

				rc = get_pid_rc(status);

				if (pid == temp_pid) {
					if (rc != EXIT_SUCCESS) {
						info("temp pid ended unexpectedly (rc=%d)", rc);
						goto done;
					}

					temp_pid = 0;
					if (send_cmd(child_sock, CMD_CLIENT_REPARENT, NULL, 0) < 0) {
						rc = EXIT_FAILURE;
						goto done;
					}

					continue;
				}

				if (pid != init_pid)
					continue;

				init_finished = 1;
				init_pid = 0;

				if (verbose) {
					if (rc < 128)
						info("client process exit rc=%d", rc);
					if (rc > 128 && rc < 255)
						info("child process was terminated by a signal %d", rc - 128);
				}

				goto done;
			}

			if (ev[i].data.fd == child_sock) {
				struct cmd hdr = { 0 };

				if (TEMP_FAILURE_RETRY(read(child_sock, &hdr, sizeof(hdr))) < 0) {
					errmsg("read header");
					rc = EXIT_FAILURE;
					goto done;
				}

				if (verbose > 2)
					info("received message: %s", print_cmd(&hdr));

				switch (hdr.type) {
					case CMD_CLIENT_PID:
						if (hdr.datalen != sizeof(init_pid)) {
							info("unexpected data length");
							rc = EXIT_FAILURE;
							goto done;
						}
						if (TEMP_FAILURE_RETRY(read(child_sock, &init_pid, hdr.datalen)) < 0) {
							errmsg("unable to read client pid");
							rc = EXIT_FAILURE;
							goto done;
						}
						break;
					case CMD_CLIENT_READY:
						cgroup_add(data->cgroups, init_pid);

						if (send_cmd(child_sock, CMD_CLIENT_EXEC, NULL, 0) < 0) {
							rc = EXIT_FAILURE;
							goto done;
						}
						break;
					default:
						rc = EXIT_FAILURE;
						goto done;
				}
			}
		}
	}
done:
	if (fd_ep >= 0) {
		epollin_remove(fd_ep, fd_signal);
		epollin_remove(fd_ep, child_sock);
		close(fd_ep);
	}

	kill_container(data);

	cgroup_destroy(data->cgroups);
	free_data(data);

	unlink(pidfile);
	pidfile[0] = '\0';

	return rc;
}

static int
conatainer_child(struct container *data, int parent_sock)
{
	FILE *seccomp_fd = NULL;
	struct mapfile envs = {};
	struct mapfile devs = {};

	program_subname = "child";
	myerror_progname = myerror_progname_subname;

	if (recv_cmd(parent_sock, CMD_CLIENT_REPARENT) < 0)
		return EXIT_FAILURE;

	if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0)
		myerror(EXIT_FAILURE, errno, "prctl(PR_SET_PDEATHSIG)");

	if (data->input)
		reopen_fd(data->input, STDIN_FILENO);

	if (data->output) {
		reopen_fd(data->output, STDOUT_FILENO);
		reopen_fd(data->output, STDERR_FILENO);
	}

	if (data->devfile && open_map(data->devfile, &devs, 0) < 0)
		return EXIT_FAILURE;

	if (data->envfile && open_map(data->envfile, &envs, 0) < 0)
		return EXIT_FAILURE;

	if (data->seccomp && !(seccomp_fd = fopen(data->seccomp, "r")))
		myerror(EXIT_FAILURE, errno, "fopen: %s", data->seccomp);

	if (data->unshare_flags & CLONE_NEWNS) {
		if (mount("/", "/", "none", MS_PRIVATE | MS_REC, NULL) < 0 && errno != EINVAL)
			myerror(EXIT_FAILURE, errno, "mount(MS_PRIVATE): %s", data->root);

		if (data->mounts) {
			do_mount(data->root, data->mounts);
			free(data->mounts);
		}
	}

	if (data->devfile)
		make_devices(data->root, &devs);

	if (data->unshare_flags & CLONE_NEWNET)
		setup_network();

	if (data->hostname && sethostname(data->hostname, strlen(data->hostname)) < 0)
		myerror(EXIT_FAILURE, errno, "sethostname");

	if (nice(data->nice) < 0)
		myerror(EXIT_FAILURE, errno, "nice: %d", data->nice);

	if (chroot(data->root) < 0)
		myerror(EXIT_FAILURE, errno, "chroot");

	if (verbose > 1)
		info("chrooted: %s", data->root);

	if (chdir("/") < 0)
		myerror(EXIT_FAILURE, errno, "chdir");

	if (setsid() < 0)
		myerror(EXIT_FAILURE, errno, "setsid");

	clearenv();

	if (data->envfile)
		load_environ(&envs);

	if (send_cmd(parent_sock, CMD_CLIENT_READY, NULL, 0) < 0 ||
	    recv_cmd(parent_sock, CMD_CLIENT_EXEC) < 0)
		return EXIT_FAILURE;

	if (data->no_new_privs) {
		if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
			myerror(EXIT_FAILURE, errno, "prctl(PR_SET_NO_NEW_PRIVS)");
		if (verbose)
			info("set no new privileges");
	}

	if (data->caps)
		apply_caps(data->caps);

	if (data->seccomp)
		load_seccomp(seccomp_fd, data->seccomp);

	if (setregid(data->gid, data->gid) < 0)
		myerror(EXIT_FAILURE, errno, "setregid");

	if (setreuid(data->uid, data->uid) < 0)
		myerror(EXIT_FAILURE, errno, "setreuid");

	if (verbose)
		info("exec: %s", data->argv[0]);

	cloexec_fds();

	execvp(data->argv[0], data->argv);
	myerror(EXIT_FAILURE, errno, "execvp");

	return EXIT_FAILURE;
}

int
cmd_start(struct container *data)
{
	pid_t pid;
	int sv[2];

	if (access(data->root, R_OK | X_OK) < 0) {
		errmsg("access: %s", data->root);
		return EXIT_FAILURE;
	}

	if (setgroups((size_t) 0, NULL) < 0) {
		errmsg("setgroups");
		return EXIT_FAILURE;
	}

	if (sanitize_fds() < 0)
		return EXIT_FAILURE;

	if (background) {
		if (daemon(1, 1) < 0) {
			errmsg("daemon");
			return EXIT_FAILURE;
		}

		openlog(program_invocation_short_name, LOG_PID | LOG_NDELAY, LOG_DAEMON);
		use_syslog = 1;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0)
		myerror(EXIT_FAILURE, errno, "socketpair");

	if ((pid = fork()) < 0)
		myerror(EXIT_FAILURE, errno, "fork");

	if (pid > 0) {
		append_pid(getpid());
		return container_parent(data, sv[0], pid);
	}

	if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0)
		myerror(EXIT_FAILURE, errno, "prctl(PR_SET_PDEATHSIG)");

	if (recv_cmd(sv[1], CMD_FORK_CLIENT) < 0)
		return EXIT_FAILURE;

	// unshare namespaces
	unshare_flags(data->unshare_flags);

	// fork to switch to new pid namespace
	if ((pid = fork()) < 0)
		myerror(EXIT_FAILURE, errno, "fork");

	if (pid > 0) {
		if (verbose > 2)
			info("child forked (pid=%d)", pid);

		free_data(data);

		if (send_cmd(sv[1], CMD_CLIENT_PID, &pid, sizeof(pid)) < 0)
			myerror(EXIT_FAILURE, errno, "unable to transfer pid");

		return EXIT_SUCCESS;
	}

	// pid == 1 if pid namespace
	return conatainer_child(data, sv[1]);
}
