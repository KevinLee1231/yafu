/*
 * lasieve_dispatch.c -- the entry point yafu uses to run the siever.
 *
 * There is no separate siever program any more.  The six I values are six sets
 * of private objects in the yafu image -- private sieve kernels, factor base,
 * montgomery state, ECM/P-1 cache -- and lasieve_run() picks one from the I
 * value the caller passes:
 *
 *     lasieve_run(13, argc, argv)
 *
 * yafu builds that argv exactly as it used to build the command line for the
 * child process, so the siever's own option handling is untouched.  Each
 * concurrent siever must be given a different I value: the per-I copies are
 * what keep two sievers from sharing state, and there are six of them.
 *
 * Two things the siever could do as a program that it must not do here:
 *
 *   exit()      would take the whole factoring run down with it.  The siever
 *               reaches exit() on internal errors and at the end of main, so
 *               lasieve_bail() longjmps back to the setjmp() below and the
 *               status comes back as an ordinary return value.  With no bail
 *               frame -- a standalone test driver, say -- it falls back to
 *               exit() and behaves as before.
 *
 *   signal()    SIGTERM/SIGINT handlers would displace yafu's own.  The siever
 *               installs them only when asked to with -n, and lasieve_run()
 *               sets the per-I lasieve_in_process flag that suppresses that, so
 *               -n keeps its other meaning (the thread number in the output
 *               name) without taking over the process.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "siever-config.h"

/* each of these is the renamed main() of objI<N>/gnfs-lasieve4e.o */
int mainI11(int argc, char **argv);
int mainI12(int argc, char **argv);
int mainI13(int argc, char **argv);
int mainI14(int argc, char **argv);
int mainI15(int argc, char **argv);
int mainI16(int argc, char **argv);

/* the per-I copies of the flag, renamed by the build.  One declaration each:
 * there is no way to write one name that reaches all six. */
extern int lasieve_in_processI11;
extern int lasieve_in_processI12;
extern int lasieve_in_processI13;
extern int lasieve_in_processI14;
extern int lasieve_in_processI15;
extern int lasieve_in_processI16;

/* The frame lasieve_bail() unwinds to.  Defined in lasieve_bail.c because the
 * standalone sieve regressions link that instead of this file; setjmp has to run
 * here, in the frame we return to. */
extern __thread jmp_buf lasieve_bail_frame;
extern __thread int lasieve_bail_armed;

int lasieve_run(int I, int argc, char **argv)
{
	int rc;

	/* the siever prints its own name from argv[0] in usage and errors; left
	 * alone it would say "yafu", so say what actually ran */
	if ((argc > 0) && (argv != NULL) && (argv[0] != NULL))
		argv[0] = (char *)"yafu lasieve";

	switch (I)
	{
	case 11: lasieve_in_processI11 = 1; break;
	case 12: lasieve_in_processI12 = 1; break;
	case 13: lasieve_in_processI13 = 1; break;
	case 14: lasieve_in_processI14 = 1; break;
	case 15: lasieve_in_processI15 = 1; break;
	case 16: lasieve_in_processI16 = 1; break;
	default:
		fprintf(stderr, "lasieve: sieve parameter must be 11 to 16, got %d\n", I);
		return 1;
	}

	lasieve_bail_armed = 1;
	rc = setjmp(lasieve_bail_frame);
	if (rc == 0)
	{
		lasieve_bail_armed = 0;
		switch (I)
		{
		case 11: rc = mainI11(argc, argv); break;
		case 12: rc = mainI12(argc, argv); break;
		case 13: rc = mainI13(argc, argv); break;
		case 14: rc = mainI14(argc, argv); break;
		case 15: rc = mainI15(argc, argv); break;
		case 16: rc = mainI16(argc, argv); break;
		}
	}
	lasieve_bail_armed = 0;
	return rc;
}
