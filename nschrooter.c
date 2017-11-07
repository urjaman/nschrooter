/* See LICENSE. */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/mount.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/sysmacros.h>
#include <stdint.h>
#include <dirent.h>


static void perror_msg_and_die2(const char* msg, const char *extra) {
	if (extra) fprintf(stderr,"%s: ", extra);
	perror(msg);
	exit(1);
}

static void perror_msg_and_die(const char* msg) {
	perror_msg_and_die2(msg, NULL);
}

static void error_msg_and_die(const char* msg) {
	fprintf(stderr,"%s\n", msg);
	exit(1);
}


static int pwritef(const char * fn, const char *buf, int flags) {
        int fd = open(fn, O_WRONLY | flags, 0600);
        if (fd < 0) return -1;
        if (write(fd, buf, strlen(buf)) != strlen(buf))
        	return -1;
        close(fd);
        return 0;
}


static void procwritef(const char * fn, const char *msg, ...) {
	char buf[80];
        va_list ap;
        va_start(ap, msg);
	if (vsnprintf(buf,80,msg,ap) >= 80) {
		error_msg_and_die("procwritef buf overflow");
	}
        va_end(ap);
        if (pwritef(fn, buf, 0) != 0) perror_msg_and_die2("procwritef", fn);
}

static void writelinef(const char * fn, const char *msg, ...) {
	char buf[80];
        va_list ap;
        va_start(ap, msg);
	if (vsnprintf(buf,80,msg,ap) >= 80) {
		error_msg_and_die("buf overflow");
	}
        va_end(ap);
        if (pwritef(fn, buf, O_CREAT) != 0) perror_msg_and_die2("writelinef", fn);
}

static void run_prog(char **argv) {
	/* We make a new environment just to be nice (and to add *sbin). */
	char *termp = getenv("TERM");
	if (termp) termp -= strlen("TERM=");
	char * const env[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", termp, NULL };

	execvpe(argv[0], argv, env);
	perror_msg_and_die("execvp");
}

static int ns_enter(int pid, char **argv) {
	/* Enter the namespaces identified by the pid
	 * and run the program specified in argv. */
	const char * spaces[] = {
		"/proc/%d/ns/user",
		"/proc/%d/ns/uts",
		"/proc/%d/ns/pid",
		"/proc/%d/ns/mnt"
	};
	char buf[6+10+4+4+1];
	int s = 1;
	/* If we're non-root, enter the user namespace first. */
	if (getuid()) s = 0;

	for (;s<4;s++) {
		sprintf(buf,spaces[s],pid);
		int fd = open(buf, O_RDONLY);
		if (setns(fd, 0) != 0)
			perror_msg_and_die2("setns", buf);
		close(fd);
	}

	if (chdir("/") != 0)
		perror_msg_and_die("chdir(/)");

	/* To enter the pid namespace, do a fork(). */
	int chld = fork();
	if (chld == -1) perror_msg_and_die("fork");
	if (chld) {
		/* Wait for the child.. */
		wait(NULL);
		exit(0);
	}

	/* Here we gooooo... */
	run_prog(argv);
	return 1;
}


static int proc_list_pids(DIR **proc) {
	if (!(*proc)) {
		*proc = opendir("/proc");
	}
	if (!(*proc)) return 0;
	struct dirent *d;
	while ((d = readdir(*proc))) {
		char *e;
		errno = 0;
		long int pid = strtol(d->d_name,&e,10);
		if (errno) continue;
		if (*e != 0) continue;
		return pid;
	}
	closedir(*proc);
	*proc = NULL;
	return 0;
}

#define PID1_FN ".pid1"

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr,"usage: $0 dir program [parameters]\n");
		exit(1);
	}

	int muid = getuid();
	int mgid = getgid();

	if (chdir(argv[1]) != 0)
		perror_msg_and_die("chdir(dir)");

	/* Check for a .pid1 file in the chroot. */
	int p1fd = open(PID1_FN, O_RDONLY);
	if (p1fd>=0) {
		char buf[6+10+4+1]; /* Enough for /proc/N/cwd */
		/* Validate pid in file... */
		int l = 0;
		do {
			int r = read(p1fd, buf+l, 16-l);
			if ((r==-1)&&(errno==EINTR)) continue;
			if (r<=0) break;
			l += r;
		} while (1);
		if (l==16) l = 0;
		close(p1fd);
		if (l) {
			buf[l] = 0;
			int pid = atoi(buf);
			if (pid<=0) pid = 0;
			if (pid) {
				/* Validate that it is an existing process and has cwd at root... */
				sprintf(buf,"/proc/%d/cwd",pid);
				char *p = realpath(buf, NULL);
				if ((p)&&(strcmp(p,"/")==0)) {
					free(p);
					return ns_enter(pid, argv+2);
				}
				free(p);
			}
		}
		unlink(PID1_FN);
		fprintf(stderr, "Removed stale " PID1_FN " file\n");
	}

	/* Figuring out the full path and default hostname for the container. */
	const char * path = realpath(".", NULL);
	const char * hn = NULL;
	if (path) {
		hn = strrchr(path, '/');
		if (hn) hn = hn+1;
	}
	if (!hn) hn = "(container)";

	/* Only do user namespaces if we have to. */
	int more_flags = muid ? CLONE_NEWUSER : 0;

	if (unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | more_flags) != 0)
		perror_msg_and_die("unshare");

	if (muid) {
		procwritef("/proc/self/setgroups", "deny");
		procwritef("/proc/self/uid_map", "0 %d 1", muid);
		procwritef("/proc/self/gid_map", "0 %d 1", mgid);
	}

	/* slave mount */
	if (mount(NULL, "/", NULL, MS_REC|MS_PRIVATE, NULL) != 0)
		perror_msg_and_die("slave mount");

	/* Bind mount the whole thing. */
	if (mount(path, path, NULL, MS_BIND|MS_REC, NULL) != 0)
		perror_msg_and_die("bind mount");

	/* This chdir is necessary to change the current directory to the bind-mounted fs. */
	if (chdir(path) != 0)
		perror_msg_and_die("chdir(path)");

	/* Make the old rootfs visible. We need them later, and as an user we cant unmount them either.  */
	(void) mkdir("oldroot", 0755);

	if (mount("/", "./oldroot", NULL, MS_BIND|MS_REC, NULL) != 0)
		perror("oldroot move");

	if (mount(path, "/", NULL, MS_MOVE, NULL) != 0)
		perror_msg_and_die("move mount");

	if (chroot(".") != 0)
		perror_msg_and_die("chroot(.)");

	if (chdir("/") != 0)
		perror_msg_and_die("chdir(/)");

	int initmode = 1; /* 0= the program is init, 1= we become init */

	/* If program name ends in "/init", it is init (eg. /sbin/init, or /init are.) */
	int a0l = strlen(argv[2]);
	if (strcmp(argv[2]+(a0l-5),"/init")==0) initmode = 0;

	int pifd[2];
	if (initmode) if (pipe(pifd) != 0) perror_msg_and_die("pipe");

	/* We need to f**k it to be in the new pid namespace. */
	pid_t chld = fork();
	if (chld == -1) perror_msg_and_die("fork");

	if (chld) {
		/* Store the child pid for other entries into the "chroot". */
		writelinef(PID1_FN, "%d", chld);

		if (initmode) {
			/* We quit when the program launched by init quits. */
			/* If the program launches daemons or other programs
			 * join the namespace in the meantime, the init gets
			 * left behind to take care of them, and quits when
			 * there are no more processes in the namespace. */
			int r;
			uint8_t dummy[1];
			close(pifd[1]);
			do {
				r = read(pifd[0], dummy, 1);
				if ((r==-1)&&(errno==EINTR)) continue;
			} while (0);
		} else {
			/* I suppose we need to wait for the child. */
			wait(NULL);
			/* Child is gone, remove pidfile. */
			unlink(PID1_FN);
		}
		exit(0);
	}
	if (initmode) close(pifd[0]);

	/* We are basically in the environment we need, on the rest
	 * of things just report errors instead of aborting on error */

	if (mount("proc", "/proc", "proc", 0, NULL) != 0)
		perror("mount /proc");

	/* These will fail if /dev and/or sys are correct already. */
	unlink("dev"); rmdir("dev");
	unlink("sys"); rmdir("sys");

	if (muid) {
		/* User mode, /dev and /sys symlinks. */
		if (symlink("/oldroot/dev", "dev") != 0) perror("dev symlink");
		if (symlink("/oldroot/sys", "sys") != 0) perror("sys symlink");
	} else {
		/* Superuser mode, bind mounts. */
		mkdir("dev", 0755);
		mkdir("sys", 0755);
		int dfd = open("/dev/zero", O_RDONLY);
		DIR *syschk = opendir("/sys/dev");
		if (dfd < 0) {
			if (mount("/oldroot/dev", "dev", NULL, MS_BIND, NULL) != 0)
				perror("mount /dev");
			if (mount("/oldroot/dev/pts", "dev/pts", NULL, MS_BIND, NULL) != 0)
				perror("mount /dev/pts");
		} else {
			close(dfd);
		}
		if (!syschk) {
			if (mount("/oldroot/sys", "sys", NULL, MS_BIND, NULL) != 0)
				perror("mount /sys");
		} else {
			closedir(syschk);
		}
	}

	if (sethostname(hn, strlen(hn)) != 0)
		perror("sethostname");

	if (initmode) {
		/* We need to become init for the program we are about to run,
		 * and any others that join the namespace later. */
		pid_t prog = fork();
		if (prog == -1) perror_msg_and_die("fork");
		if (prog) do { /* We are init. */
			int r = wait(NULL);
			if (r == -1) {
				if (errno==EINTR) continue;
				if (errno==ECHILD) {
					/* Check a bit more thorougly. */
					DIR *d = NULL;
					int p;
					while((p = proc_list_pids(&d))) {
						if (p>1) break;
					}
					if (d) closedir(d);
					if (p > 1) {
						/* Found someone. */
						sleep(3); /* Snooze */
						continue;
					}
				}
				/* Else assume we should bail out. */
				unlink(PID1_FN);
				exit(0);
			}
			if (r == prog) {
				uint8_t d[1] = { 0 };
				/* Report that the program quit to our parent. */
				do {
					int x = write(pifd[1], d, 1);
					if ((x == 0)||((x == -1)&&(errno == EINTR))) continue;
				} while(0);
				prog = -1;
			}
		} while(1);
		close(pifd[1]); /* Dont leak the pipe write fd to the program. */
	}

	run_prog(argv+2);
}
