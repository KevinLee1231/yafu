/* tdslinie2 —— 由 factor/nfs/lasieve/asm/tdslinie2.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 「时间除数筛」内核的一个变体。签名（照抄 asm/include/siever-config.h）：
 *     u32_t *tdslinie2(u16_t *aux_ptr, u16_t *aux_ptr_ub,
 *                      unsigned char *sieve_interval, u32_t **tds_buffer);
 * aux_ptr / aux_ptr_ub 都是 u16_t 指针，每条线占 4 个元素：
 * aux[0]=prime, aux[1]=proot_src, aux[3]=root，aux_ptr += 4（8 字节）。
 *
 * ============================ 结构 ============================
 * 与 tdslinie1 同为「扫描趟 + 干活趟」两趟结构，但两趟的字节读法不同：
 *   扫描趟每个 j 读 TD_AT(sieve_ptr)、TD_AT(sieve_ptr + prime)、
 *            TD_AT(sieve_ptr + 2*prime) 三个字节，跨 j 累积进 sv0；
 *   干活趟每个 j 读 TD_AT(sieve_ptr)、TD_AT(sieve_ptr + prime)，再前移 2*prime 后
 *            **有条件**读新的 TD_AT(sieve_ptr)，每 j 重新赋值 sv0。
 *
 * ============================ 必须照抄的语义 ============================
 *
 * 1) **返回值是垃圾，不要用。** 汇编把 %rax 当 tds_buffer2 临时寄存器。
 *    调用点也丢弃返回值。故返回 NULL，差分测试**不比较返回值**。
 *
 * 2) 入口**没有** tdslinie1 那条 `leaq -8(tds_buffer),tds_buffer`。
 *    干活趟里是 `decq auxreg` 后 `leaq (tds_buffer,auxreg,8),auxreg`，
 *    即索引 `tds_buffer[val-1]`。不要照抄 tdslinie1 的整体前移写法。
 *
 * 3) 扫描趟里这条：
 *        orb (sieve_ptr,prime,2),sv0
 *        jbe tdslinie2_next_j
 *    顺序是 **orb 在 jbe 之前** —— 也就是 `TD_AT(sieve_ptr + 2*prime)`
 *    这个字节是**无条件读**的，后面的 `cmpq`/`jbe` 什么都没保护。
 *    这看起来像笔误（本意大概是想把它放进条件里），但它就是原样：照抄。
 *    写成有条件会改变 sv0，从而改变哪些线进入干活趟。
 *    `jbe` 的目标标签 `tdslinie2_next_j` 就紧挨着 jbe 本身，等于跳到
 *    下一条指令，是条死跳转 —— 所以 orb 事实上无条件执行。
 *    这个多出来的字节要求差分测试给 sieve_interval 留足尾部填充。
 *
 * 4) 干活趟没有 tdslinie 里那套 lasttesta/lasttestb 尾巴，只有一次条件读：
 *        leaq (sieve_ptr,prime,2),sieve_ptr   ← 无条件前移
 *        cmpq sieve_ptr,sieve_ptr_ub ; jbe skip
 *        orb (sieve_ptr),sv0
 *    前移在比较之前，所以比较的是**前移后**的指针。
 *
 * 5) 干活趟命中后 `subq prime,sieve_ptr` **两次**（共 -2*prime），然后连续做
 *    三处写桶检查。第一、二处都是「先 leaq 前移 prime 再判」，所以即使
 *    val==0 也会前移；第三处前移到下一个位置后再判，且把 sieve_ptr 寄存器
 *    复用成 tds_buffer 临时值。三处依次看的是 起点-2p、起点-p、起点。
 *    这是「先做后判」，顺序不能调。
 *
 * 6) 射影根推进同 tdslinie：root += (u64)(aux[1]-prime)，无进位再加 prime。
 *    `cmovncq` 判的进位来自 `addq %rbx,%r10`，中间的 `leaq` 不改标志位。
 *
 * 7) **sieve_ptr_ub 在两趟里都净增 n_i**（扫描趟没有任何 subq prime，
 *    干活趟也没有）。这与 tdslinie 的 -5*prime 不同，别抄错。
 *
 * 8) 三处循环边界都是无符号指针比较。入口 `cmpq %rdi,%rsi ; jbe` 用**原始**
 *    aux_ptr_ub；回边的 cmpq 在 `leaq 8(aux_ptr),aux_ptr` **之前**，
 *    判的是自增前的 aux_ptr，上界是减过 8 字节的。
 */

#include "td_common.h"


#include "lasieve_ns.h"

namespace lasieve_ns {
u32_t *tdslinie2(u16_t *, u16_t *, unsigned char *, u32_t **);


u32_t *
tdslinie2(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval,
          u32_t **tds_buffer)
{
	if ((uintptr_t)aux_ptr_ub <= (uintptr_t)aux_ptr)
		return NULL;

	u16_t *const ub = aux_ptr_ub - 4;          /* subq $8 = 8 字节 */

	for (;;) {
		u64_t root;

		/* ---- 扫描趟（tdslinie2_fbi_loop） ---- */
		{
			const u64_t proot = (u64_t)aux_ptr[1] - (u64_t)aux_ptr[0];
			const u64_t prime = (u64_t)aux_ptr[0];
			root = (u64_t)aux_ptr[3];

			uintptr_t      sieve_ptr_ub = (uintptr_t)sieve_interval;
			uint8_t        sv0          = 0;   /* 跨 j 累积 */

			for (int j = 0; j < TD_J_PER_STRIP; j++) {
				/* root 的数值：在 leaq 之前还不能当指针用 */
				uintptr_t      sp       = (uintptr_t)root;
				uintptr_t      sieve_ptr;
				u64_t          auxreg   = 0;

				{
					const u64_t t  = root + proot;
					const bool  cf = (t < root);
					root = t;
					if (!cf)
						auxreg = prime;
				}
				sieve_ptr = sieve_ptr_ub + sp;

				sieve_ptr_ub += TD_NI;
				root += auxreg;

				sv0 |= (uint8_t)TD_AT(sieve_ptr);
				sv0 |= (uint8_t)TD_AT(sieve_ptr + prime);

				/* orb 在 jbe 之前 —— 无条件读，见第 3 条。 */
				sv0 |= (uint8_t)TD_AT(sieve_ptr + 2 * prime);
			}

			if (sv0 == 0)
				goto nextfbi;
		}

		/* ---- 干活趟（tdslinie2_suche） ---- */
		{
			const u64_t proot = (u64_t)aux_ptr[1] - (u64_t)aux_ptr[0];
			const u64_t prime = (u64_t)aux_ptr[0];
			root = (u64_t)aux_ptr[3];   /* 重新读 */

			uintptr_t      sieve_ptr_ub = (uintptr_t)sieve_interval;

			for (int j = 0; j < TD_J_PER_STRIP; j++) {
				/* root 的数值：在 leaq 之前还不能当指针用 */
				uintptr_t      sp       = (uintptr_t)root;
				uintptr_t      sieve_ptr;
				u64_t          auxreg   = 0;

				{
					const u64_t t  = root + proot;
					const bool  cf = (t < root);
					root = t;
					if (!cf)
						auxreg = prime;
				}
				sieve_ptr = sieve_ptr_ub + sp;

				sieve_ptr_ub += TD_NI;
				root += auxreg;

				uint8_t sv0 = (uint8_t)(TD_AT(sieve_ptr) | TD_AT(sieve_ptr + prime));

				/* 前移在比较之前 —— 见第 4 条。 */
				sieve_ptr += 2 * prime;
				if (sieve_ptr_ub > sieve_ptr)
					sv0 |= (uint8_t)TD_AT(sieve_ptr);

				if (sv0 == 0)
					continue;   /* tdslinie2s_next_j */

				/* 两次 subq prime，共 -2*prime —— 见第 5 条。 */
				sieve_ptr -= 2 * prime;

				/* 第一处 */
				{
					const u64_t v = TD_AT(sieve_ptr);
					sieve_ptr += prime;
					if (v != 0) {
						u32_t **slot = &tds_buffer[v - 1];
						*(*slot)++ = (u32_t)prime;
					}
				}

				/* 第二处 */
				{
					const u64_t v = TD_AT(sieve_ptr);
					sieve_ptr += prime;
					if (v != 0) {
						u32_t **slot = &tds_buffer[v - 1];
						*(*slot)++ = (u32_t)prime;
					}
				}

				/* 第三处：sieve_ptr 寄存器被复用成临时。 */
				if ((uintptr_t)sieve_ptr < (uintptr_t)sieve_ptr_ub) {
					const u64_t v = TD_AT(sieve_ptr);
					if (v != 0) {
						u32_t **slot = &tds_buffer[v - 1];
						*(*slot)++ = (u32_t)prime;
					}
				}
			}
		}

	nextfbi:
		aux_ptr[3] = (u16_t)root;
		{
			const bool again = (uintptr_t)ub > (uintptr_t)aux_ptr;
			aux_ptr += 4;
			if (!again)
				break;
		}
	}

	return NULL;   /* 见第 1 条 */
}
}  /* namespace lasieve_ns */
