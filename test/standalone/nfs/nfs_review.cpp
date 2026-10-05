#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfs_impl.h"

void split_file(int nthreads, char *base_filename, const char *file_extension);
int tdiv_int(int x, int *factors, uint64_t *primes, uint64_t num_p);
void msieve_to_ggnfs(fact_obj_t *fobj, nfs_job_t *job);
void ggnfs_to_msieve(fact_obj_t *fobj, nfs_job_t *job);

void *xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if (ptr == NULL)
		exit(2);
	return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
	void *result = realloc(ptr, size);
	if (result == NULL)
		exit(2);
	return result;
}

static int count_lines(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[64];
	int count = 0;
	if (fp == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL)
		count++;
	fclose(fp);
	return count;
}

static int test_split_file(const char *dir)
{
	char base[512];
	char path[544];
	FILE *fp;

	snprintf(base, sizeof(base), "%s/split", dir);
	snprintf(path, sizeof(path), "%s.txt", base);
	fp = fopen(path, "w");
	if (fp == NULL)
		return 1;
	fputs("1\n2\n3\n4\n5\n", fp);
	fclose(fp);
	split_file(2, base, "txt");
	snprintf(path, sizeof(path), "%s.0.txt", base);
	if (count_lines(path) != 2)
		return 1;
	snprintf(path, sizeof(path), "%s.1.txt", base);
	return count_lines(path) == 3 ? 0 : 1;
}

static int test_ranges(const char *dir)
{
	fact_obj_t fobj;
	nfs_job_t job;
	qrange_data_t *ranges;
	char path[544];
	FILE *fp;
	int failed;

	memset(&fobj, 0, sizeof(fobj));
	memset(&job, 0, sizeof(job));
	snprintf(fobj.nfs_obj.outputfile, sizeof(fobj.nfs_obj.outputfile),
		"%s/ranges", dir);
	job.qrange = 100;
	snprintf(path, sizeof(path), "%s.ranges", fobj.nfs_obj.outputfile);
	fp = fopen(path, "w");
	if (fp == NULL)
		return 1;
	fputs("r,100,20,7\n", fp);
	fputs("x,200,20,99\n", fp);
	fputs("a,4294967290,20,99\n", fp);
	fputs("bad relation line\n", fp);
	fputs("a,300,40,11\n", fp);
	fclose(fp);

	ranges = sort_completed_ranges(&fobj, &job);
	if (ranges == NULL)
		return 1;
	failed = ranges->num_r != 1 || ranges->num_a != 1 ||
		ranges->qranges_r[0].qrange_start != 100 ||
		ranges->qranges_r[0].qrange_end != 120 ||
		ranges->qranges_a[0].qrange_start != 300 ||
		ranges->qranges_a[0].qrange_end != 340 || job.current_rels != 18;
	free(ranges->qranges_r);
	free(ranges->qranges_a);
	free(ranges);
	return failed;
}

static int test_trial_bounds(void)
{
	uint64_t primes[1] = {2};
	int factors[8] = {0};
	mpz_t value;
	int count;

	if (tdiv_int(49, factors, primes, 0) != 0)
		return 1;
	mpz_init_set_ui(value, 7);
	count = tdiv_mpz(value, factors, primes, 0);
	mpz_clear(value);
	if (count != 1 || factors[0] != 7)
		return 1;
	mpz_init_set_ui(value, 14);
	count = tdiv_mpz(value, factors, primes, 1);
	mpz_clear(value);
	return (count == 2 && factors[0] == 2 && factors[1] == 7) ? 0 : 1;
}

static int test_poly_file_conversion(const char *dir)
{
	fact_obj_t fobj;
	nfs_job_t job;
	char line[2048];
	FILE *fp;
	size_t i;

	memset(&fobj, 0, sizeof(fobj));
	memset(&job, 0, sizeof(job));
	snprintf(fobj.nfs_obj.fbfile, sizeof(fobj.nfs_obj.fbfile),
		"%s/msieve.fb", dir);
	snprintf(fobj.nfs_obj.job_infile, sizeof(fobj.nfs_obj.job_infile),
		"%s/ggnfs.job", dir);
	fp = fopen(fobj.nfs_obj.fbfile, "w");
	if (fp == NULL)
		return 1;
	fputs("N ", fp);
	for (i = 0; i < 1020; i++)
		fputc('7', fp);
	fputc('\n', fp);
	fclose(fp);

	msieve_to_ggnfs(&fobj, &job);
	fp = fopen(fobj.nfs_obj.job_infile, "r");
	if (fp == NULL || fgets(line, sizeof(line), fp) == NULL)
		return 1;
	fclose(fp);
	if (strlen(line) != 1024 || strncmp(line, "n: ", 3) != 0 ||
		line[1023] != '\n')
		return 1;

	snprintf(fobj.nfs_obj.job_infile, sizeof(fobj.nfs_obj.job_infile),
		"%s/linear.job", dir);
	snprintf(fobj.nfs_obj.fbfile, sizeof(fobj.nfs_obj.fbfile),
		"%s/linear.fb", dir);
	fp = fopen(fobj.nfs_obj.job_infile, "w");
	if (fp == NULL)
		return 1;
	fputs("m: ", fp);
	for (i = 0; i < 1019; i++)
		fputc('8', fp);
	fputc('\n', fp);
	fclose(fp);

	ggnfs_to_msieve(&fobj, &job);
	fp = fopen(fobj.nfs_obj.fbfile, "r");
	if (fp == NULL || fgets(line, sizeof(line), fp) == NULL ||
		strcmp(line, "R1 1\n") != 0 || fgets(line, sizeof(line), fp) == NULL)
		return 1;
	fclose(fp);
	return strlen(line) == 1024 && strncmp(line, "R0 -", 4) == 0 &&
		line[1023] == '\n' ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (test_split_file(argv[1]) || test_ranges(argv[1]) ||
		test_trial_bounds() || test_poly_file_conversion(argv[1]))
		return 1;
	puts("nfs review checks passed");
	return 0;
}
