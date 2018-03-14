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
#include <stdint.h>
#include <seccomp.h>

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

static void run_prog(char **argv) {
	execvp(argv[0], argv);
	perror("execvp");
	exit(127); /* Specific code for failure to run command. */
}


static void apply_seccomp(void) {
	int r = 0;
	scmp_filter_ctx c = seccomp_init(SCMP_ACT_ALLOW);
	if (!c) error_msg_and_die("seccomp_init failed");

	/* chown */
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(chown), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(chown32), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(fchown), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(fchown32), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(fchownat), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(lchown), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(lchown32), 0)) < 0) goto err_r;

	/* set*id, etc. Change of groups or user/fs/etc ids... */
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setfsgid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setfsgid32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setfsuid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setfsuid32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setgid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setgid32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setgroups), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setgroups32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setregid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setregid32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setresgid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setresgid32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setresuid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setresuid32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setreuid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setreuid32), 0)) < 0) goto err_r;

	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setuid), 0)) < 0) goto err_r;
	if ((r = seccomp_rule_add(c, SCMP_ACT_ERRNO(0), SCMP_SYS(setuid32), 0)) < 0) goto err_r;

	if ((r = seccomp_load(c)) < 0) goto err_r;

	seccomp_release(c);
	return;

err_r:
	errno = -r;
	perror_msg_and_die("seccomp");
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr,"usage: %s <command> [parameters..]\n", argv[0]);
		exit(1);
	}
	apply_seccomp();
	run_prog(argv+1);
}
