/* (Copyright (C) 2001 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 把一段偏移表里的偏移逐个累加到筛数组上：sieve[offsets[i]] += val。
 * 汇编版每次展开 4 个偏移，主循环走完后还有最多 3 个收尾。
 *
 * 三处必须照抄的地方：
 *
 * 1. **第四个参数是指针不是长度**。声明是
 *    `void schedsieve(unsigned char,unsigned char*,u16_t*,u16_t*)`，
 *    调用点（gnfs-lasieve4e.cpp:4112）传的是 `med_sched[s][l+1]`，也就是
 *    **下一段的起点**。所以 `subq $12,%rcx` 是「下一段起点减 12 字节」，
 *    循环里 `cmpq %rdx,%rcx` 是拿长度和指针比，不能写成 `for (i = 0;
 *    i < n; i++)`。
 *
 * 2. **先推进指针、后判断**。循环尾和收尾段都是
 *        cmpq %rdx,%rcx ; leaq N(%rdx),%rdx ; jbe/jnc ...
 *    判断用的是**推进前**的指针。写成 `if (ub <= p) break; p += 16;` 在边界
 *    上会差一次。下面的代码把判断显式写成 `ub > p - 16` / `ub > p` 就是为了
 *    保住这个顺序。
 *
 * 3. **r11 延迟一轮**。主循环里用的是
 *        addb %al,(%rsi,%r8) ; movzwq (%rdx),%r8
 *        addb %al,(%rsi,%r9) ; movzwq 4(%rdx),%r9
 *        addb %al,(%rsi,%r10); movzwq 8(%rdx),%r10
 *        addb %al,(%rsi,%r11)          <-- 这里用的是上一轮载入的 r11
 *        cmpq %rdx,%rcx
 *        movzwq 12(%rdx),%r11          <-- 载入推迟到判断之前
 *        leaq 16(%rdx),%rdx
 *        ja  fat_loop
 *    所以循环退出时 r11 已经是新一轮的值（movzwq 在 ja 之前），收尾段能直接用。
 *
 * `prefetcht0 128(%rdx)` 是纯预取，C++ 里没有对应，忽略。
 */

#include <stdint.h>

#include "siever-config.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef u16_t u16;
typedef unsigned char u8;

void schedsieve(u8 val, u8 *sieve, u16 *offsets, u16 *next)
{
	const u8 al = val;
	const u8 *ub = (const u8 *)next - 12;      /* 汇编：subq $12,%rcx */
	const u8 *p = (const u8 *)offsets;
	u16 r8, r9, r10, r11;

	r8  = *(const u16 *)(p + 0);
	r9  = *(const u16 *)(p + 4);
	r10 = *(const u16 *)(p + 8);
	r11 = *(const u16 *)(p + 12);
	p += 16;

	if (ub > p - 16) {
		for (;;) {
			sieve[r8] += al;
			r8  = *(const u16 *)(p + 0);
			sieve[r9] += al;
			r9  = *(const u16 *)(p + 4);
			sieve[r10] += al;
			r10 = *(const u16 *)(p + 8);
			sieve[r11] += al;
			{
				/* cmpq 在 movzwq / leaq 之前，判的是推进前的 p */
				const int more = (ub > p);
				r11 = *(const u16 *)(p + 12);
				p += 16;
				if (!more)
					break;
			}
		}
	}

	/* 收尾段：最多再做三个偏移，每做一次前先推进指针并用推进前的值判断 */
	ub += 28;
	p += 4;
	if (ub <= p - 4)
		return;
	sieve[r8] += al;
	p += 4;
	if (ub <= p - 4)
		return;
	sieve[r9] += al;
	if (ub <= p)
		return;
	sieve[r10] += al;
}

/* schedsieve_1：同样的结构，但每次前进 2 个 u16（4 个字节）。
 * 入口是 subq $6 而不是 $12，收尾是 addq $14、每步 2 字节。 */
void schedsieve_1(u8 val, u8 *sieve, u16 *offsets, u16 *next)
{
	const u8 al = val;
	const u8 *ub = (const u8 *)next - 6;       /* 汇编：subq $6,%rcx */
	const u8 *p = (const u8 *)offsets;
	u16 r8, r9, r10, r11;

	r8  = *(const u16 *)(p + 0);
	r9  = *(const u16 *)(p + 2);
	r10 = *(const u16 *)(p + 4);
	r11 = *(const u16 *)(p + 6);
	p += 8;

	if (ub > p - 8) {
		for (;;) {
			sieve[r8] += al;
			r8  = *(const u16 *)(p + 0);
			sieve[r9] += al;
			r9  = *(const u16 *)(p + 2);
			sieve[r10] += al;
			r10 = *(const u16 *)(p + 4);
			sieve[r11] += al;
			{
				const int more = (ub > p);
				r11 = *(const u16 *)(p + 6);
				p += 8;
				if (!more)
					break;
			}
		}
	}

	ub += 14;
	p += 2;
	if (ub <= p - 2)
		return;
	sieve[r8] += al;
	p += 2;
	if (ub <= p - 2)
		return;
	sieve[r9] += al;
	if (ub <= p)
		return;
	sieve[r10] += al;
}
}  /* namespace lasieve_ns */
