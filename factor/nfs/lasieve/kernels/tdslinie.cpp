/* tdslinie —— 由 factor/nfs/lasieve/asm/tdslinie.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 「时间除数筛」内核的一个变体。给定一组加权筛线 (prime, proot_src, root)
 * 和当前这条线的筛法区间 sieve_interval，把所有「二次命中」的位置写进
 * 因子基桶 tds_buffer[v-1]，写完把推进后的 root 写回 aux[3]。
 *
 * 签名（照抄 asm/include/siever-config.h）：
 *     u32_t *tdslinie(u16_t *aux_ptr, u16_t *aux_ptr_ub,
 *                     unsigned char *sieve_interval, u32_t **tds_buffer);
 * aux_ptr / aux_ptr_ub 都是 **u16_t 指针**（不是长度），每条线占 4 个元素：
 *     aux[0] = prime, aux[1] = proot_src, aux[3] = root，aux_ptr += 4。
 * asm 里 `subq $8,%rsi` 是 **8 字节**（= 4 个 u16_t 元素），不是 8 个元素。
 *
 * ============================ 必须照抄的语义 ============================
 *
 * 1) **返回值是垃圾，不要用。** 汇编里 %rax 全程没有��写入（只写了 %al，
 *    那 8 个 sv0 的 or 累加），返回的 %rax 是调用方的残留。调用点
 *    gnfs-lasieve4e.cpp 也确实把返回值丢掉了（`tdslinie(x, ub, ...);`）。
 *    所以本实现返回 NULL。差分测试**不比较返回值**。
 *    tdslinie1/2/3 同理：它们把 %rax 当 tds_buffer2 的临时寄存器用。
 *
 * 2) root 的推进用的是**射影根**（projective root）技巧，不是模 p 加法：
 *        proot = (u64)proot_src - (u64)prime        （64 位，可以是「负」的）
 *        root  = root + proot                       （64 位回绕）
 *        if (无进位) root += prime;                 （cmovncq）
 *    「无进位」= 加法没有向第 63 位进位，也就是**有符号意义下的结果 >= 0**。
 *    这里必须写成 `bool cf = (t < root)`（无符号回绕判进位），不能写成
 *    `t < 0` 之类的有符号比较，也不能省掉——那正是 C 参考里
 *    `r = modadd32(r, pr)` 的等价物，两种写法在 root + proot_src >= prime
 *    与 < prime 上差一次。
 *
 * 3) `cmovncq` 判的进位来自**它前面最近一条改标志位的指令**，也就是
 *    `addq %rbx,%r10`（root += proot）。中间那条 `leaq` 不改标志位，
 *    所以 cmov 拿到的确实是 add 的进位。照抄顺序，不要重排。
 *
 * 4) sieve_ptr 是**指针**，但它是 `sieve_ptr_ub + root` 算出来的，而 root
 *    是一个 64 位累加器（射影根），不是偏移量也不是指针。所以
 *    `movq %r10,%r8` 存的是 root 的数值，随后 `leaq (%r9,%r8),%r8` 才变成
 *    真正的指针。差分测试里两侧必须拿到**同一个 sieve_interval 基址**，
 *    否则连比较的前提都不成立。
 *
 * 5) 三个比较全部是**无符号指针比较**（`cmpq %r8,%r9 ; ja` 等）：
 *    - `cmpq aux_ptr,aux_ptr_ub ; jbe`   → aux_ptr_ub <= aux_ptr 就直接返回，
 *      用的是**未减 8** 的原始 aux_ptr_ub；
 *    - 回边 `cmpq %rdi,%rsi ; ja`        → aux_ptr_ub(已减 8) > aux_ptr(自增前)；
 *      `leaq 8(%rdi),%rdi` 在 cmp 之后、跳转之前执行，但 cmp 的标志位
 *      已经取过了，所以判的是**自增前**的 aux_ptr。
 *    两个界差的这一个元素，正好让最后一条线停住。不要看成 n-1 也不要
 *    看成 n，元素说法就是「n 条线全跑」：减 8 之后自增 8，两者抵消。
 *
 * 6) 内层是**先做后判**：`tdslinie_loop1:` 的四条 or 无条件执行一次，
 *    `tdslinie_looptest1:` 里的 `cmpq` 用的是**自增前**的 sieve_ptr，
 *    而 `leaq (%r8,%r11,2),%r8` 无条件执行。所以每轮固定读 4 个字节、
 *    步进 4*prime，循环次数由判据决定，差一个都会错。
 *
 * 7) 两处 `tdslinie_tloop*a/b`：注意 `leaq (sieve_ptr2,prime),sieve_ptr2`
 *    在 `jz` **之前**、在 `cmpq` 之前无条件执行，所以这是 do-while：
 *    至少读一个位置。判据分别是
 *      `cmpq %r12,%r13 ; ja` → sieve_ptr_ub2 > sieve_ptr2（第一处，用 %r13）
 *      `cmpq %r12,%r9  ; ja` → sieve_ptr_ub  > sieve_ptr2（第二处，用 %r9）
 *    两处用的上界**不是同一个寄存器**（一个是 `sieve_ptr + 2*prime` 的局部上界
 *    sieve_ptr_ub2，一个是末尾修正过的 sieve_ptr_ub），这是「先做后判 +
 *    循环外多修正」造成的，不是笔误。
 *
 * 8) 末尾 `lasttesta/lasttestb` 两段：每段都是「判 sieve_ptr < sieve_ptr_ub
 *    才读」。第一段读 2 个字节（sieve_ptr[0] 和 sieve_ptr[prime]）并把
 *    sieve_ptr 前移 2*prime；第二段只读 sieve_ptr[0]。顺序不能换——
 *    第一段的前移正是第二段要判的那个指针变化。
 *
 * 9) 写桶的序列是四步：
 *        decq %r15                       → val-1（val==0 已经 jz 跳过）
 *        leaq (%rcx,%r15,8),%r15         → &tds_buffer[val-1]
 *        movq (%r15),%r14 ; movl %r11d,(%r14) ; leaq 4(%r14),%r14 ; movq %r14,(%r15)
 *    即 `*(*slot)++ = (u32)prime`，写的是 **32 位**，指针步进 **4 字节**。
 *    tds_buffer 的元素是 `u32_t *`，所以 val 可以取 1..255，对应下标 0..254。
 *
 * 10) 射影根用完只剩低 16 位：`movw %r10w,6(%rdi)`。注意 root 是 64 位，
 *     中间步骤允许它超出 65535，只有存回 aux[3] 时才截断。
 *
 * 11) ★★ GAS 的 `leaq (base,index,scale),dst` 是 **base + index*scale**，
 *     **没有位移字段**。位移要写成 `disp(base,index,scale)`。
 *     已实测：机器码 `4f 8d 04 58` = `lea 0x0(%r8,%r11,2),%r8`，位移是 0；
 *     运行时 base=100、index=7 得到 114 = 100 + 7*2，而不是 109。
 *     所以本文件里：
 *         leaq (sieve_ptr,prime,2),sieve_ptr   →  sieve_ptr += 2 * prime
 *         leaq (sieve_ptr_ub,prime,4),...      →  sieve_ptr_ub += 4 * prime
 *     把它当成「base + index + 2」会让每个 j 的步进差一个 prime，
 *     循环次数随之变化 —— 第一版就是这么错的，桶写入数对不上。
 *
 *     ★ 与之相对，`(%r8,%r11,2)` 这种**内存操作数**同样是 base + index*2，
 *     所以 `movzbq (sieve_ptr),...` 之外的 `orb (sieve_ptr,prime,2),sv0`
 *     读的是 `si[sieve_ptr + 2*prime]` —— 这个在 tdslinie2 扫描趟是
 *     **无条件**读的（orb 排在 jbe 之前），见 tdslinie2.cpp 第 3 条。
 *
 * 11) **指针不能全程用 unsigned char***。汇编里 sieve_ptr_ub/sieve_ptr 是
 *     64 位寄存器，`subq %r11,%r9` 在 prime 大于当前值时会回绕成巨大无符号数，
 *     随后的 `cmpq ; ja` 按无符号比较。若在 C++ 里写成 unsigned char*，
 *     越界运算是**未定义行为**，GCC 有权据此改写分支，实测与汇编不符。
 *     所以全程用 uintptr_t 承载，解引用时才经 TD_AT 转换。
 */

#include "td_common.h"


#include "lasieve_ns.h"

namespace lasieve_ns {
u32_t *tdslinie(u16_t *, u16_t *, unsigned char *, u32_t **);


u32_t *
tdslinie(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval,
         u32_t **tds_buffer)
{
	/* `cmpq %rdi,%rsi ; jbe tdslinie_ende`：判的是**原始** aux_ptr_ub。 */
	if ((uintptr_t)aux_ptr_ub <= (uintptr_t)aux_ptr)
		return NULL;

	/* `subq $8,aux_ptr_ub`：8 **字节** = 4 个 u16_t 元素。 */
	u16_t *const ub = aux_ptr_ub - 4;

	for (;;) {
		/* tdslinie_fbi_loop */
		const u64_t proot = (u64_t)aux_ptr[1] - (u64_t)aux_ptr[0];
		const u64_t prime = (u64_t)aux_ptr[0];
		u64_t       root  = (u64_t)aux_ptr[3];

		uintptr_t      sieve_ptr_ub = (uintptr_t)sieve_interval;

		for (int j = 0; j < TD_J_PER_STRIP; j++) {
			/* `movq %r10,%r8` 存的是 root 的**数值**，随后才变指针。
			 * 在 leaq 之前 sieve_ptr 仍是整数。 */
			uintptr_t      sp       = (uintptr_t)root;
			uintptr_t      sieve_ptr;
			u64_t          auxreg   = 0;

			/* addq %rbx,%r10 → cmovncq %r11,%r15（进位来自这条 add）。 */
			{
				const u64_t t  = root + proot;
				const bool  cf = (t < root);
				root = t;
				if (!cf)
					auxreg = prime;
			}
			/* leaq (%r9,%r8),%r8：用**旧** sieve_ptr_ub。 */
			sieve_ptr = sieve_ptr_ub + sp;

			sieve_ptr_ub += TD_NI;

			/* `addq %r12,%r10` —— 无进位时把 prime 补回 root。
			 * 这条与上面 cmovncq 算出的 auxreg 是配对的：cmov 只**准备**
			 * auxreg，真正加回 root 的是这一行。漏掉它整条线偏一个
			 * prime（tdslinie3 第一版就漏了，aux[3] 差值恰好 = prime）。 */
			root += auxreg;

			/* leaq (%r11,%r11,4),%r15 ; subq %r15,%r9 —— 净 -5*prime。 */
			sieve_ptr_ub -= 5 * prime;

			/* tdslinie_loop：先做后判，每轮读 4 字节、步进 4*prime。 */
			uint8_t sv0;
			for (;;) {
				sv0  = (uint8_t)(TD_AT(sieve_ptr) | TD_AT(sieve_ptr + prime));
				sieve_ptr += 2 * prime;
				sv0 |= (uint8_t)(TD_AT(sieve_ptr) | TD_AT(sieve_ptr + prime));

				if (sv0 != 0) {
					/* negq %r11 ; leaq (sieve_ptr,prime,2) ; negq %r11 */
					const uintptr_t ub2 = sieve_ptr + 2 * prime;
					uintptr_t           sp2 = sieve_ptr - 2 * prime;

					/* do-while：步进在 cmpq 之前无条件执行。 */
					do {
						const u64_t v = TD_AT(sp2);
						sp2 += prime;
						if (v != 0) {
							u32_t **slot = &tds_buffer[v - 1];
							*(*slot)++ = (u32_t)prime;
						}
					} while (ub2 > sp2);
				}

				/* tdslinie_looptest：cmpq 用**自增前**的 sieve_ptr，
				 * leaq 无条件执行。 */
				{
					const bool again =
					        sieve_ptr_ub > sieve_ptr;
					sieve_ptr += 2 * prime;
					if (!again)
						break;
				}
			}

			/* 末尾 lasttesta / lasttestb 两段修正。 */
			sieve_ptr_ub += 4 * prime;
			sv0 = 0;
			{
				uintptr_t sp2 = sieve_ptr;   /* movq %r8,%r12 */

				if (sieve_ptr_ub > sieve_ptr) {
					sv0 = (uint8_t)(TD_AT(sieve_ptr) | TD_AT(sieve_ptr + prime));
					sieve_ptr += 2 * prime;
				}
				sieve_ptr_ub += prime;
				if (sieve_ptr_ub > sieve_ptr)
					sv0 |= (uint8_t)TD_AT(sieve_ptr);

				if (sv0 != 0) {
					/* 这里的 cmpq 用 %r9（末尾修正过的 sieve_ptr_ub）。 */
					do {
						const u64_t v = TD_AT(sp2);
						sp2 += prime;
						if (v != 0) {
							u32_t **slot = &tds_buffer[v - 1];
							*(*slot)++ = (u32_t)prime;
						}
					} while (sieve_ptr_ub > sp2);
				}
			}
		}

		/* 回边：cmpq 在 leaq 之前，判的是**自增前**的 aux_ptr。
		 * `movw %r10w,6(%rdi)` 只存低 16 位。 */
		aux_ptr[3] = (u16_t)root;
		{
			const bool again = (uintptr_t)ub > (uintptr_t)aux_ptr;
			aux_ptr += 4;
			if (!again)
				break;
		}
	}

	return NULL;   /* 见第 1 条：汇编的 %rax 从未被写，返回值无意义。 */
}
}  /* namespace lasieve_ns */
