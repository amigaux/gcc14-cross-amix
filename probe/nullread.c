/* nullread.c -- does reading through a null pointer fault on this kernel?
 *
 * A diagnostic, not a toolchain test.  A child process reads the word at
 * address 12 and writes it back; the parent reports whether the child died
 * of SIGSEGV.  On a kernel that leaves page zero unmapped it does.  On one
 * that maps it, a null-pointer dereference carries on silently, and a
 * program can pass on that machine and crash on another: libc's setlogmask
 * called before openlog is the case that showed it.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	int status;
	pid_t pid = fork();

	if (pid < 0) {
		perror("nullread: fork");
		return 2;
	}
	if (pid == 0) {
		volatile int *p = (volatile int *)12;

		*p = *p;
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid) {
		perror("nullread: waitpid");
		return 2;
	}
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV)
		printf("nullread: reading address 12 faults (SIGSEGV): page zero is not mapped\n");
	else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		printf("nullread: reading and writing address 12 worked: page zero is mapped\n");
	else
		printf("nullread: the child ended with status 0x%x\n", status);
	fflush(stdout);
	return 0;
}
