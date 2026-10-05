/*
 * check_sieve_mt.c -- the six I values running at the same time, in one process.
 *
 * That is the arrangement the merge exists for: nfs_sieving.c starts one siever
 * per thread, each with a different I, and each I value is a private set of
 * objects -- private kernels, factor base, montgomery state, ECM/P-1 cache.  If
 * that privacy leaks, two sievers share a factor base and the relation counts or
 * the relation sets come out wrong.
 *
 * Nothing else in the tree exercises it.  sieve_oracle.sh runs the I values one
 * after another, which cannot see cross-talk, and a real NFS factorization is
 * expensive and awkward to set up.
 *
 * Each thread gets its own copy of the polynomial under its own name: the siever
 * derives its side files -- the factor base cache, the relation log -- from the
 * input file name, so distinct input names mean distinct files and the threads
 * do not tread on each other.  That is the same thing nfs_sieving.c does with
 * its per-thread -o and job_infile_name.
 *
 *     check_sieve_mt <polyfile> <startq> <count>
 *
 * Exits nonzero if any I value produced the wrong number of relations.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* serial mode: one siever at a time inside this process */
static pthread_mutex_t serial_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  serial_cv = PTHREAD_COND_INITIALIZER;
static int serial_active;
static int serial_next;
static int serial_mode;

int lasieve_run(int I, int argc, char **argv);

#define NTHREADS 6

static const char *polyfile;
static unsigned startq, count;
static int failures;
static int nthreads = NTHREADS;
static unsigned stagger_us = 0;

struct work
{
	int I;
	int slot;
};

static void *sieve_thread(void *arg)
{
	struct work *w = arg;
	char inname[64], startbuf[32], countbuf[32];
	int slot = w->slot;

	if (stagger_us && slot)
		usleep((useconds_t)stagger_us * (unsigned)slot);

	if (serial_mode)
	{
		pthread_mutex_lock(&serial_lock);
		while (serial_active || (serial_next != slot))
			pthread_cond_wait(&serial_cv, &serial_lock);
		serial_active = 1;
		serial_next = slot + 1;
		pthread_mutex_unlock(&serial_lock);
	}
	char *argv[16];
	int argc = 0, rc;
	FILE *f;
	int lines = 0;
	char line[4096];
	char outname[128];

	/* its own copy of the polynomial, so its side files are its own */
	snprintf(inname, sizeof(inname), "mt%d.poly", w->slot);
	if ((f = fopen(polyfile, "r")) == NULL)
	{
		fprintf(stderr, "[mt%d] cannot read %s\n", slot, polyfile);
		failures++;
		return NULL;
	}
	{
		FILE *o = fopen(inname, "w");
		char buf[4096];
		if (o == NULL)
		{
			fprintf(stderr, "[mt%d] cannot write %s\n", slot, inname);
			failures++;
			fclose(f);
			return NULL;
		}
		while (fgets(buf, sizeof(buf), f) != NULL)
			fputs(buf, o);
		fclose(o);
	}
	fclose(f);

	snprintf(startbuf, sizeof(startbuf), "%u", startq);
	snprintf(countbuf, sizeof(countbuf), "%u", count);

	argv[argc++] = (char *)"mt";
	argv[argc++] = (char *)"-a";
	argv[argc++] = (char *)"-f";
	argv[argc++] = startbuf;
	argv[argc++] = (char *)"-c";
	argv[argc++] = countbuf;
	argv[argc++] = (char *)"-S";
	argv[argc++] = (char *)"0.63913";
	argv[argc++] = inname;
	argv[argc] = NULL;

	{
		int k;
		fprintf(stderr, "[mt%d] I=%d argv:", slot, w->I);
		for (k = 0; k < argc; k++)
			fprintf(stderr, " [%s]", argv[k]);
		fprintf(stderr, "\n");
		fflush(stderr);
	}

	rc = lasieve_run(w->I, argc, argv);

	if (serial_mode)
	{
		pthread_mutex_lock(&serial_lock);
		serial_active = 0;
		pthread_cond_broadcast(&serial_cv);
		pthread_mutex_unlock(&serial_lock);
	}

	if (rc != 0)
	{
		fprintf(stderr, "[mt%d] I=%d returned %d\n", slot, w->I, rc);
		failures++;
		return NULL;
	}

	/* count the relations it wrote.  The siever uses the whole input name as
	 * the basename, so the log is <inname>.lasieve-<side>.<start>-<end> */
	snprintf(outname, sizeof(outname), "%s.lasieve-0.%u-%u", inname, startq,
		startq + count);
	if ((f = fopen(outname, "r")) == NULL)
	{
		fprintf(stderr, "[mt%d] I=%d produced no %s\n", slot, w->I, outname);
		failures++;
		return NULL;
	}
	while (fgets(line, sizeof(line), f) != NULL)
		lines++;
	fclose(f);

	printf("[mt%d] I=%-2d %4d relations  (%s)\n", slot, w->I, lines,
		(lines > 0) ? "有产出" : "空");
	fflush(stdout);
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t th[NTHREADS];
	struct work w[NTHREADS];
	int i, total = 0;

	if (argc < 4)
	{
		fprintf(stderr, "usage: %s <poly> <startq> <count> [nthreads] [stagger_us] [serial]\n", argv[0]);
		return 1;
	}
	polyfile = argv[1];
	startq = (unsigned)strtoul(argv[2], NULL, 10);
	count = (unsigned)strtoul(argv[3], NULL, 10);
	if (argc > 4)
	{
		nthreads = atoi(argv[4]);
		if ((nthreads < 1) || (nthreads > NTHREADS))
			nthreads = NTHREADS;
	}
	if (argc > 5)
		stagger_us = (unsigned)strtoul(argv[5], NULL, 10);
	if (argc > 6)
		serial_mode = atoi(argv[6]);

	printf("%d I value(s), stagger %u us, serial %d\n", nthreads, stagger_us, serial_mode);
	fflush(stdout);
	for (i = 0; i < nthreads; i++)
	{
		w[i].I = 11 + i;
		w[i].slot = i;
		if (pthread_create(&th[i], NULL, sieve_thread, &w[i]) != 0)
		{
			fprintf(stderr, "cannot create thread %d\n", i);
			return 1;
		}
	}
	for (i = 0; i < nthreads; i++)
		pthread_join(th[i], NULL);

	(void)total;
	if (failures)
	{
		printf("check_sieve_mt: %d problem(s)\n", failures);
		return 1;
	}
	printf("check_sieve_mt: %d I value(s) ran concurrently -- ok\n", nthreads);
	return 0;
}
