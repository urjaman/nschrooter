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

/* Make new malloc() string c = a + b */
static char* strdcat(const char *a, const char *b) {
	size_t la = strlen(a);
	char* c = malloc(la+strlen(b));
	if (!c) perror_msg_and_die("malloc");
	memcpy(c,a,la);
	strcpy(c+la,b);
	return c;
}

static int clean_env = 0;
static void run_prog(char **argv) {
	if (clean_env) {
		/* We make a new environment just to be nice (and to add *sbin). */
		char *termp = getenv("TERM");
		if (termp) termp -= strlen("TERM=");
		char * const env[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", termp, NULL };

		execvpe(argv[0], argv, env);
		perror("execvpe");
	} else {
		execvp(argv[0], argv);
		perror("execvp");
	}
	exit(127); /* Specific code for failure to run command. */
}

/* Generate a return value from a wait() status variable. */
static uint8_t wait_retval(int status) {
	if (WIFEXITED(status)) return WEXITSTATUS(status);
	if (WIFSIGNALED(status)) return WTERMSIG(status)+128;
	/* Umm, well it likely wasnt succesful... arbitrary pick. */
	return 255;
}

static void ns_enter(int pid, char **argv) {
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
		int s;
		wait(&s);
		exit(wait_retval(s));
	}

	/* Here we gooooo... */
	run_prog(argv);
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

static char *pfdreader(int fd, int *l) {
	int ml = 1;
	char *buf = NULL;
	int o = 0;
	do {
		if ((ml-o-1) < 2048) {
			ml += 4096;
			buf = realloc(buf, ml);
			if (!buf) perror_msg_and_die("(re)alloc");
		}
		int r = read(fd, buf+o, ml-o-1);
		if ((r==-1)&&(errno==EINTR)) continue;
		if (r<=0) break;
		o += r;
	} while (1);
	buf[o] = 0;
	if (l) *l = o;
	return buf;
}

static char * pmountsreader(void) {
	int fd = open("/proc/self/mounts", O_RDONLY);
	if (fd < 0) return NULL;
	char * d = pfdreader(fd, NULL);
	close(fd);
	return d;
}

static void umountizer(const char *prefix) {
	int plen = strlen(prefix);
	/* Unmount everything. */
	int umounted;
	int failed;
	do {
		char * mounts = pmountsreader();
		umounted = 0;
		failed = 0;
		char * pp = mounts;
		char * nl;
		do {
			/* Find the next line beforehand, because we edit part of line in-place. */
			if (!pp) break;
			nl = strchr(pp, '\n');
			if (nl) nl++;

			/* Find the mount path (between the first two spaces on the line) */
			char *ns1 = strchr(pp, ' ');
			if (!ns1) break;
			ns1++;
			char *ns2 = strchr(ns1, ' ');
			if (!ns2) break;

			/* In-place convert out octal escapes from the path and make it into a C-string. */
			int l = ns2 - ns1;
			int wi = 0;
			for (int i=0;i<l;i++) {
				if (ns1[i] == '\\') {
					ns1[wi++] = ((ns1[i+1] << 6) & 0300) | ((ns1[i+2] << 3) & 0070) | (ns1[i+3] & 0007);
					i += 3; /* skip the octal value */
					continue;
				}
				ns1[wi++] = ns1[i];
			}
			ns1[wi] = 0;
			int mlen = plen;
			if (mlen > wi) mlen = wi;

			/* If the result doesnt match prefix, try unmounting it */
			if ((strncmp(prefix, ns1, mlen) != 0)) {
				if (umount2(ns1, MNT_DETACH) == 0) umounted++;
				else failed++;
			}
		} while ((pp = nl));
		free(mounts);
		/* If all success = stop, if all fails = stop, if nothing to do = stop, else go try again. */
	} while (umounted && failed);
}

#define PID1_FN ".pid1"

void usage(char *name) {
	fprintf(stderr,"usage: %s [options] dir program [parameters]\n"
		"\n\tOptions:"
		"\n\t-i\tProvide init (default unless program ends /init)"
		"\n\t-b\tBoot system (dont provide init)"
		"\n\t-k\tKill previous instance (force new namespace)"
		"\n\t-E\tEnter previous namespace (dont make new ns)"
		"\n\t-A\tMount/Provide /proc,/dev and /sys for you (default if user)"
		"\n\t-N\tDont mount /proc,/dev,/sys (default if root)"
		"\n\t-c\tCleanup environment (only passes TERM and a clean PATH)"
		"\n\t-M hn\tSet hostname (default=directory name)"
		"\n\t-r path\tMount old root at path (default if user=oldroot,if root none)"
		"\n\t-t sec\tExit timeout in an empty namespace (default 5, -1 = forever)"
	"\n\n",name);
	exit(1);
}

int main(int argc, char **argv) {
	int entermode = 2; /* 0= Force new ns, 1= Force enter old ns, 2= automatic (enter old if exists) */
	int initmode = 2; /* 0= the program is init, 1= we become init, 2= automatic (0 if prog ends /init)  */
	int automounts = 2; /* 0= no helpful mounts, 1= do helpful mounts, 2= automatic (only if user) */
	char *hn = NULL; /* Hostname */
	char *old_root = NULL;
	int opt;
	int init_timeout = 5;

	int muid = getuid();
	int mgid = getgid();

	while ((opt = getopt(argc, argv, "+ibkEANcM:r:t:")) != -1) {
		switch (opt) {
			default: usage(argv[0]); break;
			case 'i': initmode = 1; break; /* -i = nschrooter provides ns pid 1 (Init) */
			case 'b': initmode = 0; break; /* -b = Boot a system, program is init */
			case 'k': entermode = 0; break; /* Force new namespace, Kill previous init */
			case 'E': entermode = 1; break; /* Enter old namespaces, dont try making new. */
			case 'A': automounts = 1; break; /* Help with /proc,/sys,/dev */
			case 'N': automounts = 0; break; /* No help with ^^ */
			case 'c': clean_env = 1; break; /* Cleanup environment */
			case 'M': hn = optarg; break; /* Setting hostname with the -M flag */
			case 'r': old_root = optarg; break; /* Path to old root */
			case 't': init_timeout = atoi(optarg); break; /* Timeout for exiting as init in an empty ns. */
		}
	}

	if ((argc - optind) < 2) usage(argv[0]);

	/* Automatic means we only automount if user */
	if (automounts == 2) automounts = muid ? 1 : 0;

	/* In user mode enable old_root always. */
	if ((!old_root)&&(muid)) old_root = "oldroot";

	if (chdir(argv[optind]) != 0)
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
					if (entermode) {
						ns_enter(pid, argv+optind+1);
					} else {
						kill(pid, SIGKILL);
						fprintf(stderr, "Killed previous pid 1 (%d)\n", pid);
					}
					/* ns_enter does not return */
				} else {
					free(p);
				}
			}
		}
		unlink(PID1_FN);
		if (entermode) fprintf(stderr, "Removed stale " PID1_FN " file\n");
		if (entermode==1) {
			fprintf(stderr, "Cannot enter (-E) old namespace\n");
			exit(1);
		}
	}

	/* Figuring out the full path and default hostname for the container. */
	const char * path = realpath(".", NULL);
	if (!path) perror_msg_and_die("realpath");

	if (!hn) {
		hn = strrchr(path, '/');
		if (hn) hn = hn+1;
		if (!hn) hn = "(container)";
	}

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
	if (mount(NULL, "/", NULL, MS_REC|MS_SLAVE, NULL) != 0)
		perror_msg_and_die("slave mount");

	/* Bind mount the whole thing. */
	if (mount(path, path, NULL, MS_BIND|MS_REC, NULL) != 0)
		perror_msg_and_die("bind mount");

	/* This chdir is necessary to change the current directory to the bind-mounted fs. */
	if (chdir(path) != 0)
		perror_msg_and_die("chdir(path)");

	if (automounts) { /* for /dev and /sys */
		/* These will fail if /dev and/or sys are correct already. */
		unlink("dev"); rmdir("dev");
		unlink("sys"); rmdir("sys");

		if (muid) {
			/* User mode, /dev and /sys symlinks. */
			char *ds = strdcat(old_root,"/dev");
			char *ss = strdcat(old_root,"/sys");
			if (symlink(ds, "dev") != 0) perror("dev symlink");
			if (symlink(ss, "sys") != 0) perror("sys symlink");
			free(ds);
			free(ss);
		} else {
			/* Superuser mode, bind mounts. */
			mkdir("dev", 0755);
			mkdir("sys", 0755);
			if (mount("/dev", "dev", NULL, MS_BIND|MS_REC, NULL) != 0)
				perror("mount /dev");
			if (mount("/sys", "sys", NULL, MS_BIND, NULL) != 0)
				perror("mount /sys");
		}
	}

	if (old_root) {
		/* Make the old rootfs visible. We need them later, and as an user we cant unmount them either.  */
		(void) mkdir(old_root, 0755);

		if (mount("/", old_root, NULL, MS_BIND|MS_REC, NULL) != 0)
			perror("oldroot move");
	}

	umountizer(path);

	if (mount(path, "/", NULL, MS_MOVE, NULL) != 0)
		perror_msg_and_die("move mount");

	if (chroot(".") != 0)
		perror_msg_and_die("chroot(.)");

	if (chdir("/") != 0)
		perror_msg_and_die("chdir(/)");


	if (initmode == 2) { /* Automatic mode */
		/* If program name ends in "/init", it is init (eg. /sbin/init, or /init are.) */
		int a0l = strlen(argv[2]);
		if (strcmp(argv[2]+(a0l-5),"/init")==0) initmode = 0;
	}

	int pifd[2];
	if (initmode) if (pipe(pifd) != 0) perror_msg_and_die("pipe");

	/* We need to f**k it to be in the new pid namespace. */
	pid_t chld = fork();
	if (chld == -1) perror_msg_and_die("fork");

	if (chld) {
		/* Store the child pid for other entries into the "chroot". */
		writelinef(PID1_FN, "%d", chld);
		uint8_t retval[1] = { 0 };

		if (initmode) {
			/* We quit when the program launched by init quits. */
			/* If the program launches daemons or other programs
			 * join the namespace in the meantime, the init gets
			 * left behind to take care of them, and quits when
			 * there are no more processes in the namespace. */
			int r;
			close(pifd[1]);
			do {
				r = read(pifd[0], retval, 1);
				if ((r==-1)&&(errno==EINTR)) continue;
			} while (0);
		} else {
			int s;
			/* I suppose we need to wait for the child. */
			wait(&s);
			retval[0] = wait_retval(s);
			/* Child is gone, remove pidfile. */
			unlink(PID1_FN);

		}
		exit(retval[0]);
	}
	if (initmode) close(pifd[0]);

	/* We are basically in the environment we need, on the rest
	 * of things just report errors instead of aborting on error */

	if (automounts) { /* for /proc, since needs to be in new pid ns. */
		(void) mkdir("proc", 0755);
		if (mount("proc", "/proc", "proc", MS_NOEXEC|MS_NOSUID|MS_NODEV, NULL) != 0)
			perror("mount /proc");

	}

	if (sethostname(hn, strlen(hn)) != 0)
		perror("sethostname");

	if (initmode) {
		/* We need to become init for the program we are about to run,
		 * and any others that join the namespace later. */
		int timeout = 0;
		pid_t prog = fork();
		if (prog == -1) perror_msg_and_die("fork");
		if (prog) do { /* We are init. */
			int s;
			int r = wait(&s);
			if (r == -1) {
				if (errno==EINTR) continue;
				if (errno==ECHILD) {
					if (init_timeout < 0) {
						/* -1 = Forever */
						sleep(30);
						continue;
					}
					/* Check a bit more thorougly. */
					DIR *d = NULL;
					int p;
					while((p = proc_list_pids(&d))) {
						if (p>1) break;
					}
					if (d) closedir(d);
					if (p > 1) {
						/* Found someone. */
						timeout = 0;
						sleep(3); /* Snooze */
						continue;
					}
					if ((++timeout) <= init_timeout) {
						/* Give it a moment */
						sleep(1);
						continue;
					}
				}
				/* Else assume we should bail out. */
				unlink(PID1_FN);
				exit(0);
			}
			if (r == prog) {
				uint8_t retval[1] = { wait_retval(s) };
				/* Report that the program quit to our parent. */
				do {
					int x = write(pifd[1], retval, 1);
					if ((x == 0)||((x == -1)&&(errno == EINTR))) continue;
				} while(0);
				prog = -1;
			}
		} while(1);
		close(pifd[1]); /* Dont leak the pipe write fd to the program. */
	}

	run_prog(argv+optind+1);
}
