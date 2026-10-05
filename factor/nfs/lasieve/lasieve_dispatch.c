/*
lasieve_dispatch.c -- picks the I value for a siever run.

The six I values used to be six executables because their assembly
libraries define the same symbols and cannot be linked together.  They are
now six sets of private objects in one image, so the entry point has to say
which one it wants.

Usage:
    gnfs-lasieve4e <I> [siever options ...]

I is one of 11..16.  Everything after it is the siever's own command line,
unparsed and untouched -- the per-I copies of gnfs-lasieve4e.c each keep
their getopt loop, they just answer to a different name.
*/

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

int lasieve_run(int I, int argc, char **argv)
{
  switch (I)
  {
    case 11: return mainI11(argc, argv);
    case 12: return mainI12(argc, argv);
    case 13: return mainI13(argc, argv);
    case 14: return mainI14(argc, argv);
    case 15: return mainI15(argc, argv);
    case 16: return mainI16(argc, argv);
    default: return -1;
  }
}

static void usage(const char *me)
{

  fprintf(stderr, "usage: %s <I> [options ...]\n", me);
  fprintf(stderr, "  <I> picks the sieve parameter, 11 to 16\n");
  fprintf(stderr, "  the options after it are the siever's own\n");
}

int main(int argc, char **argv)
{
  int I;
  char* end;

  if (argc < 2)
  {
    usage(argv[0]);
    return 1;
  }

  if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
  {
    usage(argv[0]);
    return 0;
  }

  I = (int)strtol(argv[1], &end, 10);
  if ((end == argv[1]) || (*end != '\0') || (I < 11) || (I > 16))
  {
    fprintf(stderr, "%s: sieve parameter must be 11 to 16, got '%s'\n",
            argv[0], argv[1]);
    usage(argv[0]);
    return 1;
  }

  /* drop the I value and hand the rest to the siever unchanged; argv[0] is
   * replaced so that usage messages still name this program */
  argv[1] = argv[0];
  return lasieve_run(I, argc - 1, argv + 1);
}
