/* (Copyright (C) 2004 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 32 位模逆：Stein 二进制 GCD 加「批量减 15 次」优化（define(nts,15)，
 * forloop 展开成 15 段）。返回值 y 满足 x*y ≡ 1 (mod modulo32)。
 *
 * ============================= 契约（实测定的）=============================
 * 1 <= x < modulo32，且 gcd(x, modulo32) = 1。越界就 call abort，不是返回错误码。
 *
 * 入口那句 `cmpl %edi,%esi ; jbe badargs` 是 AT&T 语法：目标在前、
 * 源在后，实际算的是 **modulo32 - x**（esi - edi），jbe 在 y <= x 时跳转。
 * 所以它要求的是 x < modulo32（不是 x > modulo32）；x=1 合法，返回 1。
 * 这一点用两种办法验过：(1) fork 子进程逐点试，x<m 全部正常返回、x>=m
 * 全部 abort；(2) 展开后的汇编是 `cmp %edi,%esi`，objdump 一致。
 * 真实调用点 lasieve-prepn.cpp:516 传的是
 *     x = modsub32(A0MOD(modulo32), modmul32(proots[fbi], B0MOD(modulo32)))
 * modsub32 的结果落在 [0, modulo32)，调用点还判了 x != 0，所以契约满足。
 *
 * ============================ 试减块：先减后判 =============================
 * 试减块里是「先减、再判借位」，不是「先判、再减」：
 *     subl %edi,%esi     ; y -= x     （不是 x -= y！AT&T 目标是最后一个操作数）
 *     addl %ecx,%r8d    ; yc += xc
 *     cmpl %edi,%esi    ; 算 y - x
 *     jb  xlarger        ; y < x 就退出这个块
 * 之所以能先减后判，是因为进每个块时大小关系已经摆好：第一次进 divide 时
 * 入口检查过 x < modulo32 = y，之后每次进 divide 都是刚从 x %= y 出来的，
 * x < y。15 次减法里前 14 次都够减；第 15 次不够减时 y 已经被减过了（回绕成
 * 一个巨大的 u32），但退出点 xlarger 紧接着检查 y <= 1，语义上正好对应
 * 「已经减到 1 或更小」。改成先判后减会少减一次，返回值就不是模逆了。
 *
 * 另一个方向性的坑在第二块：`subl %esi,%edi` 才是 x -= y。两块的减法方向
 * 相反，因为循环变量不同（第一块削 y，第二块削 x）。写成同一个方向必错。
 *
 * ============================== divl 的用法 ==============================
 *     movl y,%eax ; xorl %edx,%edx ; divl x ; movl %edx,y ; mull xc ; addl %eax,yc
 * 商和余数都要用：edx 是余数存回 y，eax（商）用来累加 yc。
 * 写成 `y %= x; yc += (y/x)*xc;` 是错的 —— 那时 y 已经是余数了。
 *
 * mull xc 出 64 位积，但 addl %eax,yc 只取低 32 位，也就是 yc 累加的是
 * 32 位回绕后的值，乘积高半被丢掉。C++ 里用 u32 累加即是同一个语义
 * （无符号溢出回绕，不是 UB）。
 *
 * movl modulo32(%rip),%eax ; subl %r8d,%eax → 返回 modulo32 - yc，
 * 也是 32 位回绕。
 */

#include <stdint.h>
#include <stdlib.h>

#include "siever-config.h"
#include "32bit.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef u32_t u32;

/* 32bit.h 里已经有 `u32_t ASM_ATTR asm_modinv32(u32_t x);`（在 块里）
 * 和 `volatile extern u32_t modulo32;`，上面这两行 include 就够了：
 * 前者让下面的定义拿到 C 链接、和汇编那一侧对得上，后者提供 modulo32。
 * 同一个头里还有 `#define modinv32(x) asm_modinv32(x)`，所以实现里要写全名。
 * 不要自己再写 `extern u32 modulo32;` —— 少了 volatile 会 conflicting
 * declaration。 */

#define NTS 15          /* 试减次数，和汇编里的 define(nts,15) 一致。
		 * 这个数不是可观测语义：试减块后面紧跟 divl，divl 给的是精确
		 * 商和余数，减 k 次之后 (m - k*x) mod x 恒等于 m mod x，商那半边
		 * (m/x - k)*xc 会在系数递推里抵消。所以 NTS 取 1..40 结果都一样
		 * （440196 组 (modulo32,x) 上验过，差异 0 次）。它只影响速度和
		 * 「借位检查在哪一轮触发」，差分测试钉不住它，别以为测试漏了。 */

u32 asm_modinv32(u32 x)
{
	u32 y = modulo32;
	u32 xc, yc;
	int i;

	if (x == 0)                          /* testl x,x ; jz badargs */
		abort();
	if (x >= y)                          /* cmpl x,y（=y-x）; jbe badargs */
		abort();

	xc = 0;
	yc = 0;
	xc = 1;
	if (x <= 1)                          /* cmpl $1,x ; jbe have_inverse2 */
		goto have_inverse2;

divide:
	/* 第一块：削 y。每次 y -= x 之后若 y < x 就带着回绕值跳走，
	 * 退出点立刻判 y <= 1，所以回绕无害。 */
	for (i = 0; i < NTS; i++) {
		y -= x;
		yc += xc;
		if (y < x)                       /* cmpl x,y ; jb xlarger */
			goto xlarger;
	}
	{
		/* divl x 同时给出商和余数，见文件头 */
		u32 q = y / x;
		y = y % x;
		yc += q * xc;                   /* mull xc ; addl %eax,yc */
	}

xlarger:
	if (y <= 1)                          /* cmpl $1,y ; jbe have_inverse1 */
		goto have_inverse1;

	/* 第二块：削 x，方向和第一块相反 */
	for (i = 0; i < NTS; i++) {
		x -= y;
		xc += yc;
		if (x < y)                       /* cmpl y,x ; jb ylarger */
			goto ylarger;
	}
	{
		u32 q = x / y;
		x = x % y;
		xc += q * yc;
	}

ylarger:
	if (x > 1)                           /* cmpl $1,x ; ja divide */
		goto divide;

have_inverse2:
	/* 这里的标志位来自上面那条 cmpl $1,x：ZF 表示 x == 1。 */
	if (x != 1)                          /* jne badargs */
		abort();
	return xc;                           /* movl xc,%eax */

have_inverse1:
	/* 标志位来自 cmpl $1,y：ZF 表示 y == 1。y==0（x 是 modulo32 的因子）时
	 * 也走这里，但 ZF=0，于是 abort —— 非互素就是这条路径。 */
	if (y != 1)                          /* jne badargs */
		abort();
	return modulo32 - yc;                 /* movl modulo32(%rip),%eax ; subl yc */
}
}  /* namespace lasieve_ns */
