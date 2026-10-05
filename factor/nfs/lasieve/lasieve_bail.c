/*
 * lasieve_bail.c -- leaving the siever without leaving the process.
 *
 * The siever used to be a program, so its error paths and its main() could
 * exit().  It is now called from inside yafu, where exit() would take the whole
 * factoring run down with it.  Those call sites call lasieve_bail() instead, and
 * lasieve_run() has armed a frame for it to unwind to, so the status comes back
 * from lasieve_run() as an ordinary return value.
 *
 * This lives in its own file rather than inside lasieve_dispatch.c because the
 * standalone sieve regressions compile if.c, redu2.c and friends directly and
 * need lasieve_bail() too.  Linking the dispatch would drag in all six per-I
 * object sets; linking this drags in one function.
 *
 * With nothing armed -- a regression binary, which has no lasieve_run() to
 * return to -- it falls back to exit() and behaves as it always did.
 *
 * The frame itself is set up in lasieve_dispatch.c, not here: setjmp has to run
 * in the frame that will be returned to, so only the storage lives here.
 */

#include <setjmp.h>
#include <stdlib.h>

__thread jmp_buf lasieve_bail_frame;
__thread int lasieve_bail_armed;

void lasieve_bail(int status)
{
	if (lasieve_bail_armed)
	{
		/* longjmp wants a nonzero value, and 0 is a legitimate answer from
		 * the end of the siever's main */
		longjmp(lasieve_bail_frame, status ? status : 1);
	}
	exit(status);
}
