/* libc-calls-main.c -- the two programs of probe/libc-calls.c.
 *
 * Built by tools/test-amixrt.sh twice: as libc-static, linked with
 * libc-calls.c itself, so that the calls under test are libc's static
 * members; and with -DLC_VIA_LIBRARY as libc-amixrt, linked only with
 * libsolibscmid.so.1, so that they are libamixrt.so.1's.  The scratch files
 * go in the directory named on the command line, /tmp by default.
 */
int lc_run(const char *);
int mid_lc_run(const char *);

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/tmp";

#ifdef LC_VIA_LIBRARY
	return mid_lc_run(dir);
#else
	return lc_run(dir);
#endif
}
