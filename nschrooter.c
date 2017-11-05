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

void perror_msg_and_die2(const char* msg, const char *extra) {
	if (extra) fprintf(stderr,"%s: ", extra);
	perror(msg);
	exit(1);
}

void perror_msg_and_die(const char* msg) {
	perror_msg_and_die2(msg, NULL);
}

void error_msg_and_die(const char* msg) {
	fprintf(stderr,"%s\n", msg);
	exit(1);
}

void procwritef(const char * fn, const char *msg, ...) {
	char buf[80];
        va_list ap;
        va_start(ap, msg);
	if (vsnprintf(buf,80,msg,ap) >= 80) {
		error_msg_and_die("procwritef buf overflow");
	}
        va_end(ap);
        int fd = open(fn, O_WRONLY);
        if (fd < 0) perror_msg_and_die2("procwritef open", fn);
        if (write(fd, buf, strlen(buf)) != strlen(buf))
        	perror_msg_and_die2("procwritef write", fn);
        close(fd);
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

	//procwritef("/proc/sys/kernel/hostname", "%s", hn);

	/* slave mount */
	if (mount(NULL, "/", NULL, MS_REC|MS_PRIVATE, NULL) != 0)
		perror_msg_and_die("slave mount");

	/* Bind mount the whole thing. */
	if (mount(path, path, NULL, MS_BIND|MS_REC, NULL) != 0)
		perror_msg_and_die("bind mount");

	//if (mount(NULL, path, NULL, MS_REC|MS_SHARED, NULL) != 0)
	//	perror_msg_and_die("shared mount");

	/* This chdir is necessary to change the current directory to the bind-mounted fs. */
	if (chdir(path) != 0)
		perror_msg_and_die("chdir(path)");

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
	if (chld == -1)
		perror_msg_and_die("fork");
	if (chld) {
		wait(NULL);
		exit(0);
	}

	/* One more mount namespace please. */
//	if (unshare(CLONE_NEWNS) != 0)
//		perror_msg_and_die("unshare2");

	execvp(argv[2], argv+2);
	perror_msg_and_die("execvp");
}
