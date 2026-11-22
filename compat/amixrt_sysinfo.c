/* amixrt_sysinfo.c -- sysinfo(2), for libamixrt.so.
 *
 * <sys/systeminfo.h> declares it, the kernel has it (SYS_systeminfo, 139),
 * and libc.so.1 does not export it: AMIX has it only as a static member of
 * usr/ccs/lib/libc.so, sysinfo.o, which traps with 139 and the arguments on
 * the stack and goes to _cerror on failure.  This is the same call through
 * __amixrt_syscall.  ToolTalk and CDE's DtSvc call it for the host name and
 * the secure RPC domain.  The int return is the header's; it is the length
 * the value needs, its terminating null included, or -1 with errno set.
 */
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/utsname.h>	/* SYS_NMLN, which <sys/systeminfo.h> uses */
#include <sys/systeminfo.h>

extern int __amixrt_syscall(int, ...);

int sysinfo(int command, char *buf, long count)
{
	return __amixrt_syscall(SYS_systeminfo, command, buf, count);
}
