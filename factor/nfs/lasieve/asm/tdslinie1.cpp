/* tdslinie1 —— 由 factor/nfs/lasieve/asm/tdslinie1.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 「时间除数筛」内核的一个变体。签名（照抄 asm/include/siever-config.h）：
 *     u32_t *tdslinie1(u16_t *aux_ptr, u16_t *aux_ptr_ub,
 *                      unsigned char *sieve_interval, u32_t **tds_buffer);
 * aux_ptr / aux_ptr_ub 都是 u16_t 指针，每条线占 4 个元素：
 * aux[0]=prime, aux[1]=proot_src, aux[3]=root，aux_ptr += 4（8 字节）。
 *
 * ============================ 结构 ============================
 * tdslinie1 是「**先整条线扫一遍，扫到有活再回头细做**」的两趟结构：
 *   tdslinie1_fbi_loop  —— 扫描趟，跨全部 j 把 sv0 累积成「整条线有没有活」
 *   tdslinie1_suche     —— 干活趟，只有 sv0 != 0 才进入
 * 两趟都从 aux_ptr[0..3] 重新读一遍线参数，所以扫描趟推进过的 root 不参与
 * 干活趟的计算；干活趟跑完 `jmp tdslinie1_nextfbi` 才写回 aux[3]。
 * 扫描趟的 sv0 入口清零一次、跨 j 累积；干活趟的 sv0 **每个 j 重新赋值**，
 * 跨 j 不累积。两种语义不同，不要合并。
 *
 * ============================ 必须照抄的语义 ============================
 *
 * 1) **返回值是垃圾，不要用。** 汇编把 %rax 当 tds_buffer2 的临时寄存器，
 *    返回的 %rax 是最后一次临时值。调用点也丢弃返回值。故返回 NULL，
 *    差分测试**不比较返回值**。
 *
 * 2) 入口 `leaq -8(tds_buffer),tds_buffer` 把 tds_buffer **整体前移一个元素**，
 *    之后索引用的是 `tds_buffer[val]`（原始的 `tds_buffer[val-1]`）。
 *    这是为了省掉 tdslinie 里那条 `decq`。搬运后直接写 `&tb[val]` 即可，
 *    语义与 `&tds_buffer[val-1]` 完全相同。
 *
 * 3) 射影根推进（与 tdslinie 同源）：
 *        root += (u64)(aux[1] - prime);  if (无进位) root += prime;
 *    `cmovncq` 判的进位来自 `addq %rbx,%r10`，中间的 `leaq` 不改标志位。
 *    「无进位」= 64 位加法没有向第 63 位进位，即有符号意义下结果 >= 0。
 *    必须写成 `bool cf = (t < root)`（无符号回绕判进位）。
 *
 * 4) **sieve_ptr_ub 净增 n_i，不是 n_i-5*prime。** 每个 j 里有
 *        subq prime,sieve_ptr_ub ; cmpq sieve_ptr,sieve_ptr_ub ;
 *        leaq (sieve_ptr_ub,prime),sieve_ptr_ub
 *    这一对减/加互相抵消，所以每轮净 +n_i。但 `cmpq` 卡在这对中间，
 *    它比较的是 **(sieve_ptr_ub 旧值 + n_i - prime) 和 sieve_ptr**。
 *    少写这对减/加、或把 cmpq 放到 leaq 之后，循环次数就会变。
 *
 * 5) 扫描趟的 `cmovaq (sieve_ptr,prime),auxreg` 是 **cmov + 8 字节 load**
 *    （movq），但随后只用 `auxb`（低 1 字节）。搬运时只读 1 字节即可，
 *    但**差分测试必须给 sieve_interval 留足尾部填充**，否则汇编那 7 个
 *    多余字节会越过数组末尾。
 *    条件是 `sieve_ptr_ub(减过 prime) > sieve_ptr`。
 *
 * 6) 干活趟第一处写桶：
 *        movzbq (sieve_ptr),auxreg ; testq ; **leaq (sieve_ptr,prime),sieve_ptr** ;
 *        jz skip ; ...写桶...
 *    `leaq` 在 `jz` **之前**无条件执行：即使 val==0 跳过了写桶，
 *    sieve_ptr 也已经前进 prime。这是「先做后判」，少前进一次就错。
 *
 * 6b) ★ 干活趟那条 `jbe tdsline1s_t2_` 的目标标签紧跟在
 *        `orb (sieve_ptr,prime),sv0` **之后**：
 *            movb (sieve_ptr),sv0
 *            cmpq sieve_ptr,sieve_ptr_ub
 *            leaq (sieve_ptr_ub,prime),sieve_ptr_ub
 *            jbe  tdsline1s_t2_'i     ← 只跳过下面这一条 orb
 *            orb  (sieve_ptr,prime),sv0
 *        tdsline1s_t2_'i:
 *            testb sv0,sv0            ← 照样执行
 *            jz    ...next_j
 *        所以条件为假时**只是少了第二个字节的 or**，写桶照做。
 *        误写成 `if (!above) continue;` 会把整段写桶跳过，表现是
 *        「汇编写了 N 条、C++ 一条没写」。
 *
 * 7) 干活趟第二处写桶把 `movq (tds_buffer,auxreg,8),%r8` 写进了 **sieve_ptr
 *    寄存器**（复用），随后 `leaq 4(%r8),%r8` 也用它。搬运时用一个局部
 *    临时变量即可 —— sieve_ptr 在本 j 迭代内此后不再被读（每个 j 开头
 *    `movq root,sieve_ptr` 重新赋值），所以这个复用是安全的。
 *
 * 8) 入口 `cmpq %rdi,%rsi ; jbe` 用**原始** aux_ptr_ub；回边
 *    `cmpq aux_ptr,aux_ptr_ub ; movw ... ; leaq 8(aux_ptr),aux_ptr ; ja`
 *    的 cmpq 在 leaq **之前**，判的是**自增前**的 aux_ptr，上界是减过 8 字节的。
 *    三处都是无符号指针比较。
 */

#include "td_common.h"

extern "C" {

u32_t *tdslinie1(u16_t *, u16_t *, unsigned char *, u32_t **);

} /* extern "C" */

extern "C" u32_t *
tdslinie1(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval,
          u32_t **tds_buffer)
{
	if ((uintptr_t)aux_ptr_ub <= (uintptr_t)aux_ptr)
		return NULL;

	u16_t *const ub = aux_ptr_ub - 4;          /* subq $8 = 8 字节 */

	/* 入口 `leaq -8(tds_buffer),tds_buffer`：整体前移一个 u32_t* 元素。 */
	u32_t **tb = tds_buffer - 1;

	for (;;) {
		/* 两趟各自重读 aux[0..3]；写回 aux[3] 的是最后跑完那一趟的 root。
		 * 两趟每个 j 的 root 推进完全相同，所以结果一致。 */
		u64_t root;

		/* ---- 扫描趟（tdslinie1_fbi_loop） ---- */
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

				/* subq … ; cmpq … ; leaq … —— 减/加抵消，但 cmpq 卡在中间。 */
				sieve_ptr_ub -= prime;
				const bool above =
				        sieve_ptr_ub > sieve_ptr;
				sieve_ptr_ub += prime;

				/* cmovaq 只用低字节（见第 5 条）。 */
				if (above)
					sv0 |= (uint8_t)TD_AT(sieve_ptr + prime);
			}

			if (sv0 == 0)
				goto nextfbi;   /* 整条线没活，跳过干活趟 */
		}

		/* ---- 干活趟（tdslinie1_suche） ---- */
		{
			const u64_t proot = (u64_t)aux_ptr[1] - (u64_t)aux_ptr[0];
			const u64_t prime = (u64_t)aux_ptr[0];
			root = (u64_t)aux_ptr[3];   /* 重新读，扫描趟的推进作废 */

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
				sieve_ptr_ub -= prime;

				uint8_t sv0 = (uint8_t)TD_AT(sieve_ptr);

				const bool above =
				        sieve_ptr_ub > sieve_ptr;
				sieve_ptr_ub += prime;

				/* ★ `jbe tdsline1s_t2_1` 的目标标签紧跟在
				 * `orb (sieve_ptr,prime),sv0` **之后**，所以它只跳过
				 * 这一条 orb，**紧接着仍然执行 testb/jz 和后面的写桶**。
				 * 写成 `if (!above) continue;` 会把整段写桶也一起跳过，
				 * 于是 si[sieve_ptr] 非 0、但 (ub <= sieve_ptr) 的那些 j
				 * 全部漏报 —— 实测表现正是「asm=+N 而 cxx=+0」。 */
				if (above)
					sv0 |= (uint8_t)TD_AT(sieve_ptr + prime);

				if (sv0 == 0)
					continue;   /* jz tdsline1s_next_j_ */

				/* 第一处写桶：leaq 在「跳过」判断之前无条件执行。 */
				{
					const u64_t v = TD_AT(sieve_ptr);
					sieve_ptr += prime;
					if (v != 0) {
						u32_t **slot = &tb[v];
						*(*slot)++ = (u32_t)prime;
					}
				}

				/* 第二处写桶：sieve_ptr 寄存器被复用成临时（见第 7 条）。 */
				if ((uintptr_t)sieve_ptr < (uintptr_t)sieve_ptr_ub) {
					const u64_t v = TD_AT(sieve_ptr);
					if (v != 0) {
						u32_t **slot = &tb[v];
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
