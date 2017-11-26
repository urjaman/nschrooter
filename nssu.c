/* See LICENSE. */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <pwd.h>

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

/* Make new malloc() string c = a + b */
static char* strdcat(const char *a, const char *b) {
	size_t la = strlen(a);
	char* c = malloc(la+strlen(b));
	if (!c) perror_msg_and_die("malloc");
	memcpy(c,a,la);
	strcpy(c+la,b);
	return c;
}

void msetenv(const char *name, const char *value) {
	if (setenv(name, value, 1)) perror_msg_and_die2("setenv", name);
}

void usage(char *name) {
	fprintf(stderr, "Usage: %s [-mpl] [-] [-s shell] [user] [-c CMD] [ARGS]"
		"\n\n"
		"Change apparent identity to that of user (by default, root) and run shell\n"
		"\n\t-,-l\tClear environment, go to home, run shell as login shell"
		"\n\t-p,-m\tDo not set new $HOME, $SHELL, $USER, $LOGNAME"
		"\n\t-c CMD\tCommand to pass to 'sh -c'"
		"\n\t-s SH\tShell to use"
	"\n", name);
	exit(1);
}

int main(int argc, char **argv) {
	int login = 0;
	int env_preserve = 0;
	char * shell = NULL;
	char * user = NULL;
	char * cmd = NULL;

	int muid = getuid();
	int mgid = getgid();

	int tuid = 0;
	int tgid = 0;
	int opt;

	while ((opt = getopt(argc, argv, "mplc:s:")) != -1) {
		switch (opt) {
			default: usage(argv[0]); break;
			case 'm': case 'p': env_preserve = 1; break;
			case 'l': login = 1; break;
			case 'c': cmd = optarg; break;
			case 's': shell = optarg; break;
		}
	}

	/* Detect '-' (login) */
	if (argv[optind] && argv[optind][0] == '-' && argv[optind][1] == 0) {
		login = 1;
		optind++;
	}

	/* Username */
	if (argv[optind]) {
		user = argv[optind++];
	}

	/* Defaults are for root so if no specific user was requested and we cant
	 * find passwd info then we can live with the defaults. */
	struct passwd *pw = getpwnam(user?user:"root");
	if ((user)&&(!pw)) error_msg_and_die("Unknown user");
	if (!user) user = "root"; /* Future doesnt need to know */

	const char *home = "/root";
	char *pw_shell = "/bin/sh";

	if (pw) {
		tuid = pw->pw_uid;
		tgid = pw->pw_gid;
		pw_shell = pw->pw_shell;
		home = pw->pw_dir;
	}

	if (!shell && env_preserve) {
		shell = getenv("SHELL");
	}

	if (!shell) shell = pw_shell;

	/* Dont change namespace if no change is necessary. */
	if (tuid != muid) {
		/* Check that we _cannot_ setuid() to target. If we can, we have
		 * too much actual power ;) */
		int r = setuid(tuid);
		if ( ((r==0)&&(getuid()==tuid)) || ((r==-1)&&(errno==EAGAIN)) )
			error_msg_and_die("Do not use nssu while actually root");

		if (unshare(CLONE_NEWUSER) != 0)
			perror_msg_and_die("unshare");

		procwritef("/proc/self/setgroups", "deny");
		procwritef("/proc/self/uid_map", "%d %d 1", tuid, muid);
		procwritef("/proc/self/gid_map", "%d %d 1", tgid, mgid);
	}

	if (login) {
		const char* term = getenv("TERM");
		if (chdir(home) != 0) {
			perror("chdir");
			fprintf(stderr,"Instead you'll be at /");
			if (chdir("/") != 0) perror("chdir");
		}
		clearenv();
		if (term) msetenv("TERM", term);
		msetenv("PATH", tuid ? "/bin:/usr/bin" : "/bin:/sbin:/usr/bin:/usr/sbin");
		msetenv("USER", user);
		msetenv("LOGNAME", user);
		msetenv("HOME", home);
		msetenv("SHELL", shell);
	} else {
		if (tuid) {
			msetenv("USER", user);
			msetenv("LOGNAME", user);
		}
		msetenv("HOME", home);
		msetenv("SHELL", shell);
	}

	if (cmd) {
		argv[--optind] = cmd;
		argv[--optind] = "-c";
	}

	char *arg0 = strrchr(shell, '/');
	if (arg0) arg0++;
	if (!arg0) arg0 = shell;
	if (login) arg0 = strdcat("-", arg0);
	argv[--optind] = arg0;

	execvp(shell,argv+optind);
	perror("execvp");
	exit(127); /* Specific code for failure to run command. */
}
