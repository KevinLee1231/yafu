/* mpqs3_sieve —— 由 mpqs3_sieve.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 两个入口，都是纯标量代码，没有一条 SIMD 指令，也没有一条读 x86 标志位的
 * 指令需要标量回退 —— 汇编里的 jc / jnc 全部是「循环到哪儿为止」或者
 * 「指针要不要钳到 sieveend」，都能用普通的比较和取地址表达出来。
 *
 *   asm3_sieve   —— 素数逐个从 FB / FB_start / FB_log 里读，按 p 的范围分成
 *                   八段（sl/8, sl/7, ..., sl/2, sl-1）处理，每段展开固定步数。
 *   asm3_sievea  —— 素数从 FB0 读。FB0 的编码是若干段
 *                       0, lo, p, p, ..., 0, 0
 *                   汇编每段用一套展开不同的循环处理，五段之后直接返回。
 *
 * ------------------------------------------------------------------
 * 翻译时照抄、不能「顺手改好」的地方，逐条列在下面。
 *
 * 1. 段的分界是严格大于：
 *        movzwq (FB),prime ; cmpq prime,sl4 ; jc <下一段>
 *    `jc` 判的是 CF，也就是 sl4 < prime（无符号）。所以边界值 sl4 本身
 *    仍然属于**上一段**。写 C 时是 `if (p > bound) break;`，不是 `>=`。
 *
 * 1b. 指针步长：`leaq 4(FB),FB` 前进的是 **4 字节 = 2 个 u16 元素**。
 *     mpqs3_FB / mpqs3_FB_start 都是 ushort 数组，一个素数占两格
 *     （[2i]=p、[2i+1]=p²，FB_start 是 [2i]=s1、[2i+1]=s2），所以
 *     FB、FBs 一次前进 2 个元素；mpqs3_FB_log 是 uchar 数组，FBl 一次
 *     前进 1 个字节。写成 `FB++` 会读到 p² 那一格，结果全错。
 *
 * 1c. 同理 `movzwq 2(FBs),sloc2` 里的 2 是**字节**偏移，也就是 ushort
 *     数组的第 1 格（第二条根），不是第 2 格。写成 FBs[2] 会读到下一个
 *     素数的 s1，结果全错。
 *
 * 2. 每一段里汇编都是「先做完一整轮，再判 jc/jnc」，也就是 do-while。
 *    C 参考实现 mpqs3.cpp 里写的是 while，两者只在越界输入上不同；这里照抄
 *    汇编的 do-while。
 *
 * 3. 各段展开出来的偏移表必须逐条数对，而且**相邻几段并不相同**，抄错一个数就
 *    整段错位。以反汇编为准：
 *
 *      mainloop7  4 次 `addq prime2`：0,p,2p,3p,4p,5p,6p,7p   共 8 处
 *      mainloop6  2 次 advance + (sloc,prime2)：0,1,2,3,4,5,6  共 7 处
 *      mainloop5  2 次 advance：                0,1,2,3,4,5     共 6 处
 *      mainloop4  只有 1 次 advance，最后那条 (sloc,prime2) 落在
 *                 2p+2p=**4p**（不是 6p）：       0,1,2,3,4       共 5 处
 *      mainloop3  `leaq (sloc,prime,2),sloc` 前进 2p：0,1,2,3    共 4 处
 *      mainloop2  先写 (sloc,prime)，再 `addq prime,prime` 把 prime 加倍，
 *                 然后 (sloc) 和 (sloc,prime)：p, 0, 2p          共 3 处
 *      mainloop1  0, p                                              共 2 处
 *
 *    AT&T 里 `(base,index,scale)` 的 scale 是比例因子，没有位移，所以
 *    `leaq (sloc,prime,2),sloc` 是 sloc += 2p，不是 2p+2。
 *
 * 4. asm3_sieve 只有第一段（p<=sl/8）才对 s1/s2 排序（cmovc 交换），
 *    后七段不排。asm3_sievea 只有 return8a 排，return4a/3a/2a/1a 都不排。
 *    顺序只影响「哪一条根先被写」，不影响结果，但会影响 asm3_sievea 里
 *    「用较大的那个根跟 sieveend 比循环边界」——照抄。
 *
 * 5. `movzwq mpqs3_sievelen(%rip),sl4` 只读 16 位。mpqs3_sievelen 在 C 里是
 *    u32_t，汇编这边把高 16 位丢了。实际 sievelen <= MPQS3_SIEVELEN = 1<<15，
 *    永远读不到高位，但这里照样写成 (u16) 截断，不改成读 32 位。
 *
 * 6. asm3_sievea 的 `lo` 就是 `%al`，它的初值是 `(u8)mpqs3_sievebegin`。
 *    这一条**不是**「未定义的残留值」，是确定的，probe 实测过（见下）：
 *    函数入口第一条写 %rax 的指令是
 *        movzwq mpqs3_sievebegin(%rip),%rax
 *    （在五段 push 之后、在任何 %al 的使用之前），此后 %rax 在 asm3_sievea 里
 *    再没被写过。所以第一次用 %al 之前它已经被赋成 sievebegin 的低字节。
 *
 *    有人会写成 `u8 lo = 0;`，那会让「FB0 里以裸素数开头的段」全部错位
 *    ——108 处不一致里有相当一部分就是这么来的。照抄成 (u8)svb。
 *
 *    探针结论（Temp/probe_al.c，FB0 = `0,0,p,...`，即 8a 段为空、4a 段以裸素数开头）：
 *        svb=0     -> 增量全 0
 *        svb=7     -> 增量 14   = 2 次写 x 7
 *        svb=300   -> 增量 88   = 2 次写 x 44，而 (u8)300 == 44
 *        调用前把 %rax 强行打成 0xAA，结果一模一样
 *    即 %al 与调用方的 %rax 无关，取值确定。C++ 侧写 (u8)svb。
 *
 *    跨段行为（探针同样实测）：`%al` 只有 update8a/4a/3a/2a/1a 里的
 *    `movb %r14b,%al` 会改它，所以它**跨五段一直保留**：
 *        FB0 = `0,9,5, 0,0, <裸素数>`   -> 裸素数那段用 lo=9
 *        FB0 = `0,9,5, 0,0, 0,4,11, <裸素数>` -> 裸素数那段用 lo=4
 *    所以下面每次 `lo = (u8)l16` 都是对 %al 的真实赋值，写成局部变量
 *    `const u8 lo` 反而会丢掉这个语义，必须让它跨段可见。
 *
 *    对照：asm3_sieve 不受这条影响。它的每个 mainloopN 都在 addb 之前
 *    执行 `movb (%r9),%al`，入口的 %al 一定被覆盖掉。
 *
 * 7. check* 标签里的
 *        cmpq sieveend,slocX ; cmovncq sieveend,slocX
 *    是把「越界的那几条」全部堆到 sievearray[sievelen] 这一个字节上。这个字节
 *    是 mpqs3.cpp 分配时 `(1+3*MPQS3_SIEVELEN)` 里多出来的那个 1。照抄。
 *
 * 8. 写出会越过 sievelen。汇编对 p<=sl/4 的段最多写到 6p+s，p<=sl/1 的段
 *    最多写到 s+p，s<p。最大约 1.5*sievelen，靠那块 3 倍的分配兜住。
 *    C++ 里不做任何边界检查 —— 汇编也没有，加了检查两边就不一致了。
 *
 * 8b. 只有第一段（p<=sl/8）有内层循环 `loop4`，它的**一轮**是
 *       (sloc) (sloc,prime) | +2p | (sloc) (sloc,prime) | +2p
 *     即偏移 {0,p,2p,3p}、步长 4p，`jc` 在轮尾（do-while）。
 *     很容易写成「一轮两次 expand、推进 8p」—— 那样当 sl 不是 4p 整数倍时
 *     会多写一截，sl=17 就能看出（多写 idx24..31）。
 *
 * 8c. asm3_sievea 的 loop8a / loop4a 内层同样是「一轮 {0,p,2p,3p}、步长 4p」，
 *     区别只在 loop4a 的循环尾部多一段「再写 {0,p}、推进 2p」。
 *
 * 9. 段界相等时（sl=16 时 sl/8=sl/7=sl/6=2）仍然要按汇编那样每段各退一格，
 *    所以同一素数会被多段重复处理。测试里 sl=16 已经覆盖这条。
 */

#include <stdint.h>

#include "siever-config.h"
#include "mpqs-config.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef unsigned char u8;
typedef u16_t u16;
typedef u32_t u32;
typedef u64_t u64;

/* mpqs3.cpp 里定义的那几个符号。文件作用域的变量在 C++ 里不做名字改编，
 * 子 Makefile 按 I 值用 -Dmpqs3_FB_start=mpqs3_FB_startI11 统一改名，
 * 定义和引用一起变，对得上。 */
extern u16 mpqs3_FB[];       /* 2*MPQS3_MAX_FBXSIZE：每素数两个 u16 */
extern u16 mpqs3_FB0[];      /* MPQS3_MAX_FBSIZE+2*256 */
extern u16 mpqs3_FB_start[]; /* 2*MPQS3_MAX_FBXSIZE */
extern u8 mpqs3_FB_log[];    /* MPQS3_MAX_FBSIZE */
extern u16 mpqs3_sievebegin;
extern u32 mpqs3_sievelen;
extern u8 *mpqs3_sievearray;

/* ------------------------------------------------------------------
 * 各段的偏移表：第 k 项是「这一轮里第 k 个位置」相对起点的字节数。
 * 写法全部照抄汇编的指令顺序，顺序不影响结果（写的是互不相同的字节），
 * 但保留顺序方便跟 .asm 对照。
 */

/* asm3_sieve，p <= sl/8：内层 do-while，偏移 0,p,2p,3p，每轮走 4p */
static const u8 off_sl8[] = {0, 1, 2, 3};

/* p <= sl/7：0,p,2p,3p,4p,5p,6p,7p */
static const u8 off_sl7[] = {0, 1, 2, 3, 4, 5, 6, 7};

/* p <= sl/6：0, p, 2p, 3p, 4p, 5p, 6p，共 7 处。
 * 轨迹（反汇编 mainloop6）：
 *   (sloc) (sloc,prime) | +2p | (sloc) (sloc,prime) | +2p | (sloc) (sloc,prime)
 *   (sloc,prime2)
 * => 0, p, 2p, 3p, 4p, 5p, 2p+2p=6p */
static const u8 off_sl6[] = {0, 1, 2, 3, 4, 5, 6};

/* p <= sl/4：0, p, 2p, 3p, 4p，共 5 处。
 * 注意这里和 mainloop6 **不一样**：mainloop4 只 advance 了一次 prime2，
 * 所以最后的 (sloc,prime2) 落在 2p+2p=4p，而不是 6p。抄错这个数会让整段错位。 */
static const u8 off_sl4[] = {0, 1, 2, 3, 4};

/* p <= sl/5：0..5p */
static const u8 off_sl5[] = {0, 1, 2, 3, 4, 5};

/* p <= sl/3：0,p,2p,3p */
static const u8 off_sl3[] = {0, 1, 2, 3};

/* p <= sl/2：汇编先写 (sloc,p)，再写 (sloc)，最后写 (sloc,prime) 而 prime
 * 已经被 `addq prime,prime` 加倍过，所以是 0,p,2p */
static const u8 off_sl2[] = {0, 1, 2};

/* p <= sl-1：0,p */
static const u8 off_sl1[] = {0, 1};

/* asm3_sievea 的 return8a / loop8a 内层：0,p,2p,3p，每轮走 4p */
static const u8 off_8a[] = {0, 1, 2, 3};

/* return4a / loop4a 内层：0,p,2p,3p，每轮走 4p */
static const u8 off_4a[] = {0, 1, 2, 3};

/* return3a：0,p,2p，然后 sloc += 3p，再写 0,p */
static const u8 off_3a[] = {0, 1, 2};

/* return2a：先写 0,p 两条根，`addq %rdx,%rsi`（rdx=2p）前进 2p，
 * 再钳位写一条。offset 表只有 2 项（0,p），第三处在 advance 之后。 */
static const u8 off_2a[] = {0, 1};

/* return1a：先写 0 两条根，`addq %r14,%rsi` 前进 p，再钳位写一条。 */
static const u8 off_1a[] = {0};

/* 把一段展开里那几条 addb 写完。
 *
 * base 是**某一条根的指针**，已经含了根的偏移（调用点传的是 sv + s）。
 * idx 是**相对 sievearray 数组头**的下标：根偏移 + off[k]*p。
 *
 * 注意必须用 `mpqs3_sievearray[idx]` 解引用，不能写 `base[idx]`：
 * base[idx] 等于 sv + s + idx = sv + 2s + off[k]*p，把根偏移算了两遍。
 * 这会让「哪一条根被展开」整体平移，差分测试立刻炸在
 * asm3_sieve 的每一个段上（连 s=0 的那些也会因为另一条根 s'≠0 而错）。
 */
static void expand(u8 *base, u64 p, u8 lo, const u8 *off, unsigned n)
{
	for (unsigned k = 0; k < n; k++) {
		const u64 idx =
			(u64)(base - mpqs3_sievearray) + (u64)off[k] * p;

		mpqs3_sievearray[idx] += lo;
	}
}

/* ------------------------------------------------------------------
 * asm3_sieve
 */
void asm3_sieve(void)
{
	const u64 svb = mpqs3_sievebegin;          /* movzwq */
	const u64 sl = (u64)(u16)mpqs3_sievelen;   /* movzwq */

	u16 *FB = mpqs3_FB + 2 * svb;
	u16 *FBs = mpqs3_FB_start + 2 * svb;
	u8 *FBl = mpqs3_FB_log + svb;

	u8 *sv = mpqs3_sievearray;
	u8 *sieveend = sv + sl;

	u64 bound = sl >> 3;

	/* ---- 第一段：p <= sl/8。唯一一段排序的，也是唯一有内层循环的 ----
	 *
	 * 汇编是 `movzwq (FB),prime ; cmpq ; leaq 4(FB),FB ; jc`，即**先推进再
	 * 判界**。`leaq 4(FB),FB` 前进 4 字节 = 2 个 u16 元素，因为 mpqs3_FB
	 * 是 ushort 数组，一个素数占 [2i]=p、[2i+1]=p² 两格。FB/FBs 一次前进 2 个
	 * 元素，FBl（uchar 数组）一次前进 1 个字节。写成 `FB++` 会读到 p² 那格。
	 *
	 * `jc` 跳出时 FB 已经推过了当前素数；下一段入口的 `leaq -4(FB),FB`
	 * 正好退回来（见下面的 rewind）。 */
	for (;;) {
		const u64 p = FB[0];
		FB += 2;
		if (p > bound)
			break;

		u64 s1 = FBs[0];
		u64 s2 = FBs[1];
		FBs += 2;

		/* cmpq sloc1,sloc2 ; cmovcq —— 排成 s1 <= s2 */
		if (s2 < s1) {
			const u64 t = s1;
			s1 = s2;
			s2 = t;
		}

		const u8 lo = *FBl++;

		/* leaq (prime,prime,2),aux1 = 3p，这里算完没人用；
		 * leaq (prime,prime),prime2 = 2p。 */
		u8 *q1 = sv + s1;
		u8 *q2 = sv + s2;

		do {   /* jc 在轮尾，是 do-while */
			/* 汇编 loop4 一轮写四条（两条根各两条）、推进 4p：
			 *   (sloc) (sloc,prime) | +2p | (sloc) (sloc,prime) | +2p
			 * 也就是偏移 {0, p, 2p, 3p}、步长 4p。
			 * 别写成「两轮」——那样步长会变成 8p，sl 不是 4p 整数倍
			 * 时就会多写一截。 */
			expand(q1, p, lo, off_sl8, 4);
			expand(q2, p, lo, off_sl8, 4);
			q1 += 4 * p;
			q2 += 4 * p;
		} while (q1 < sieveend);
	}

	/* ---- 后七段：p <= sl/7, sl/6, sl/5, sl/4, sl/3, sl/2, sl-1 ----
	 *
	 * 逐条照抄，不做指针抽象（抽象会差一格）：
	 *
	 *   mainloopN:  movzwq (FB),prime ; cmpq prime,sl4 ; leaq 4(FB),FB ; jc <下一段>
	 *   loopNbegin: ... ; leaq -4(FB),FB
	 *
	 *  a) 段内是「读 p -> 前推一格 -> 判界」。jc 跳出时 FB 已越过当前素数；
	 *     下一段入口 `leaq -4(FB),FB` 退回来，让新段重新读**同一个**素数。
	 *
	 *  b) 段界递增（sl/8, sl/7, ..., sl-1），退回后那个素数必然还满足新界，
	 *     于是被**再处理一次**；FBs/FBl 不退，读到的 s1/s2/lo 已是后面那个
	 *     素数的了。看着像原代码的 bug，但这是实际行为，照抄。
	 *
	 *  c) 每一段的入口 loopNbegin 都有 `leaq -4(FB),FB` —— **包括 loop1begin**，
	 *     所以八段全部 REWIND=1。rewind 之后没有 loopNbegin，只有 `end`。
	 *
	 * 下面用 `rewind` 标志表达：段首退一格 = 汇编里 loopNbegin 那条指令。
	 */
#define SEG7(BOUND, OFF, REWIND)                             \
	do {                                                 \
		const u64 bnd = (BOUND);                     \
		if (REWIND)                                \
			FB -= 2;                         \
		for (;;) {                                    \
			const u64 p = FB[0];                \
			FB += 2;                         \
			if (p > bnd)                      \
				break;                     \
			const u64 s1 = FBs[0];             \
			const u64 s2 = FBs[1];             \
			FBs += 2;                         \
			const u8 lo = *FBl++;              \
			expand(sv + s1, p, lo, (OFF), sizeof(OFF)); \
			expand(sv + s2, p, lo, (OFF), sizeof(OFF)); \
		}                                            \
	} while (0)

	SEG7(sl / 7, off_sl7, 1);
	SEG7(sl / 6, off_sl6, 1);
	SEG7(sl / 5, off_sl5, 1);
	SEG7(sl / 4, off_sl4, 1);
	SEG7(sl / 3, off_sl3, 1);
	SEG7(sl / 2, off_sl2, 1);
	SEG7(sl - 1, off_sl1, 1);   /* loop1begin 也有 leaq -4(FB),FB */

#undef SEG7
}

/* ------------------------------------------------------------------
 * asm3_sievea
 *
 * FB0 的编码：一段是 `0, lo, p, p, ..., 0, 0`。汇编从 FB0[0] 读：
 * 非零就直接当 p 处理；读到 0 就进 update*，那里再读一项 —— 非零就把它当 lo
 * （只取低字节），再读一项当 p；还是 0 就是本段的 `0,0` 收尾，进入下一段。
 *
 * 汇编只处理五段（8a/4a/3a/2a/1a）然后返回；mpqs3.cpp 里填 FB0 的代码写了
 * 六段（/8, /4, /3, /2, /1, 余下全部）。第六段汇编根本不碰。这里照抄汇编，
 * 只处理五段。
 *
 * `movq $0,%eax` 那句是不存在的：汇编入口第一条写 %rax 的是
 * `movzwq mpqs3_sievebegin(%rip),%rax`，所以 %al 的初值是 (u8)sievebegin，
 * 不是 0。FB0 里以裸素数开头的段（也就是沿用 %al 的段）会因此全部错位。
 * 详见头注 6，这里写 (u8)svb 照抄。
 */
void asm3_sievea(void)
{
	const u64 svb = mpqs3_sievebegin;          /* movzwq */
	const u64 sl = (u64)(u16)mpqs3_sievelen;   /* movzwq */

	u16 *FB0 = mpqs3_FB0;
	u16 *FBs = mpqs3_FB_start + 2 * svb;

	u8 *sv = mpqs3_sievearray;
	u8 *const sieveend = sv + sl;

	/* %al 的初值 = 入口 `movzwq mpqs3_sievebegin(%rip),%rax` 的低字节。
	 * 它跨五段保留，只有下面各段的 `lo = (u8)l16`（movb %r14b,%al）
	 * 会改它。写成 0 是错的。 */
	u8 lo = (u8)svb;

	/* ---- 第一段：p <= sl/8。唯一排序的一段 ---- */
	for (;;) {
		u64 p = FB0[0];
		FB0++;
		if (p == 0) {
			const u64 l16 = FB0[0];
			if (l16 == 0) {
				FB0++;
				break;
			}
			lo = (u8)l16;   /* movb %r14b,%al */    /* movb prime_byte,%al */
			p = FB0[1];
			FB0 += 2;
		}

		u64 s1 = FBs[0];
		u64 s2 = FBs[1];
		FBs += 2;
		if (s2 < s1) {
			const u64 t = s1;
			s1 = s2;
			s2 = t;
		}

		u8 *q1 = sv + s1;
		u8 *q2 = sv + s2;

		/* sievebound = sieveend - 3p，循环条件 sloc2 < sievebound */
		u8 *bound = sieveend - 3 * p;

		for (;;) {
			expand(q2, p, lo, off_8a, 4);
			expand(q1, p, lo, off_8a, 4);
			q1 += 4 * p;
			q2 += 4 * p;
			if (!(q2 < bound))
				break;
		}

		bound += 2 * p;
		if (q2 < bound) {
			expand(q1, p, lo, off_8a, 2);
			expand(q2, p, lo, off_8a, 2);
			q1 += 2 * p;
			q2 += 2 * p;
		}

		if (q1 >= sieveend)
			q1 = sieveend;
		if (q2 >= sieveend)
			q2 = sieveend;
		*q1 += lo;
		q1 += p;              /* addq %r14,%rsi */
		*q2 += lo;
		if (q1 >= sieveend)
			q1 = sieveend;
		*q1 += lo;
	}

	/* ---- 第二段：p <= sl/4。不排序。---- */
	for (;;) {
		u64 p = FB0[0];
		FB0++;
		if (p == 0) {
			const u64 l16 = FB0[0];
			if (l16 == 0) {
				FB0++;
				break;
			}
			lo = (u8)l16;   /* movb %r14b,%al */
			p = FB0[1];
			FB0 += 2;
		}

		u8 *q1 = sv + FBs[0];
		u8 *q2 = sv + FBs[1];
		FBs += 2;

		u8 *bound = sieveend - p;   /* mov %r15,%rbx ; sub %r14,%rbx */

		/* 注意：汇编这里**不是循环**。反汇编里 4e1/4e5 推进 4p 之后，
		 * 4e9 `jae check4a` 直接跳出；不跳时执行 4ee..4fd 那一组，然后
		 * **顺序落进 check4a**，没有跳回 4c7。所以最多写两轮。 */
		expand(q2, p, lo, off_4a, 4);
		expand(q1, p, lo, off_4a, 4);
		q1 += 4 * p;
		q2 += 4 * p;
		if (q2 < bound) {
			expand(q1, p, lo, off_4a, 2);
			expand(q2, p, lo, off_4a, 2);
			q1 += 2 * p;
			q2 += 2 * p;
		}

		if (q1 >= sieveend)
			q1 = sieveend;
		if (q2 >= sieveend)
			q2 = sieveend;
		*q1 += lo;
		q1 += p;              /* addq %r14,%rsi */
		*q2 += lo;
		q2 += p;              /* addq %r14,%rdi */
		if (q1 >= sieveend)
			q1 = sieveend;
		if (q2 >= sieveend)
			q2 = sieveend;
		*q1 += lo;
		*q2 += lo;
	}

	/* ---- 第三段：p <= sl/3。不排序。---- */
	for (;;) {
		u64 p = FB0[0];
		FB0++;
		if (p == 0) {
			const u64 l16 = FB0[0];
			if (l16 == 0) {
				FB0++;
				break;
			}
			lo = (u8)l16;   /* movb %r14b,%al */
			p = FB0[1];
			FB0 += 2;
		}

		u8 *q1 = sv + FBs[0];
		u8 *q2 = sv + FBs[1];
		FBs += 2;

		expand(q1, p, lo, off_3a, 3);
		expand(q2, p, lo, off_3a, 3);
		q1 += 3 * p;
		q2 += 3 * p;

		if (q1 >= sieveend)
			q1 = sieveend;
		if (q2 >= sieveend)
			q2 = sieveend;
		*q1 += lo;
		*q2 += lo;
	}

	/* ---- 第四段：p <= sl/2。不排序。---- */
	for (;;) {
		u64 p = FB0[0];
		FB0++;
		if (p == 0) {
			const u64 l16 = FB0[0];
			if (l16 == 0) {
				FB0++;
				break;
			}
			lo = (u8)l16;   /* movb %r14b,%al */
			p = FB0[1];
			FB0 += 2;
		}

		u8 *q1 = sv + FBs[0];
		u8 *q2 = sv + FBs[1];
		FBs += 2;

		expand(q1, p, lo, off_2a, 2);
		expand(q2, p, lo, off_2a, 2);
		q1 += 2 * p;
		q2 += 2 * p;

		if (q1 >= sieveend)
			q1 = sieveend;
		if (q2 >= sieveend)
			q2 = sieveend;
		*q1 += lo;
		*q2 += lo;
	}

	/* ---- 第五段：p <= sl/1。不排序。---- */
	for (;;) {
		u64 p = FB0[0];
		FB0++;
		if (p == 0) {
			const u64 l16 = FB0[0];
			if (l16 == 0) {
				FB0++;
				break;
			}
			lo = (u8)l16;   /* movb %r14b,%al */
			p = FB0[1];
			FB0 += 2;
		}

		u8 *q1 = sv + FBs[0];
		u8 *q2 = sv + FBs[1];
		FBs += 2;

		expand(q1, p, lo, off_1a, 1);
		expand(q2, p, lo, off_1a, 1);
		q1 += p;
		q2 += p;

		if (q1 >= sieveend)
			q1 = sieveend;
		if (q2 >= sieveend)
			q2 = sieveend;
		*q1 += lo;
		*q2 += lo;
	}

	/* 汇编的 loop0begina：leaq 2(FB0),FB0 然后返回。第六段不管。 */
}
}  /* namespace lasieve_ns */
