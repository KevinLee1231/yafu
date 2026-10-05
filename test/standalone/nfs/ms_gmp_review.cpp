#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ms_gmp_xface.h"

static int test_int64_min_conversion(void)
{
	mpz_t value;
	char *text;
	int failed;

	mpz_init(value);
	int64_2gmp(INT64_MIN, value);
	text = mpz_get_str(NULL, 10, value);
	failed = text == NULL || strcmp(text, "-9223372036854775808") != 0 ||
		gmp2int64(value) != INT64_MIN;
	free(text);
	mpz_clear(value);
	return failed;
}

static int child_rejected(pid_t child)
{
	int status;
	if (waitpid(child, &status, 0) != child)
		return 0;
	return WIFEXITED(status) && WEXITSTATUS(status) != 0;
}

static int test_gmp2mp_capacity(void)
{
	pid_t child = fork();
	if (child < 0)
		return 1;
	if (child == 0) {
		mpz_t value;
		mp_t fixed;
		(void)freopen("/dev/null", "w", stderr);
		mpz_init_set_ui(value, 1);
		mpz_mul_2exp(value, value, 1024);
		gmp2mp(value, &fixed);
		_exit(0);
	}
	return child_rejected(child) ? 0 : 1;
}

static int test_gmp2int64_range(void)
{
	pid_t child = fork();
	if (child < 0)
		return 1;
	if (child == 0) {
		mpz_t value;
		(void)freopen("/dev/null", "w", stderr);
		mpz_init_set_ui(value, 1);
		mpz_mul_2exp(value, value, 64);
		(void)gmp2int64(value);
		_exit(0);
	}
	return child_rejected(child) ? 0 : 1;
}

static int test_mp_d2mp_conversion(void)
{
	double input = 0x1.0000000000001p+100;
	mp_t fixed;
	mpz_t actual, expected;
	int failed;

	mp_d2mp(&input, &fixed);
	mpz_init(actual);
	mpz_init_set_ui(expected, 1);
	mpz_mul_2exp(expected, expected, 100);
	mpz_setbit(expected, 48);
	mp2gmp(&fixed, actual);
	failed = mpz_cmp(actual, expected) != 0;
	mpz_clear(actual);
	mpz_clear(expected);
	return failed;
}

int main(void)
{
	if (test_int64_min_conversion() || test_gmp2mp_capacity() ||
		test_gmp2int64_range() || test_mp_d2mp_conversion())
		return 1;
	puts("ms gmp conversion checks passed");
	return 0;
}
