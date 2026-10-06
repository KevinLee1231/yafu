/* mpqs3_td.cpp —— 由 factor/nfs/lasieve/asm/mpqs3_td.asm 翻译而来
 * (Copyright (C) 2004 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 两个入口：
 *   asm3_tdsieve(u16_t *fb, u16_t *fbs, u16_t **buf, u16_t disp) -> u32_t
 *   asm3_td(u16_t *relptr, u16_t minus, u32_t *qx)               -> u32_t
 *
 * ===================================================================
 * 一、asm3_tdsieve：把 4 个素数的残基累加进一个 64 位字节累加器，
 *    再按字节散列到 buf[] 指向的幸存者表里。
 * ===================================================================
 *
 * 1. acc 是 u64，每次「加一个字节再左移 8」。8 字节正好塞满 64 位，所以
 *    tds4innerloop 最后一拍**没有**再左移（汇编里那条 shlq 是故意缺的，
 *    照抄）。尾巴那 4 拍和收尾那 3 拍的移位次数各不相同，也照抄。
 *    写成 acc = (acc + sv[i]) << 8 就是逐位等价。
 *
 * 2. 散列是「从最低字节往上走」：acc & 0xff 取出一个字节值 b，
 *    非 0 就往 buf[b] 指向的位置写一个 disp 并把指针 +2；
 *    然后 acc >>= 8，**直到 acc 变成 0 才停**。
 *    所以中间那个 0 字节不会截断循环（只要高位还有非 0），只有高字节
 *    全 0 才停。写循环时不能写成「固定 8 次」。
 *
 * 3. tds4check 里把 sloc1/sloc2 夹到 sieveend 之后会读 sv[sievelen]，
 *    也就是数组末尾第 0 个字节。调用方靠
 *        mpqs3_sievearray[mpqs3_sievelen]=0;
 *    把它清零，汇编那边 movqb 会真的读那一字节。差分测试必须照样分配
 *    sievelen+1 字节并把最后一字节填 0。
 *
 * 4. 返回值恒为 0。汇编 tds4loopend 就是 xorq %rax,%rax。
 *    .if 0 块里那段 32 位老代码本来是 movl fb,%eax; subl fbs,%eax;
 *    shrl $1,%eax（返回处理的素数个数），64 位改写时这段被留在 .if 0
 *    里没启用，于是返回 0。调用点 asm3_tdsieve 整段被
 *    「#error ASM_MPQS3_TDSIEVE」关掉了，所以这个 0 没造成实际影响。
 *    照抄，不修。
 *
 * 5. movzwq mpqs3_sievelen(%rip),%r15 是**16 位**读一个 u32 全局。
 *    真实取值 <= MPQS3_SIEVELEN = 2^15，截不出差别，但仍按 16 位读。
 *
 * 6. sloc1/sloc2 交换那两条 cmovcq 的条件位都来自同一条
 *    cmpq %r8,%r9，中间只有 movq（不改标志），所以是「sloc2 < sloc1
 *    就交换」，结果是 sloc1 <= sloc2（相等时不换）。
 *
 * 7. 边界契约：调用方保证 fbs 的值 < p 且 p <= mpqs3_sievelen/4，
 *    于是 p1[prime]、p1[prime]+prime2 这些下标都 < sievelen。
 *    第一次进 tds4innerloop 前**没有**任何边界检查，靠的就是这个契约。
 *
 * ===================================================================
 * 二、asm3_td：先试除已经在筛选阶段找到的素数，再试除 FBk / Adiv。
 * ===================================================================
 *
 * 1. ind 广播：movzwq (%rdi),%rax 取 ind，然后两次 shl/or 把 ind 复制成
 *    4 个 16 位字，再 movd %rax,%xmm5/movd %rax,%xmm3 送进 xmm。
 *    **注意 movd 在 GAS 里遇到 64 位源寄存器会编成 movq（64 位）**，
 *    我 objdump 过确认是 movq，所以低 64 位是 ind,ind,ind,ind，高 64 位
 *    是 0；pslldq $8 把它挪到高半边，paddw 之后得到**均匀**的
 *    [ind]*8。这里要是按 32 位 movd 理解就会得到
 *    [ind,ind,0,0,ind,ind,0,0]，整段筛选结果就全错了。
 *
 * 2. 命中判据（照抄那四条）：
 *        xmm4 = p - s + ind          (psubw / paddw，模 2^16 回绕)
 *        xmm4 = lo16(xmm4 * inv)      pmullw
 *        xmm4 = hi16(xmm4 * p)        pmulhuw
 *        hit  = (xmm4 == 0)           pcmpeqw
 *    **pmulhuw 是覆盖写，不是累加**。Intel 手册里 PMULHUW 的
 *    DEST[i] := (SRC1[i]*SRC2[i])[31:16]，没有 dst += 这一项；
 *    pmullw / pmulhw / pmulhuw 是同一个乘积的低/高两半，彼此独立。
 *    我第一版照搬 Montgomery 的常见写法，在 pmulhuw 之后又加了一次
 *    lo（写成 lo + _mm_mulhi_epu16(lo, p)），结果 lane 全部偏掉。
 *    用 0x3333*5 = 0xffff 单独测过：纯高乘得 0，lo+高乘得 13107，
 *    汇编实测给 0，所以正确写法就是 hi16(lo*p) 本身。
 *    于是命中条件其实是 hi16(lo16(t*inv) * p) == 0，也就是
 *    lo16(t*inv) * p < 2^16。
 *    注意 pmulhuw 的第二个操作数是 **p**（xmm0）不是 inv，而且乘的是
 *    pmullw 之后的 16 位结果，不是原始的 t。这是 v1 mpqs_td.asm 里
 *    一模一样的写法，属于既有惯用法，不是本文件写错。
 *    pmulhuw 是**无符号**高乘，对应 _mm_mulhi_epu16。
 *
 * 3. 掩码折叠：pmovmskb 给 16 位，每个 16 位 lane 占 2 位；一个素数占
 *    4 位（两个 lane × 2 字节）。汇编做 mask |= (mask >> 2) 之后，
 *    「素数 r 命中」会把第 4r..4r+3 整段置 1，取第 4r 位即「该素数任一
 *    lane 命中」。这正对应 C 参考的 if (!ls1 || !ls2)。
 *    随后那串 shrl / movw / adcq $0,%nr / incw 就是逐位把这一位取出来。
 *    adcq $0 读的进位**就是** shrl 刚移出去的那一位，等价于
 *    nr += bit，我直接写成 `nr += (v >> k) & 1` 并在注释里说明；逐位一致。
 *    位号是本次迁移最容易写错的地方（我第一版就写成 1/5/9/13 了）：
 *    shrl $N 移出去的是移位**前**的第 N-1 位，所以
 *      group>0：$1,$4,$4,$4 依次取 v 的第 0、4、8、12 位（4 个素数）
 *      group 0：$5,$4,$4  依次取 v 的第     4、8、12 位（3 个素数）
 *    第一组另外 and $0xfff0 掐掉素数 0 那两个 lane（掩码的 0..3 共 4 位）。
 *    FB_inv_info[0,1] / [8,9] 在 mpqs3.cpp 里从没被填过（填充循环从
 *    j=1 起），所以第一组只记 nFBk_1+1、+2、+3 三个。C 参考的循环
 *    也是 j=1 起。
 *
 * 4. 乘积是 128 位累乘：prodloop1 用 mulq 在不溢出的前提下只算 64 位，
 *    一旦高 64 位非 0（jnc 不成立）就切到 prodloop2 走通用 128 位乘法。
 *    两条路径合起来等价于 acc = (__uint128_t)acc * f，我用 __uint128_t
 *    一次写完，模 2^128 的截断行为一致。
 *    初值 1（rax=1, rdx=0），注释特意说明 nr=0 时也要有定义。
 *
 * 5. 试除前那步 128 位比较：
 *        hi:lo = (prod << 32)，再减掉零扩展成 128 位的 96 位 qx，
 *        jc 说明借位，即 prod*2^32 < qx，返回 0。
 *    汇编用 sbbq 读借位；我用 __uint128_t 直接比大小，判据完全相同。
 *
 * 6. 试除是蒙哥马利式：qxd = qx[0] * inv(prod0) mod 2^32，
 *    对每个素数 p 反复算 rr = qxd * inv(p) mod 2^32，
 *    再看 rr * p 的高 32 位是否为 0（testl %edx,%edx; jnz）。
 *    高位为 0 说明整除，qxd = rr 并记录该素数，继续用同一个 p；
 *    高位非 0 说明不整除，换下一个素数。
 *    mpqs3_FB_inv / FBk_inv / FB_A_inv 都必须真的是对应素数的 32 位逆元。
 *
 * 7. 素数表的下标基址是 &FB[0] - 4*nFBk_1，索引 r13*4 之后得到
 *    FB[2*(ii - nFBk_1)] 和 FB_inv[ii - nFBk_1]，与 C 参考
 *    FB[2*(rels[i][7+j]-mpqs3_nFBk_1)] 一致。
 *
 * 8. 试除遍历方向是**从后往前**：rsi 从 nr 递减，读 relptr[6+rsi]
 *    再 dec。C 参考是 j=0 正向。素数集合一样，但写进 relptr 的**顺序**
 *    不同，差分测试逐字节比内存，所以必须照汇编的反向。
 *    同样，rsi 是在 minus 标记和 2 的标记写进去**之前**保存的
 *    （movq %r9,%rsi 紧跟 testq $1,%minus），所以试除只覆盖筛选阶段
 *    找到的那批素数，不含 -1 和 2 —— 和 C 参考用局部 nr 而非
 *    rels[i][6] 的写法一致。
 *
 * 9. 三个循环的 nr 上限是 25（NMAXDIV，m4 宏），**不是**
 *    MPQS3_TD_MAX_NDIV(=29)。posloop 用 64 位比较，后两个用
 *    16 位比较（cmpw $25,%r9w）。差分测试要在 nr 逼近 25 的地方取点。
 *
 * 10. FBk / Adiv 循环都是「rsi = 计数+1，进来先 dec」，所以下标是
 *     rsi-1。FBk 记录值是 rsi（对应 C 的 1+j），Adiv 记录值是
 *     nFB + nFBk + rsi（对应 C 的 1+nFB+nFBk+j），都取低 16 位。
 *
 * 11. FBk 循环结束后判的是 %eax 而不是 %r8d（tdendk: cmpl $1,%eax），
 *     但那条路径上 eax 刚被 movl %r8d,%eax 刷新过，等价于判 qxd==1。
 *     Adiv 循环后直接落到 end，不再判一次。
 *
 * 12. 返回值是最终 qxd（低 32 位）。gotonext 路径返回 0，调用方把 0
 *     当作「丢弃这个候选」。
 *
 * 13. 载入一律用 loadu，汇编那边是 movaps（要求 16 字节对齐）。取值
 *     完全一样，少一个「数组碰巧没对齐就崩」的隐患。
 *
 * 14. 结尾的 emms 是 MMX 状态复位，这里全程只用 XMM，不需要照搬。
 */

#include <stdint.h>

#include <emmintrin.h>
#include <x86intrin.h>

#include "siever-config.h"
#include "mpqs-config.h"

typedef u16_t u16;
typedef u32_t u32;
typedef u64_t u64;
typedef unsigned char u8;
typedef unsigned __int128 u128;

/* mpqs3.cpp 里定义的全局。文件作用域变量在 C++ 里不做名字改编，
 * 子 Makefile 按 I 值用 -Dmpqs3_FB_start=mpqs3_FB_startI11 统一改名，
 * 这里跟着走即可。 */
extern u32 mpqs3_sievelen;
extern u8 *mpqs3_sievearray;

extern u16 mpqs3_FB[];         /* 2*MPQS3_MAX_FBXSIZE */
extern u16 mpqs3_FB_start[];   /* 2*MPQS3_MAX_FBXSIZE，每素数两个 u16 */
extern u16 mpqs3_FB_inv_info[];/* 4*MPQS3_MAX_NPRIMES */
extern u16 mpqs3_nFBk_1, mpqs3_nFBk;
extern u16 mpqs3_FBk[3];
extern u16 mpqs3_nFB, mpqs3_nAdiv_total, mpqs3_td_begin;
extern u16 mpqs3_Adiv_all[];   /* MPQS3_MAX_ADIV_ALL */
extern u32 mpqs3_FB_inv[];     /* MPQS3_MAX_FBXSIZE */
extern u32 mpqs3_FBk_inv[3];
extern u32 mpqs3_FB_A_inv[];   /* MPQS3_MAX_ADIV_ALL */
extern u8 mpqs3_256_inv_table[128];

/* asm3_tdsieve 在 mpqs-config.h 里是注释掉的，声明放在这里。
 * 放在 extern "C" 里，定义才和汇编那侧的符号对得上。 */
extern "C" {
u32_t asm3_tdsieve(u16_t *, u16_t *, u16_t **, u16_t);
}

/* ===================================================================
 * asm3_tdsieve
 * =================================================================== */

u32_t asm3_tdsieve(u16_t *fb, u16_t *fbs, u16_t **buf, u16_t disp)
{
	u8 *const sv = mpqs3_sievearray;
	/* movzwq：16 位读，见头注 5 */
	const u64 sl = (u64)(u16)mpqs3_sievelen;
	const u64 pend = sl >> 2;
	u8 *const svend = sv + sl;

	for (;;) {
		const u64 prime = fb[0];
		if (prime >= pend)		/* cmpq pend,prime ; jnc */
			return 0;		/* 见头注 4：恒返回 0 */

		fb += 2;
		++disp;

		u64 sl1 = fbs[0], sl2 = fbs[1];
		fbs += 2;
		/* 两条 cmovcq 共用 cmpq 的条件位，等价于「sl2 < sl1 就换」 */
		if (sl2 < sl1) {
			u64 t = sl1;
			sl1 = sl2;
			sl2 = t;
		}

		const u64 prime2 = prime * 2;
		/* 汇编里 sloc1/sloc2/sievebound/sieveend 全都是 64 位**整数**
		 * （movq %r12,%r13 之类），比较也是整数比较，不是指针比较。
		 * 所以 sievebound = sieveend - 3*prime 必须用 u64 算：prime
		 * 一旦大过数组长度，指针表达式 svend - prime 会落到数组之外，
		 * 之后的 p2 < bound 就是拿无关地址比大小，属未定义行为，
		 * 编译器会直接优化掉循环条件（实测 -Ofast 下整个内层循环
		 * 只跑一轮就退出，少散列很多桶）。
		 * 这里统一用下标做整数运算，和汇编逐位一致。 */
		const u64 bound0 = (u64)sl - prime - prime2;
		u64 o1 = sl1, o2 = sl2, bound = bound0, acc = 0;

		for (;;) {			/* tds4innerloop */
			acc = (acc + sv[o1]) << 8;
			acc = (acc + sv[o2]) << 8;
			acc = (acc + sv[o1 + prime]) << 8;
			acc = (acc + sv[o2 + prime]) << 8;
			o1 += prime2;
			o2 += prime2;
			acc = (acc + sv[o1]) << 8;
			acc = (acc + sv[o2]) << 8;
			acc = (acc + sv[o1 + prime]) << 8;
			/* 最后一拍故意不左移，见头注 1 */
			acc = acc + sv[o2 + prime];
			o1 += prime2;
			o2 += prime2;

			if (acc) {
				/* tds4storeloop，见头注 2 */
				for (;;) {
					const u64 b = acc & 0xff;
					if (b) {
						u16 **slot = buf + b;
						u16 *dst = *slot;
						*dst = disp;
						*slot = dst + 1;
					}
					acc >>= 8;
					if (!acc)
						break;
				}
			}

			/* tds4store_return */
			if (o2 < bound)
				continue;
			bound += prime2;
			if (o2 >= bound)
				break;		/* -> tds4check */

			/* 这 4 拍每一拍后面都有 shlq $8 */
			acc = (acc + sv[o1]) << 8;
			acc = (acc + sv[o2]) << 8;
			acc = (acc + sv[o1 + prime]) << 8;
			acc = (acc + sv[o2 + prime]) << 8;
			o1 += prime2;
			o2 += prime2;
			/* 汇编在这 4 拍之后是**顺序落到 tds4check**，
			 * 不再跳回 tds4innerloop。写成 continue 会多跑一轮
			 * 内层循环，多散列一批字节（实测 prime=5 时多一轮）。
			 * 所以这里必须 break 出内层循环。 */
			break;		/* -> tds4check */
		}

		/* tds4check：夹到 sieveend，会读 sv[sievelen]，见头注 3。
		 * 汇编是 cmovncq，判据也是整数比较。 */
		if (o1 >= sl)
			o1 = sl;
		if (o2 >= sl)
			o2 = sl;
		acc = (acc + sv[o1]) << 8;
		o1 += prime;
		acc = (acc + sv[o2]) << 8;
		if (o1 >= sl)
			o1 = sl;
		acc += sv[o1];		/* 同样不左移 */
		if (!acc)
			continue;	/* jz tds4loop：本素数一个幸存者都没有 */

		/* tds4store0loop：与上面的散列循环逐条相同 */
		for (;;) {
			const u64 b = acc & 0xff;
			if (b) {
				u16 **slot = buf + b;
				u16 *dst = *slot;
				*dst = disp;
				*slot = dst + 1;
			}
			acc >>= 8;
			if (!acc)
				break;
		}
	}
}

/* ===================================================================
 * asm3_td
 * =================================================================== */

/* 32 位 Newton 求逆，形状与 mpqs3.cpp 的 mpqs3_inv 一致。
 * 汇编里就是内联展开的这一段（mull / andl / mull / subl）。 */
static inline u32 td_inv32(u32 a)
{
	u32 inv = mpqs3_256_inv_table[(a & 0xff) >> 1];
	u32 h = a * inv;
	h &= 0xff00;
	h *= inv;
	inv -= h;
	h = a * inv;
	h &= 0xffff0000;
	h *= inv;
	inv -= h;
	return inv;
}

/* 单个素数的蒙哥马利试除循环。dividend 进出都是 qxd。
 * 返回 false 表示 nr 撞到 25 上限，调用方要走 gotonext。 */
static inline bool td_div_once(u32 p, u32 pinv, u32 *qxd, u16 *relptr,
			       u64 *nr, u64 limit, u16 record)
{
	for (;;) {
		const u32 rr = *qxd * pinv;	/* mull %ebx，只取低 32 位 */
		/* mull %ecx 后 testl %edx,%edx：rr*p 的高 32 位非 0 即不整除 */
		if (((u64)rr * p) >> 32)
			return true;
		if ((*nr & 0xffff) >= limit)
			return false;
		relptr[7 + *nr] = record;	/* movw，无条件写 */
		++*nr;			/* incq，无条件加 */
		*qxd = rr;
	}
}

u32_t asm3_td(u16_t *relptr, u16_t minus, u32_t *qx)
{
	/* qx 是 96 位，汇编摆成 r14(高 32) : r8(低 64) */
	u64 qx0 = (u64)qx[0] | ((u64)qx[1] << 32);
	u64 qx1 = (u64)qx[2];

	const u16 ind = relptr[0];
	u64 nr = relptr[6];

	/* 头注 1：movd 实际编成 movq，得到均匀的 [ind]*8 */
	const __m128i indv = _mm_set1_epi16((short)ind);
	const __m128i zero = _mm_setzero_si128();

	/* ---- 筛选阶段：逐组找素因子 ---- */
	{
		const u16 *info = mpqs3_FB_inv_info;
		const u16 *fbs = mpqs3_FB_start;
		const u64 tdbegin = (u64)(u16)mpqs3_td_begin;
		u16 aux5w = (u16)(mpqs3_nFBk_1 + 4);
		u64 rcx = tdbegin;

		for (u32_t g = 0;; ++g) {
			const __m128i p = _mm_loadu_si128(
				(const __m128i *)(const void *)(info + 16 * g));
			const __m128i iv = _mm_loadu_si128(
				(const __m128i *)(const void *)(info + 16 * g + 8));
			const __m128i s = _mm_loadu_si128(
				(const __m128i *)(const void *)(fbs + 8 * g));

			/* t = p - s + ind，全部模 2^16 */
			__m128i t = _mm_add_epi16(_mm_sub_epi16(p, s), indv);
			/* lo16(t*inv)，然后 hi16(lo*p)，见头注 2。
			 * pmulhuw 覆盖写，不能再加一次 lo。 */
			const __m128i lo = _mm_mullo_epi16(t, iv);
			t = _mm_mulhi_epu16(lo, p);

			u32 m = (u32)_mm_movemask_epi8(
				_mm_cmpeq_epi16(t, zero));
			if (g == 0)
				m &= 0xfff0u;	/* 丢掉素数 0 */

			if (m) {
				/* 头注 3：折叠成「每素数一位」。
				 *
				 * m = pmovmskb 的 16 位；一个 16 位 lane 占 2 位，
				 * 一个素数占两个 lane 共 4 位。v = m | (m>>2) 之后
				 * 「素数 r 命中」会把 v 的第 4r..4r+3 整段置 1，
				 * 相邻素数之间有 2 位重叠，所以汇编取的那一位
				 * 对每个素数都是可靠的。
				 *
				 * 汇编随后用一串 shrl 把位一位位移进进位、
				 * 紧跟 adcq $0,%nr 累加，等价于「读第 k 位」。
				 * 关键：shrl $N 移出去的是移位**前**的第 N-1 位，
				 * 所以 group>0 的 $1,$4,$4,$4 依次取的是
				 * v 的第 0、4、8、12 位；group 0 的 $5,$4,$4 依次取
				 * 第 4、8、12 位。位号是 0/4/8/12，不是 1/5/9/13。
				 */
				const u32 v = m | (m >> 2);
				/* 第一组跳过素数 0（首个 shr 是 $5 而不是 $1，
				 * 且只取 3 次），所以起始位是 4。 */
				const u32 first = (g == 0) ? 4u : 0u;
				const u32 cnt = (g == 0) ? 3u : 4u;
				u16 rec = (u16)(aux5w - ((g == 0) ? 3 : 4));
				for (u32_t j = 0; j < cnt; ++j) {
					const u32 bit =
						(v >> (first + 4 * j)) & 1u;
					relptr[7 + nr] = rec;	/* movw */
					nr += bit;		/* adcq $0 */
					++rec;			/* incw */
				}
			}

			aux5w = (u16)(aux5w + 4);	/* loop2: addw $4 */
			rcx -= 4;			/* subq $4 */
			if (!rcx)			/* jz prod */
				break;
		}
	}

	/* ---- prod：128 位累乘 ---- */
	u128 prod = 1;
	for (u64 i = 0; i < nr; ++i) {
		const u64 ii = relptr[7 + i];
		const u64 f = mpqs3_FB[2 * (ii - (u64)mpqs3_nFBk_1)];
		prod = prod * (u64)f;		/* 模 2^128 截断 */
	}

	/* rsi = nr 必须在 minus / 2 的标记写进去**之前**保存，见头注 8 */
	u64 nr1 = nr;

	if (minus & 1) {
		relptr[7 + nr] = 0;
		++nr;
	}

	/* ---- posloop：qx 为偶就记 nFBk_1（代表 -1）并右移 ---- */
	while (!(qx0 & 1)) {
		++nr;
		if (nr >= 25)		/* cmpq $25,%r9 ; jnc gotonext */
			return 0;
		relptr[6 + nr] = mpqs3_nFBk_1;
		const u64 c = qx1 & 1;
		qx1 >>= 1;
		qx0 = (qx0 >> 1) | (c << 63);
	}

	/* ---- division：prod*2^32 与 qx 比较，见头注 5 ---- */
	{
		const u128 q96 = ((u128)qx[2] << 64) |
				 ((u128)qx[1] << 32) | (u128)qx[0];
		if ((prod << 32) < q96)
			return 0;
	}

	/* ---- 蒙哥马利试除：qxd = qx[0] * inv(prod0) ---- */
	u32 qxd = (u32)qx0 * td_inv32((u32)prod);

	/* 筛选阶段找到的素数，反向遍历，见头注 8 */
	while (nr1) {
		const u64 ii = relptr[6 + nr1];
		--nr1;
		const u32 p = mpqs3_FB[2 * (ii - (u64)mpqs3_nFBk_1)];
		const u32 pinv = mpqs3_FB_inv[ii - (u64)mpqs3_nFBk_1];
		if (!td_div_once(p, pinv, &qxd, relptr, &nr, 25,
				(u16)ii))
			return 0;
	}

	/* ---- FBk 小素数 ---- */
	if (qxd == 1)
		goto end;
	{
		u64 k = (u64)mpqs3_nFBk + 1;
		for (;;) {
			--k;
			if (!k) {
				if (qxd == 1)
					goto end;
				break;
			}
			const u32 p = mpqs3_FBk[k - 1];
			const u32 pinv = mpqs3_FBk_inv[k - 1];
			if (!td_div_once(p, pinv, &qxd, relptr, &nr, 25,
					(u16)k))
				return 0;
		}
	}

	/* ---- Adiv_all ---- */
	{
		u16 rec = (u16)(mpqs3_nFB + mpqs3_nFBk);
		u64 k = (u64)mpqs3_nAdiv_total + 1;
		for (;;) {
			--k;
			if (!k)
				break;		/* tdenda 直接落 end */
			const u32 p = mpqs3_Adiv_all[k - 1];
			const u32 pinv = mpqs3_FB_A_inv[k - 1];
			const u16 store = (u16)(rec + (u16)k);
			if (!td_div_once(p, pinv, &qxd, relptr, &nr, 25,
					store))
				return 0;
		}
	}

end:
	relptr[6] = (u16)nr;		/* movw %r9w,12(%rdi) */
	return qxd;
}
