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


static int pwritef(const char * fn, const char *buf) {
        int fd = open(fn, O_WRONLY);
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
        if (pwritef(fn, buf) != 0) perror_msg_and_die2("procwritef", fn);
}


int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr,"usage: $0 dir program [parameters]\n");
		exit(1);
	}

	int muid = getuid();
	int mgid = getgid();

	if (chdir(argv[1]) != 0)
		perror_msg_and_die("chdir(dir)");

	/* Figuring out the full path and default hostname for the container. */
	const char * path = realpath(".", NULL);
	const char * hn = NULL;
	if (path) {
		hn = strrchr(path, '/');
		if (hn) hn = hn+1;
	}
	if (!hn) hn = "(container)";

	if (unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUSER | CLONE_NEWUTS) != 0)
		perror_msg_and_die("unshare");

	procwritef("/proc/self/setgroups", "deny");
	procwritef("/proc/self/uid_map", "0 %d 1", muid);
	procwritef("/proc/self/gid_map", "0 %d 1", mgid);

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
	(void) mkdir("oldroot", 0777);

	if (mount("/", "./oldroot", NULL, MS_BIND|MS_REC, NULL) != 0)
		perror("oldroot move");

	if (mount(path, "/", NULL, MS_MOVE, NULL) != 0)
		perror_msg_and_die("move mount");

	if (chroot(".") != 0)
		perror_msg_and_die("chroot(.)");

	if (chdir("/") != 0)
		perror_msg_and_die("chdir(/)");

	/* We need to f**k it to be in the new pid namespace. */
	pid_t chld = fork();
	if (chld == -1) perror_msg_and_die("fork");

	if (chld) {
		/* I suppose we need to wait for the child. */
		wait(NULL);
		exit(0);
	}

	/* We are basically in the environment we need, on the rest
	 * of things just report errors instead of aborting on error */

	if (mount("proc", "/proc", "proc", 0, NULL) != 0)
		perror("mount /proc");


	unlink("dev"); rmdir("dev");
	unlink("sys"); rmdir("sys");

	if (symlink("/oldroot/dev", "dev") != 0)
		perror("dev symlink");

	if (symlink("/oldroot/sys", "sys") != 0)
		perror("sys symlink");

	if (sethostname(hn, strlen(hn)) != 0)
		perror("sethostname");

	char *termp = getenv("TERM");
	if (termp) termp -= strlen("TERM=");
	char * const env[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", termp, NULL };

	execvpe(argv[2], argv+2, env);
	perror_msg_and_die("execvp");
}
