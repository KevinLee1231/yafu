/* rescale —— 筛区间/横向和的收缩。
 *
 * 由 rescale.asm 翻译而来（Copyright (C) 2002,2004 Jens Franke,
 * T.Kleinjung，gnfs4linux，GPL）。原文件是 m4 模板，向量化部分只用
 * pavgb 一条指令：_mm_avg_epu8 就是 (a + b + 1) >> 1 的逐字节形式。
 *
 *   rescale_interval1: array[i] = (array[i] + 1) / 2
 *   rescale_interval2: array[i] = (array[i] + 3) / 4
 */

#include <immintrin.h>
#include <stdint.h>

#include "siever-config.h"

/* 一次收缩 length 个字节。delta1=1 对应 (x+1)/2，delta1=0 时先做一次
 * (x+1)/2 再做一次，等价于 (x+3)/4 —— 汇编里 rescale_interval2 也是
 * 连着两条 pavgb。 */
static void rescale_bytes(unsigned char *array, uint64_t length, int twice)
{
	const __m128i one = _mm_set1_epi8(1);
	uint64_t n16 = length >> 4;

	for (uint64_t i = 0; i < n16; i++) {
		__m128i x = _mm_loadu_si128((const __m128i *)(array + (i << 4)));
		x = _mm_avg_epu8(x, one);
		if (twice)
			x = _mm_avg_epu8(x, one);
		_mm_storeu_si128((__m128i *)(array + (i << 4)), x);
	}

	array += n16 << 4;
	length &= 15;
	for (uint64_t i = 0; i < length; i++) {
		unsigned v = (unsigned)array[i] + 1;
		v >>= 1;
		if (twice) {
			v += 1;
			v >>= 1;
		}
		array[i] = (unsigned char)v;
	}
}

void rescale_interval1(unsigned char *array, u64_t length)
{
	rescale_bytes(array, length, 0);
}

void rescale_interval2(unsigned char *array, u64_t length)
{
	rescale_bytes(array, length, 1);
}
