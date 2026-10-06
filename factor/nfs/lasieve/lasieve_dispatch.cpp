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
#include "siever-asm.h"

/* objI<N>/gnfs-lasieve4e.o puts its main() and its lasieve_in_process flag in
 * namespace lasieve_I<N>, and this file is compiled once, outside every one of
 * them.  So each pair has to be declared under its own namespace qualifier --
 * there is no way to write one declaration that reaches all six. */
namespace lasieve_I11 { int main(int, char **); extern int lasieve_in_process; }
namespace lasieve_I12 { int main(int, char **); extern int lasieve_in_process; }
namespace lasieve_I13 { int main(int, char **); extern int lasieve_in_process; }
namespace lasieve_I14 { int main(int, char **); extern int lasieve_in_process; }
namespace lasieve_I15 { int main(int, char **); extern int lasieve_in_process; }
namespace lasieve_I16 { int main(int, char **); extern int lasieve_in_process; }

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
	case 11: lasieve_I11::lasieve_in_process = 1; break;
	case 12: lasieve_I12::lasieve_in_process = 1; break;
	case 13: lasieve_I13::lasieve_in_process = 1; break;
	case 14: lasieve_I14::lasieve_in_process = 1; break;
	case 15: lasieve_I15::lasieve_in_process = 1; break;
	case 16: lasieve_I16::lasieve_in_process = 1; break;
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
		case 11: rc = lasieve_I11::main(argc, argv); break;
		case 12: rc = lasieve_I12::main(argc, argv); break;
		case 13: rc = lasieve_I13::main(argc, argv); break;
		case 14: rc = lasieve_I14::main(argc, argv); break;
		case 15: rc = lasieve_I15::main(argc, argv); break;
		case 16: rc = lasieve_I16::main(argc, argv); break;
		}
	}
	lasieve_bail_armed = 0;
	return rc;
}
