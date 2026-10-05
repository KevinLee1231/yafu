/*
 * check_sieve.c -- standalone driver for the sieve tests.
 *
 * The siever lives inside yafu now and is reached through lasieve_run() from
 * nfs_sieving.c.  The sieve oracle needs to run it by itself, to compare the
 * relation output of each I value against the recorded set, so this driver
 * links the same objects into a throwaway program.
 *
 * It is not part of `make all`: the shipped artefact is yafu.  test_lasieve.sh
 * builds it into a temporary directory and throws it away.
 *
 *     check_sieve <I> [siever options ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "siever-asm.h"

int lasieve_run(int I, int argc, char **argv);

int main(int argc, char **argv)
{
	char *end;
	long I;

	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <I> [options ...]\n", argv[0]);
		fprintf(stderr, "  <I> picks the sieve parameter, 11 to 16\n");
		return 1;
	}

	if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
	{
		fprintf(stderr, "usage: %s <I> [options ...]\n", argv[0]);
		return 0;
	}

	I = strtol(argv[1], &end, 10);
	if ((end == argv[1]) || (*end != '\0') || (I < 11) || (I > 16))
	{
		fprintf(stderr, "%s: sieve parameter must be 11 to 16, got '%s'\n",
				argv[0], argv[1]);
		return 1;
	}

	/* drop the I value; lasieve_run() names itself from argv[0] */
	argv[1] = argv[0];
	return lasieve_run((int)I, argc - 1, argv + 1);
}
