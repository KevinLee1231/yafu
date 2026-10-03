/*
MIT License

Copyright (c) 2021 Ben Buhrow

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "soe.h"
#include "ytools.h"
#include <string.h>
#include <math.h>

uint32_t tiny_soe(uint32_t limit, uint32_t *primes)
{
	//simple sieve of erathosthenes for small limits - not efficient
	//for large limits.
	uint8_t *flags;
	uint32_t prime;
	uint32_t i,j;
	int it = 0;
	uint32_t flag_count = limit / 2;

	if (limit > 2)
	{
		primes[it++] = 2;
	}
	if (flag_count <= 1)
	{
		return (uint32_t)it;
	}

	//allocate flags
	flags = (uint8_t *)xmalloc(flag_count * sizeof(uint8_t));
	memset(flags, 1, flag_count);

	//find the sieving primes, don't bother with offsets, we'll need to find those
	//separately for each line in the main sieve.
	//sieve using primes less than the sqrt of the desired limit
	//flags are created only for odd numbers (mod2)
	for (i = 1; i < flag_count && (2 * i + 1) <= (limit - 1) / (2 * i + 1); i++)
	{
		if (flags[i] > 0)
		{
			prime = (uint32_t)(2*i + 1);
			for (j = i + prime; j < flag_count; j += prime)
				flags[j]=0;

			primes[it]=prime;
			it++;
		}
	}

	//now find the rest of the prime flags and compute the sieving primes
	for (; i < flag_count; i++)
	{
		if (flags[i] == 1)
		{
			primes[it] = (uint32_t)(2*i + 1);
			it++;
		}
	}

	free(flags);
	return it;
}
