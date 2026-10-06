/* tdslinie3 —— 由 factor/nfs/lasieve/asm/tdslinie3.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 「时间除数筛」内核的一个变体。签名（照抄 asm/include/siever-config.h）：
 *     u32_t *tdslinie3(u16_t *aux_ptr, u16_t *aux_ptr_ub,
 *                      unsigned char *sieve_interval, u32_t **tds_buffer);
 * aux_ptr / aux_ptr_ub 都是 u16_t 指针，每条线占 4 个元素：
 * aux[0]=prime, aux[1]=proot_src, aux[3]=root，aux_ptr += 4（8 字节）。
 *
 * ============================ 结构 ============================
 * tdslinie3 是**单趟**结构（tdslinie1/2 才有扫描趟），每个 j 内：
 *   读 4 个字节 → 命中则回退 2*prime → 向前走一条命中报告链。
 * 它没有 tdslinie 的 lasttesta/lasttestb 双段尾巴，也没有 tdslinie2
 * 扫描趟那条无条件多读的 `TD_AT(sieve_ptr + 2*prime)`。
 *
 * ============================ 必须照抄的语义 ============================
 *
 * 1) **返回值是垃圾，不要用。** 汇编把 %rax 当 tds_buffer2 临时寄存器。
 *    调用点也丢弃返回值。故返回 NULL，差分测试**不比较返回值**。
 *
 * 2) 入口没有整体前移 tds_buffer 的写法；干活时是
 *    `decq auxreg ; leaq (tds_buffer,auxreg,8),auxreg`，
 *    即索引 `tds_buffer[val-1]`。
 *
 * 3) 射影根推进同 tdslinie：root += (u64)(aux[1]-prime)，无进位再加 prime。
 *    `cmovncq` 判的进位来自 `addq %rbx,%r10`，中间的 `leaq` 不改标志位。
 *
 * 4) **sieve_ptr_ub 每个 j 净增 n_i，但比较用的中间值少一个 prime。**
 *        addq $n_i,sieve_ptr_ub
 *        movb (sieve_ptr),sv0
 *        subq prime,sieve_ptr_ub      ← 提前减
 *        ... 读字节 / 前移 ...
 *        cmpq sieve_ptr,sieve_ptr_ub ; jbe t2     ← 用的是减过的值
 *    t2:
 *        testb sv0,sv0
 *        leaq (sieve_ptr_ub,prime),sieve_ptr_ub   ← 加回来
 *        jz next_j
 *    减/加互相抵消，所以净 +n_i；但两条比较
 *      （`jbe t2` 那条，以及 tloop 里的 `ja`）都发生在减过、还没加回来的
 *    窗口内。搬运时必须保留这个中间状态 —— 直接写成 `ub += n_i` 再比较，
 *    循环次数就会变。
 *
 * 5) `testb sv0,sv0` 和它后面的 `jz` 之间夹着一条 `leaq`；lea 不改标志位，
 *    所以 jz 判的仍是 testb 的结果（sv0==0）。搬运时保持了原顺序。
 *
 * 6) 命中后 `subq prime,sieve_ptr` **两次**（共 -2*prime），然后 tloop：
 *        movzbq (sieve_ptr),auxreg
 *        testq auxreg,auxreg
 *        leaq (sieve_ptr,prime),sieve_ptr   ← 在「跳过」判断之前无条件前移
 *        jz tdsloop_next
 *        decq auxreg ; leaq (tds_buffer,auxreg,8),auxreg ; ...写并前移桶指针...
 *    tdsloop_next:
 *        cmpq sieve_ptr,sieve_ptr_ub ; ja tloop
 *    这是 do-while：至少报告一个位置；前移在判零之前，所以 val==0 也会走。
 *
 * 7) 每个 j 读的字节是：sieve_ptr[0]、sieve_ptr[prime]、前移 2*prime 后的
 *    sieve_ptr[0]，以及（有条件）再往后一个的 sieve_ptr[prime]。最后那个读
 *    `orb (sieve_ptr,prime),sv0` 卡在 `jbe t2` **之后**，是真条件读，
 *    与 tdslinie2 扫描趟那条无条件读正好相反 —— 别互相抄错。
 *
/*
 * 8) 三处循环边界都是无符号指针比较。入口 `cmpq %rdi,%rsi ; jbe` 用**原始**
 *    aux_ptr_ub；回边的 cmpq 在 `leaq 8(aux_ptr),aux_ptr` **之前**，
 *    判的是自增前的 aux_ptr，上界是减过 8 字节的。
 *
 * 9) **指针不能全程用 unsigned char***。汇编里 sieve_ptr_ub/sieve_ptr 是
 *    64 位寄存器，`subq %r11,%r9`（prime 比当前值大时会回绕）和
 *    tloop 里 `sieve_ptr += prime` 的无符号比较，全都依赖 64 位回绕语义。
 *    若在 C++ 里写成 unsigned char*，这类越界运算是**未定义行为**：
 *    GCC 有权假定指针不越界，于是把 `cmpq ; ja` 那条比较优化掉或反过来，
 *    结果与汇编不符。实测 prime=22486、root=3367 时，C++ 版会读到
 *    sieve_interval 右侧 2MB 之外（崩），而汇编正常返回。
 *    所以本文件全程用 uintptr_t 承载指针，解引用时经 TD_AT 转换，
 *    保证整条链路的算术与汇编逐位一致。
 */

#include "td_common.h"

extern "C" {

u32_t *tdslinie3(u16_t *, u16_t *, unsigned char *, u32_t **);

} /* extern "C" */

extern "C" u32_t *
tdslinie3(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval,
          u32_t **tds_buffer)
{
	if ((uintptr_t)aux_ptr_ub <= (uintptr_t)aux_ptr)
		return NULL;

	u16_t *const ub = aux_ptr_ub - 4;          /* subq $8 = 8 字节 */

	for (;;) {
		/* tdslinie3_fbi_loop —— 单趟，没有扫描/干活之分。 */
		const u64_t proot = (u64_t)aux_ptr[1] - (u64_t)aux_ptr[0];
		const u64_t prime = (u64_t)aux_ptr[0];
		u64_t       root  = (u64_t)aux_ptr[3];

		/* sieve_ptr_ub 全程用 uintptr_t 承载：汇编里 %r9 就是一个 64 位
		 * 寄存器，`subq %r11,%r9` 在 prime > 当前值时会回绕成巨大无符号数。
		 * 若写成 unsigned char* 指针，这类越界运算是 UB，GCC 可以据此
		 * 删掉比较或改变分支，结果与汇编不符（实测 prime=22486 时 C++ 会
		 * 读到区间右侧 2MB 之外，而汇编正常返回）。全程整数运算才能与
		 * 汇编的 64 位回绕语义逐位一致。 */
		uintptr_t sieve_ptr_ub = (uintptr_t)sieve_interval;

		for (int j = 0; j < TD_J_PER_STRIP; j++) {
			/* root 的数值：在 leaq 之前还不是指针 */
			uintptr_t sieve_ptr = (uintptr_t)root;
			u64_t     auxreg    = 0;

			{
				const u64_t t  = root + proot;
				const bool  cf = (t < root);
				root = t;
				if (!cf)
					auxreg = prime;
			}
			sieve_ptr = sieve_ptr_ub + sieve_ptr;

			sieve_ptr_ub += TD_NI;

			/* `addq %r12,%r10` —— 无进位时把 prime 补回 root。
			 * 漏掉这条 root 会整条线偏一个 prime（实测差值恰好 = prime）。 */
			root += auxreg;

			uint8_t sv0 = TD_AT(sieve_ptr);

			/* 提前减 prime：下面两条比较都在这个窗口内 —— 见第 4 条。 */
			sieve_ptr_ub -= prime;

			sv0 |= (uint8_t)TD_AT(sieve_ptr + prime);
			sieve_ptr += 2 * prime;
			sv0 |= (uint8_t)TD_AT(sieve_ptr);

			if (sieve_ptr_ub > sieve_ptr)
				sv0 |= (uint8_t)TD_AT(sieve_ptr + prime);

			/* testb / leaq / jz —— lea 不改标志位，jz 仍判 sv0==0。 */
			sieve_ptr_ub += prime;
			if (sv0 == 0)
				continue;   /* tdslinie3_next_j */

			/* 两次 subq prime，共 -2*prime。 */
			sieve_ptr -= 2 * prime;

			/* tdslinie3_tloop：`cmpq %r8,%r9 ; ja tdslinie3_tloop1a`
			 * 即 **ub > sp 时跳回循环体（继续）**。注意入口是从
			 * `subq prime` 之后**直接落入循环体**的，判断在体尾，
			 * 所以这是 **do-while**，不是 while —— 写成
			 * `while (ub > sp)` 会在入口条件为假时整段跳过，汇编不会。
			 * `ja` 判的是无符号的 ub - sp。
			 * 另：步进 `leaq (%r8,%r11),%r8` 在判零之前无条件执行，
			 * 所以 val==0 也照样前移。 */
			do {
				const u64_t v = TD_AT(sieve_ptr);
				sieve_ptr += prime;
				if (v != 0) {
					u32_t **slot = &tds_buffer[v - 1];
					*(*slot)++ = (u32_t)prime;
				}
			} while (sieve_ptr_ub > sieve_ptr);
		}

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
