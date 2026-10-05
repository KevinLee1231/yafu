/* asm-zeit —— 
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 三个计时入口，都读 rdtsc，没有别的状态。
 *
 * 汇编是 rdtsc 之后 shlq $32,%rdx / orq %rdx,%rax，也就是把 edx:eax 拼成
 * 64 位计数器；__rdtsc() 直接返回这 64 位，语义一致。
 *
 * zeitA / zeitB 读全局 asmzeitcounter（kernels/zeit.c 里定义的那一个，逐 I 值
 * 按 I 值分处各自的命名空间）。汇编里 movq asmzeitcounter(%rip),%rdx
 * 取的是指针值，再 subq/addq 到 (%rdx,%rdi,8)，即 asmzeitcounter[i] 上做加减。
 */

#include <stdint.h>
#include <x86intrin.h>

#include "siever-config.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef u64_t u64;
typedef ulong ul;

extern u64_t *asmzeitcounter;

ul asmgetclock(void)
{
	return (ul)__rdtsc();
}

/* 汇编：先减当前值，再按借位决定加不加上它 —— zeita 用减，zeitB 用加。 */
void zeitA(ul i)
{
	asmzeitcounter[i] -= (u64)__rdtsc();
}

void zeitB(ul i)
{
	asmzeitcounter[i] += (u64)__rdtsc();
}
}  /* namespace lasieve_ns */
