/* Remove Pulseaudio library reference
 *
 * Copyright (C) 2016 kspflo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Build using: gcc -std=gnu99 -O2 -o pulsenomore pulsenomore.c -ldl

#define _GNU_SOURCE
#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define LIBPULSE_SIMPLE		"libpulse-simple.so.0"

#ifdef __NR_memfd_create
static inline int memfd_create(const char *name, unsigned int flags) {
	return syscall(__NR_memfd_create, name, flags);
}
#else
#error "memfd_create syscall missing!"
#endif

static int check_pulseaudio_running(void) {
	void *h;
	pid_t pid;
	int (*pa_pid_file_check_running)(pid_t *, const char *), running;

	if(!(h = dlopen(LIBPULSE_SIMPLE, RTLD_LAZY))) {
		warnx("Failed to resolve " LIBPULSE_SIMPLE);

		return 0;
	}

	if(!(pa_pid_file_check_running = dlsym(h, "pa_pid_file_check_running")))
		errx(1, "Failed to resolve symbol pa_pid_file_check_running");

	running = (pa_pid_file_check_running(&pid, "pulseaudio") < 0) ? 0 : 1;

	dlclose(h);

	return running;
}

int main(int argc, char **argv) {
	int fd, mfd;
	char **args, *mem, *p;
	ssize_t s;
	struct stat stbuf;

	if(argc < 2)
		errx(1, "Usage: %s <program> [<arg1> ... <argN>]", argv[0]);

	args = malloc(sizeof(char *) * argc);
	if(!args)
		err(1, "Failed to allocate argument vector");

	for(int i = 1; i < argc; ++i)
		args[i - 1] = strdup(argv[i]);

	args[argc - 1] = NULL;

	if(check_pulseaudio_running()) {
		warnx("Pulseaudio is running");

		execv(argv[1], args);
		err(1, "Failed to exec '%s'", argv[1]);
	}
	else {
		warnx("Pulseaudio is NOT running");

		if((fd = open(argv[1], O_RDONLY | O_CLOEXEC)) < 0)
			err(1, "Failed to open executable '%s'", argv[1]);

		if(fstat(fd, &stbuf) < 0)
			err(1, "Failed to stat executable");

		if((mfd = memfd_create(argv[1], MFD_CLOEXEC)) < 0)
			err(1, "Failed to create memfd");

		if(ftruncate(mfd, stbuf.st_size) < 0)
			err(1, "Failed to truncate memfd");

		if((p = mem = mmap(NULL, stbuf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0)) == MAP_FAILED)
			err(1, "Failed to mmap memfd");

		while((s = read(fd, p, 64 * 1024 * 1024)) > 0)
			p += s;

		if(s < 0)
			err(1, "Failed to load executable into memory");

		if((p = memmem(mem, stbuf.st_size, LIBPULSE_SIMPLE, strlen(LIBPULSE_SIMPLE))) != NULL) {
			strcpy(p, "/dev/null");
			p += strlen("/dev/null") + 1;
			while(*p)
				*p++ = '\0';

			warnx(LIBPULSE_SIMPLE " reference replaced");
		}
		else
			warnx(LIBPULSE_SIMPLE " reference not found");

		munmap(mem, stbuf.st_size);

		fexecve(mfd, args, environ);
		err(1, "Failed to exec '%s'", argv[1]);
	}
}
