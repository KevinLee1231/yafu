/* mpqs_sieve —— 由 mpqs_sieve.asm 翻译而来
 * (Copyright (C) 2001 Jens Franke, T. Kleinjung，gnfs4linux，GPL)。
 *
 * 一个入口 asm_sieve(void)，纯标量，没有一条 SIMD 指令，也没有 SIMD 对齐
 * 要求。全部状态在 mpqs_* 全局里。
 *
 * 素数按 p 的范围分五段处理，每段展开出来的步数不同：
 *     p <= sl/4   唯一排序、唯一有内层循环的一段
 *     p <= sl/3
 *     p <= sl/2
 *     p <= sl-1
 *     p <= 0xffff   一直做到碰到 FB 里的 0xffff 哨兵为止
 *
 * ------------------------------------------------------------------
 * 照抄、不能「顺手改好」的地方。
 *
 * 1. `movzwq mpqs_sievelen(%rip),sl4` 只读 16 位，mpqs_sievelen 在 C 侧是
 *    u32_t，高 16 位被丢掉。实际 sievelen <= MPQS_SIEVELENMAX = 1<<14，
 *    永远读不到高位，但这里照样写成 (u16) 截断，不改成读 32 位。
 *    mpqs_sievebegin 本来就是 ushort，不用截。
 *
 * 2. 指针步长：`leaq 4(FB),FB` 前进的是 **4 字节 = 2 个 u16 元素**。
 *    mpqs_FB / mpqs_FB_start 是 ushort 数组，一个素数占两格
 *    （FB 是 [2i]=p、[2i+1]=p²，FB_start 是 [2i]=s1、[2i+1]=s2），
 *    所以 FB、FBs 一次前进 2 个元素；mpqs_FB_log 是 uchar 数组，FBl 一次
 *    前进 1 个字节。写成 `FB++` 会读到 p² 那一格。
 *    同理 `movzwq 2(FBs),sloc2` 里的 2 是**字节**偏移，也就是 ushort 数组的
 *    第 1 格（第二条根），不是第 2 格。
 *
 * 3. 每段都是「读 p -> 前推一格 -> 判界」，`jc` 跳出时 FB 已经越过当前素数；
 *    下一段入口那条 `leaq -4(FB),FB` 正好退回来，让新段重新读**同一个**
 *    素数。段界递增（sl/4, sl/3, sl/2, sl-1, 0xffff），退回后那个素数必然
 *    还满足新界，于是被**再处理一次**；FBs / FBl 不退，读到的 s1/s2/lo
 *    已经是后面那个素数的了。看着像原代码的 bug，但这是实际行为，照抄。
 *    段界相等时（sl 小的时候）同一个素数会被连续几段重复处理。
 *
 * 4. 只有第一段排序：`cmpq sloc1,sloc2 ; cmovcq` 排成 sloc1 <= sloc2。
 *    后四段不排。顺序只影响「哪条根先写」，但影响第一段里
 *    「用较大的那个根跟 sievebound 比循环边界」，照抄。
 *
 * 5. **钳位不是「不写」，是「写到一个固定字节」**：
 *        cmpq sieveend,slocX ; cmovncq sieveend,slocX
 *    把越界的那几条全部堆到 sievearray[sievelen] 这一个字节上。
 *    C 参考实现（mpqs.cpp:1610 之后那段）写的是 `if (sv+s1<svend) sv[s1]+=lo;`
 *    —— 判断和不判断的语义完全不同，汇编这一份必须照抄成钳位。
 *    那个字节是 mpqs.cpp 分配时 `(1+MPQS_SIEVELENMAX)` 里多出来的 1。
 *
 * 6. 第一段内层循环 `loop4` 是 do-while（`jc` 在轮尾），一轮写
 *    (sloc) (sloc,p) (sloc,2p) (sloc,3p) 两条根共 8 处，步长 4p，
 *    条件是**较大的那条根** sloc2 < sievebound = sieveend - 3p。
 *    很容易写成「一轮两次 advance、推进 8p」—— sl 不是 4p 整数倍时会
 *    多写一截。
 *
 * 6b. loop4 之后还有两段收尾，顺序不能换：
 *        addq prime2,sievebound        （sievebound += 2p）
 *        cmpq sievebound,sloc2 ; jnc check      —— 这里是**第一个测试用跳转**
 *        写 (sloc1) (sloc1,p) (sloc2) (sloc2,p)，两条根各 +2p
 *     check: 两次 cmovncq 钳位，然后
 *        写 sloc1，sloc1 += p，写 sloc2，再钳一次 sloc1，写 sloc1。
 *     也就是最后那一条根的最后一个落点是**钳位之后**的 sloc1。
 *
 * 7. 后三段（sl/3、sl/2、sl-1）各自的展开：
 *        sl/3   (sloc) (sloc,p) 两条根 | (sloc,2p) 两条根 | += 3p | 钳位 | 写两条
 *        sl/2   (sloc) (sloc,p) 两条根 | += 2p | 钳位 | 写两条
 *        sl-1   (sloc) 两条根     | += p   | 钳位 | 写两条
 *     第六段（0xffff）只有 (sloc) 两条根 + 钳位 + 写两条，不推进。
 *
 * 8. 写出会越过 sievelen（钳位只保证最后那几条，循环体里的写不钳位）。
 *     C++ 里不做任何边界检查 —— 汇编也没有，加了检查两边就不一致了。
 *
 * 9. `divq sl4` 那条在 loop3begin：sl / 3，64 位无符号除法。被除数是
 *    0..0xffff 的 sievelen，除数是常量 3，不会 #DE。照抄。
 */

#include <stdint.h>

#include "siever-config.h"
#include "mpqs-config.h"

typedef unsigned char u8;
typedef u16_t u16;
typedef u32_t u32;
typedef u64_t u64;

/* mpqs.cpp 里定义的那几个符号。文件作用域的变量在 C++ 里不做名字改编，
 * 子 Makefile 按 I 值用 -Dmpqs_FB_start=mpqs_FB_startI11 统一改名，定义和
 * 引用一起变，对得上。 */
extern u16 mpqs_FB[];          /* 2*MPQS_MAX_FBSIZE：每素数两个 u16 */
extern u16 mpqs_FB_start[];    /* 2*MPQS_MAX_FBSIZE：[2i]=s1、[2i+1]=s2 */
extern u8 mpqs_FB_log[];       /* MPQS_MAX_FBSIZE */
extern u16 mpqs_sievebegin;
extern u32 mpqs_sievelen;
extern u8 *mpqs_sievearray;

/* `cmpq sieveend,slocX ; cmovncq sieveend,slocX` —— 见头注 5 */
static u8 *clamp(u8 *q, u8 *sieveend)
{
	return (q >= sieveend) ? sieveend : q;
}

void asm_sieve(void)
{
	const u64 svb = mpqs_sievebegin;         /* movzwq */
	const u64 sl = (u64)(u16)mpqs_sievelen;  /* movzwq：高 16 位被丢掉 */

	u16 *FB = mpqs_FB + 2 * svb;             /* leaq (FB,%rax,4),FB */
	u16 *FBs = mpqs_FB_start + 2 * svb;
	u8 *FBl = mpqs_FB_log + svb;             /* leaq (FBl,%rax),FBl：1 字节/素数 */

	u8 *const sv = mpqs_sievearray;
	u8 *const sieveend = sv + sl;            /* movq sieveend ; addq sl */

	/* ---------------- 第一段：p <= sl/4 ---------------- */
	for (;;) {
		const u64 p = FB[0];
		FB += 2;
		if (p > sl / 4)          /* cmpq prime,sl4 ; jc —— sl4 < prime */
			break;

		u64 s1 = FBs[0], s2 = FBs[1];
		FBs += 2;

		/* cmpq sloc1,sloc2 ; cmovcq —— 排成 s1 <= s2 */
		if (s2 < s1) {
			const u64 t = s1;
			s1 = s2;
			s2 = t;
		}

		const u8 lo = *FBl++;

		u8 *q1 = sv + s1;        /* sloc1：较小的那条根 */
		u8 *q2 = sv + s2;        /* sloc2：较大的那条根 */

		/* sievebound = sieveend - 3p，循环条件用较大的那条根比 */
		u8 *sievebound = sieveend - 3 * p;

		do {
			q2[0] += lo;
			q2[p] += lo;
			q1[0] += lo;
			q1[p] += lo;
			q2[2 * p] += lo;
			q2[3 * p] += lo;      /* (sloc2,aux1)，aux1 = 3p */
			q1[2 * p] += lo;
			q1[3 * p] += lo;
			q2 += 4 * p;          /* leaq (sloc2,prime2,2),sloc2 */
			q1 += 4 * p;
		} while (q2 < sievebound);   /* jc 在轮尾：do-while */

		/* 收尾 1：再补一轮 {0,p}，两条根各前进 2p。
		 * 这里汇编用的是「第一个测试用跳转」而不是 cmov。 */
		sievebound += 2 * p;
		if (q2 < sievebound) {
			q1[0] += lo;
			q1[p] += lo;
			q2[0] += lo;
			q2[p] += lo;
			q1 += 2 * p;
			q2 += 2 * p;
		}

		/* check：钳位之后写，最后一条根的最后一落在钳位后的 q1 上 */
		q1 = clamp(q1, sieveend);
		q2 = clamp(q2, sieveend);
		q1[0] += lo;
		q1 += p;
		q2[0] += lo;
		q1 = clamp(q1, sieveend);
		q1[0] += lo;
	}
	FB -= 2;                          /* loop3begin: leaq -4(FB),FB */

	/* ---------------- 第二段：p <= sl/3 ---------------- */
	for (;;) {
		const u64 p = FB[0];
		FB += 2;
		if (p > sl / 3)
			break;

		u8 *q1 = sv + FBs[0];
		u8 *q2 = FBs[1] + sv;
		FBs += 2;
		const u8 lo = *FBl++;

		q1[0] += lo;
		q1[p] += lo;
		q2[0] += lo;
		q2[p] += lo;
		q1[2 * p] += lo;
		q2[2 * p] += lo;
		q1 += 3 * p;                  /* addq aux1,sloc1，aux1 = 3p */
		q2 += 3 * p;

		q1 = clamp(q1, sieveend);
		q2 = clamp(q2, sieveend);
		q1[0] += lo;
		q2[0] += lo;
	}
	FB -= 2;                          /* loop2begin: leaq -4(FB),FB */

	/* ---------------- 第三段：p <= sl/2 ---------------- */
	for (;;) {
		const u64 p = FB[0];
		FB += 2;
		if (p > sl / 2)
			break;

		u8 *q1 = sv + FBs[0];
		u8 *q2 = FBs[1] + sv;
		FBs += 2;
		const u8 lo = *FBl++;

		q1[0] += lo;
		q1[p] += lo;
		q2[0] += lo;
		q2[p] += lo;
		q1 += 2 * p;                  /* prime2 = 2p */
		q2 += 2 * p;

		q1 = clamp(q1, sieveend);
		q2 = clamp(q2, sieveend);
		q1[0] += lo;
		q2[0] += lo;
	}
	FB -= 2;                          /* loop1begin: leaq -4(FB),FB */

	/* ---------------- 第四段：p <= sl-1 ---------------- */
	for (;;) {
		const u64 p = FB[0];
		FB += 2;
		if (p > sl - 1)
			break;

		u8 *q1 = sv + FBs[0];
		u8 *q2 = FBs[1] + sv;
		FBs += 2;
		const u8 lo = *FBl++;

		q1[0] += lo;
		q2[0] += lo;
		q1 += p;
		q2 += p;

		q1 = clamp(q1, sieveend);
		q2 = clamp(q2, sieveend);
		q1[0] += lo;
		q2[0] += lo;
	}
	FB -= 2;                          /* loop0begin: leaq -4(FB),FB */

	/* ---------------- 第五段：p <= 0xffff ----------------
	 * 段界是 0xffff，退出条件不是 `jc` 而是 `je`：碰到 FB 里的 0xffff
	 * 哨兵才停。所以这一段**必须**有 0xffff 哨兵，否则会一直跑下去。 */
	for (;;) {
		const u64 p = FB[0];
		FB += 2;
		if (p == 0xffff)
			break;

		u8 *q1 = sv + FBs[0];
		u8 *q2 = FBs[1] + sv;
		FBs += 2;
		const u8 lo = *FBl++;

		q1 = clamp(q1, sieveend);
		q2 = clamp(q2, sieveend);
		q1[0] += lo;
		q2[0] += lo;
	}
}
