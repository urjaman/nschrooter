/* See LICENSE. */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <dirent.h>

static int proc_list_pids(const char *dir, DIR **proc) {
	if (!(*proc)) {
		*proc = opendir(dir);
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

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr,"usage: %s proc-dir grepname\n", argv[0]);
		exit(1);
	}
	DIR *d = NULL;
	int p;
	char * nbuf = malloc(strlen(argv[1]) + 17); /* [1]/%d/comm\0 = 1+10+5+1 = 17 */
	while ((p = proc_list_pids(argv[1],&d))) {
		char name[32]; /* comm has a short name */
		sprintf(nbuf,"%s/%d/comm",argv[1], p);
		int fd = open(nbuf, O_RDONLY);
		if (fd < 0) continue; /* It's fine if processes disappear. */
		int l = 0;
		do {
			int r = read(fd, name+l, 32-l);
			if ((r==-1)&&(errno==EINTR)) continue;
			if (r <= 0) break; /* End of file */
			l += r;
		} while (l<32);
		close(fd);
		if (l>=32) l = 31; /* We cut. */
		if (!l) continue;
		name[l] = 0;
		if (strstr(name,argv[2])) printf("%d ", p);
	}
	return 0;
}
