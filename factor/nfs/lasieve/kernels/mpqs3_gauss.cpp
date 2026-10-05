/* (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * asm_re_strip 把一条 64 位字长的关系（约简后）按 4 路 GF(2) 消元结构
 * 「剥离」：先用 dptr 里的 4 个列号构造一张 16 项的异或查找表 tab，再用
 * ucmptr 里的字节逐项去异或。文件头的 C 注释就是这段逻辑的原样描述。
 *
 * 两处必须照抄的地方：
 *
 * 1. `movswq (dptr),%d` 是**符号扩展**，然后 `addq $1,%d`，再 `cmovcq tab,%h`
 *    —— 也就是「原值是 -1 时换成 tab 的地址」。汇编是先 +1 再判断是否为 0，
 *    C 版写成 `d == -1` 是等价的，但顺序反过来理解就容易写成 `d == 0`。
 *
 * 2. 循环次数。入口 `testq $1,cnt; jz odd_end` 处理 cnt 为奇数时的头一个字，
 *    然后 `shrq $1,cnt` 除以 2，`re_loop` 的 `decq cnt` 排在循环体**之前**，
 *    所以一共执行 cnt/2 次、每次两个 u64。cnt 为 0 或 1 时 `decq` 会把计数
 *    减成全 1 然后永远不归零 —— 真实调用点 cnt 是偶数且不小于 2。
 */

#include <stdint.h>

#include <emmintrin.h>

#include "siever-config.h"
#include "mpqs-config.h"   /* mpqs3 复用这个头，asm_re_strip 的声明在里面 */

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef u64_t u64;

/* cnt 的声明类型是 u32_t（mpqs-config.h:124），调用方 mpqs3.cpp:2779 传的
 * mpqs3_gauss_m = 1 + nFB + nFBk，奇偶不定 —— 汇编里那条 testq $1,cnt 正
 * 是为此。汇编按 64 位用 %rsi，C 调用约定下 u32 实参在高 32 位是 0，所以
 * 内部提升成 u64 与汇编一致。 */
void asm_re_strip(u64_t *rowptr, u32_t cnt_in, i16_t *dptr, unsigned char *ucmptr)
{
	u64 tab[16];
	u64 cnt = cnt_in;

	/* tab[0] = 0；汇编用 pxor %mm0,%mm0 写的 */
	tab[0] = 0;

	/* j=0..3，zz=1,2,4,8。每轮：tab[zz] = (d==-1 ? 0 : rowptr[d])，
	 * 再把 tab[1..zz-1] 依次和 tab[zz] 异或写进 tab[zz+1..2zz-1]。
	 *
	 * 索引是 rowptr[d] 而不是 rowptr[d+1]：汇编里
	 *     movswq (dptr),%d ; leaq (rowptr,%d,8),%h ; addq $1,%d ; cmovcq %tab,%h
	 * leaq 用的是 +1 **之前**的 d，addq 只是为了把 -1 变成 0 好让 cmovc
	 * 判断「原值是不是 -1」。文件头的 C 注释写的也是 rowptr[dptr[j]]。
	 */
	{
		int j, zz = 1;
		for (j = 0; j < 4; j++, zz <<= 1) {
			const long d0 = (long)dptr[j];
			const u64 *h = (d0 == -1) ? &tab[0] : (rowptr + d0);
			const u64 v = *h;
			int k;
			tab[zz] = v;
			for (k = 1; k < zz; k++)
				tab[zz + k] = tab[k] ^ v;
		}
	}

	/* cnt 为奇数时先单独处理头一个字，汇编里 movzbq (ucmptr),h 只取 1 字节 */
	if (cnt & 1) {
		rowptr[0] ^= tab[ucmptr[0]];
		ucmptr++;
		rowptr++;
	}
	cnt >>= 1;

	/* decq cnt 排在循环体之前，所以是 cnt 次 */
	while (cnt) {
		cnt--;
		rowptr[0] ^= tab[ucmptr[0]];
		rowptr[1] ^= tab[ucmptr[1]];
		ucmptr += 2;
		rowptr += 2;
	}
}
}  /* namespace lasieve_ns */
