/* mpqs_sieveinit —— 由 mpqs_sieveinit.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 三个入口：
 *   void asm_sieve_init   (unsigned char *sv, u32_t len, u16 *tab,
 *                          u64 *maskptr, unsigned char *tinyptr, u32_t tinylen)
 *   void asm_sieve_init16 (同上)
 *   void asm_sieve_init0  (unsigned char *ta, u64 *m64, u32_t len)
 *
 * 前两个把 tinyarray 的字节按 mask 累加到 sievearray 上；第三个是 mpqs3 的
 * 「tinyarray 展开 + 加 mask」，纯 MMX 的 64 位移位拷贝。
 *
 * 这三个入口都不读任何全局数组，全部数据从参数进来（asm 里没有一条 %rip）。
 *
 * ---------------------------------------------------------------------------
 * 翻译时照抄的、看起来像笔误但必须原样的语义
 * ---------------------------------------------------------------------------
 * 1) asm_sieve_init 里 `cmpq tptrend,tptr` 之后隔着两条 `paddb` 才 `cmovnc`。
 *    如果 PADD 会改标志位，cmovnc 判的就不是 cmpq 的 CF 了。实测（本机 AMD）
 *    MMX `paddb` **完全不碰** 算术标志位：预设 EFLAGS=0x0cd5 再 paddb，读回来
 *    还是 0x0cd5；预设 0x0202 或 0x0203 也原样透传。所以 cmovnc 判的确实是
 *    cmpq 的 CF。这里照原样写成显式的指针比较，与本机汇编行为一致。
 *    （asm_sieve_init16 的 cmpq 就在 cmovnc 前一条，两边顺序本来就一致。）
 *
 * 2) 源里写 `movd %rax,%mm2`，但 GAS 把它汇编成 **movq**（`48 0f 6e d0`，
 *    带 REX.W 的 MOVQ），mask 是 `mult * mask1` 的完整 64 位，paddb 八个字节
 *    全加。反汇编确认过（objdump: `movq %rax,%mm2`），实测也对得上：
 *    写 32 位版本时 sv[4..7] 与汇编差一个固定的 0xf3。
 *    asm_sieve_init0 里的 `movd %rax,%sh` / `movd %rax,%ish` 同样被编成 movq，
 *    但那两个值本来就小于 64，截不截断都一样。
 *
 *    这条正好解释了为什么 C 参考里是 `mask=0x0101..01*(lo*mult)`（8 字节全加）——
 *    C 参考是对的，「movd 只写低 32 位、高 4 字节为 0」的直觉在这里是错的。
 *
 * 3) mpqs_sinit_tab 的走法：llen=tab[0]；段长 = (tab[2]-llen) 再右移 4 或 5；
 *    每轮 tab += 2；轮末 llen = 新的 tab[0]，等于 len 就停。所以 tab 的布局是
 *    [起点0, 乘数0, 终点0(=起点1), 乘数1, 终点1, ...]，最后那个终点等于 len。
 *    表项按 u16 零扩展后再相减，段长必须 ≥ 1（见第 4 条）。
 *
 * 4) 内层循环是「先做一轮再 decq/jnz」，段长算成 0 会跑完一轮后计数器变成
 *    0xffffffff 再绕回来冲出数组。真实调用点的每段都 ≥ 16（init16 是 ≥ 32）
 *    字节，差分测试也只放非零段。
 *
 * 5) 环形读 tinyarray：tptr 先 +=16 再判断 `>= tptrend`，越界就 -= 16*tinylen。
 *    读操作发生在回绕之前，所以最后一次会读到 tptrend 往后最多 15（init16 是
 *    31）字节。tinyarray 在 mpqs.cpp/mpqs3.cpp 里是按 16*tinylen 分配的，
 *    这里差分测试多垫了 64 字节，两侧读到的是同一块内存。
 *
 * 6) asm_sieve_init0 里 sh = 8*(len&7)（第 1 段）、16*(len&3)（第 2 段）、
 *    32*(len&1)（第 3 段），恒小于 64；ish = 64 - sh，sh==0 时 **ish==64**。
 *    本机实测 psllq/psrlq 的计数语义是「>= 64 结果为 0」，**不是**「取低 6 位」：
 *      psllq(0x8000000000000001, 64) = 0
 *      psllq(0x8000000000000001,  0) = 0x8000000000000001
 *      psllq(0x0123456789abcdef, 65) = 0
 *    所以 sh==0 时三段的移位全变成「清零」，整段 memcpy 退化成
 *    「ta[8k+8..] = ta[8k]」，第 0 组 ta[0..7] 保持原样。C++ 里移位量 >= 64 是
 *    未定义行为，这里必须显式判掉 —— 这是必须照抄的硬件语义，不是保险。
 *
 * 7) asm_sieve_init0 的 mcloopXbegin 处那两条 `leaq 8(...)` 是无条件执行的，
 *    包括 jz 直接跳到 mcloopXend 的那条路径。所以「不进入主循环」时
 *    targ 仍然前进了 8（走奇数分支时是 16）。mcloopXend 的收尾 store 用的就是
 *    这个已经前进过的 targ。
 *
 * 8) 三个 memcpy 段里 `psllq ish` 后接 `psrlq ish`，合起来是「保留低 64-ish 位」；
 *    而单独的 `psrlq ish` 是真右移、把高 ish 位挪下来。ish>0 时两者结果不同
 *    （一个清高位、一个移位），ish==0 时才一致。不要互相简化。
 *
 * 9) asm_sieve_init0 最后一段是 byte 粒度的 paddb（u64 *u32 就用 u64 回绕，
 *    语义不同），必须按字节加、用 _mm_add_epi8，不能用 64 位加法代替。
 *
 * 这里全部用 __m128i 只操作低 64 位来对应 MMX 的 movq/paddb/pxor/psllq ——
 * 上半部分的值不参与任何结果。选 __m128i 而不是 __m64 是为了不跟 x87 状态
 * 混用，汇编里那句 emms 因此不需要（这里没有用 __m64）。
 *
 * movq 不要求对齐，所以 tinyarray / sv 用 loadu / 普通指针搬运。
 */

#include <stdint.h>

#include <emmintrin.h>

#include "siever-config.h"
#include "mpqs-config.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef u16_t u16;
typedef u32_t u32;
typedef u64_t u64;

/* ---- MMX 的 64 位操作 ---- */

/* movq (%p),%mm  —— 高 64 位清零 */
static inline __m128i ld64(const void *p)
{
	return _mm_loadl_epi64((const __m128i *)p);
}

/* movq %mm,(%p) —— 只写低 64 位 */
static inline void st64(void *p, __m128i v)
{
	_mm_storel_epi64((__m128i *)p, v);
}

/* paddb —— 逐字节模 2^8 回绕 */
static inline __m128i paddb(__m128i a, __m128i b)
{
	return _mm_add_epi8(a, b);
}

/* psllq / psrlq 的计数语义（本机实测，见文件头第 6 条）：计数 < 64 是普通移位，
 * 计数 >= 64 结果是 0 —— 不是「把计数取低 6 位」。所以这里不能简单 & 63。 */
static inline __m128i shlq(__m128i x, u64 n)
{
	if (n >= 64)
		return _mm_setzero_si128();
	return _mm_sll_epi64(x, _mm_cvtsi32_si128((int)n));
}

static inline __m128i shrq(__m128i x, u64 n)
{
	if (n >= 64)
		return _mm_setzero_si128();
	return _mm_srl_epi64(x, _mm_cvtsi32_si128((int)n));
}

void asm_sieve_init(unsigned char *sv, u32_t len, u16 *tab, u64 *maskptr,
                    unsigned char *tinyptr, u32_t tinylen)
{
	const unsigned char *tptr = tinyptr;
	const unsigned char *const tptrend = tinyptr + 16 * (size_t)tinylen;
	const size_t tinylen4 = 16 * (size_t)tinylen;
	const u64 mask1 = *maskptr;
	u64 llen = tab[0];		/* 首轮是 0 */

	for (;;) {
		u64 ilen = (u64)(u32)tab[2] - llen;	/* 段长（字节） */
		__m128i mask;
		ilen >>= 4;		/* 段长 / 16 */
		/* 源里写的是 `movd %rax,%mm2`，但 GAS 汇编成 `movq %rax,%mm2`
		 * （48 0f 6e d0，REX.W 的 MOVQ），所以高 32 位也进了 MMX 寄存器，
		 * paddb 是 8 个字节全加。见文件头第 2 条。 */
		mask = _mm_cvtsi64_si128((long long)((u64)(u32)tab[1] * mask1));
		tab += 2;

		do {
			__m128i mm0 = ld64(tptr);
			__m128i mm1 = ld64(tptr + 8);
			size_t back;
			tptr += 16;
			/* cmovnc tinylen4：CF 来自上面的 cmpq（paddb 不改标志位） */
			back = (tptr >= tptrend) ? tinylen4 : 0;
			mm0 = paddb(mm0, mask);
			mm1 = paddb(mm1, mask);
			tptr -= back;
			--ilen;
			st64(sv, mm0);
			st64(sv + 8, mm1);
			sv += 16;
		} while (ilen != 0);

		llen = tab[0];
		if (llen == (u64)len)
			break;
	}
}

void asm_sieve_init16(unsigned char *sv, u32_t len, u16 *tab, u64 *maskptr,
                      unsigned char *tinyptr, u32_t tinylen)
{
	const unsigned char *tptr = tinyptr;
	const unsigned char *const tptrend = tinyptr + 16 * (size_t)tinylen;
	const size_t tinylen4 = 16 * (size_t)tinylen;
	const u64 mask1 = *maskptr;
	u64 llen = tab[0];

	for (;;) {
		u64 ilen = (u64)(u32)tab[2] - llen;
		__m128i mask;
		ilen >>= 5;		/* 段长 / 32 */
		mask = _mm_cvtsi64_si128((long long)((u64)(u32)tab[1] * mask1));
		tab += 2;

		do {
			__m128i mm0 = ld64(tptr);
			__m128i mm1 = ld64(tptr + 8);
			__m128i mm2 = ld64(tptr + 16);
			__m128i mm3 = ld64(tptr + 24);
			size_t back;
			tptr += 32;
			mm0 = paddb(mm0, mask);
			mm1 = paddb(mm1, mask);
			back = (tptr >= tptrend) ? tinylen4 : 0;
			mm2 = paddb(mm2, mask);
			mm3 = paddb(mm3, mask);
			tptr -= back;
			--ilen;
			st64(sv, mm0);
			st64(sv + 8, mm1);
			st64(sv + 16, mm2);
			st64(sv + 24, mm3);
			sv += 32;
		} while (ilen != 0);

		llen = tab[0];
		if (llen == (u64)len)
			break;
	}
}

/* 一段「按 shift 位移位」的 memcpy：把 src 的内容按 sh 位对齐后铺到
 * targ = src + 8*(len>>s)，第 2/3 段的 sh 分别是 16*(len&3) / 32*(len&1)。 */
static inline void expand_shifted(unsigned char *ta, u32_t len, int s,
                                  unsigned int lomask, int shiftbase)
{
	unsigned char *src = ta;
	unsigned char *targ = ta + 8 * (size_t)(len >> s);
	const u64 sh = ((u64)len & lomask) << shiftbase;
	const u64 ish = 64 - sh;	/* sh==0 时 ish==64，移位结果为 0 */
	u64 len8 = len >> s;
	__m128i ms, mt, ms0, msz, mtz;

	ms = ld64(src);
	mt = ld64(targ);
	ms0 = ms;
	mt = shrq(shlq(mt, ish), ish);
	ms = shlq(ms, sh);
	mt = _mm_xor_si128(ms, mt);
	st64(targ, mt);

	if (len8 & 1) {			/* 奇数：多做一轮并把指针各推 8 */
		--len8;
		ms = ld64(src + 8);
		mt = ms0;
		src += 8;
		targ += 8;
		mt = shrq(mt, ish);
		ms0 = ms;
		ms = shlq(ms, sh);
		mt = _mm_xor_si128(ms, mt);
		st64(targ, mt);
	}
	src += 8;		/* 无条件执行，jz 走掉时也一样 */
	targ += 8;
	if (len8 != 0) {
		len8 >>= 1;
		do {
			ms = ld64(src);
			msz = ld64(src + 8);
			mt = shrq(ms0, ish);
			mtz = shrq(ms, ish);
			ms0 = msz;
			ms = shlq(ms, sh);
			msz = shlq(msz, sh);
			mt = _mm_xor_si128(ms, mt);
			mtz = _mm_xor_si128(msz, mtz);
			st64(targ, mt);
			st64(targ + 8, mtz);
			src += 16;
			targ += 16;
		} while (--len8 != 0);
	}
	ms0 = shrq(ms0, ish);
	st64(targ, ms0);
}

void asm_sieve_init0(unsigned char *ta, u64 *m64, u32_t len)
{
	unsigned char *src, *targ;
	u64 len8;
	__m128i ms, mt, mtz, ms0, msz, ms0z;

	/* memcpy(ta+len,     ta,     len)     移位 8*(len&7)  位 */
	expand_shifted(ta, len, 3, 7u, 3);
	/* memcpy(ta+2*len,   ta,     2*len)   移位 16*(len&3) 位 */
	expand_shifted(ta, len, 2, 3u, 4);
	/* memcpy(ta+4*len,   ta,     4*len)   移位 32*(len&1) 位 */
	expand_shifted(ta, len, 1, 1u, 5);

	/* 交错加 mask0 / mask1，同时把加过的值复制到 ta+8*len。
	 * 注释里的伪码是：
	 *   ullsv=(u64_t*)ta; ullsvend=ullsv+2*len+2;
	 *   while (ullsv<ullsvend) { *ullsv+++=mask0; *ullsv+++=mask1; }
	 * 汇编是成对处理 16 字节的，所以这里一轮吃 src/targ 各 16 字节。 */
	src = ta;
	targ = ta + 8 * (size_t)len;
	mt = _mm_cvtsi64_si128((long long)m64[0]);	/* mask0 */
	mtz = _mm_cvtsi64_si128((long long)m64[1]);	/* mask1 */
	ms = ld64(src);
	ms0 = ms;
	ms = paddb(ms, mt);
	ms0 = paddb(ms0, mtz);
	st64(src, ms);
	st64(targ, ms0);

	len8 = len >> 1;		/* 尾数由 mcloop4end 兜住 */
	src += 8;
	targ += 8;
	if (len8 != 0) {
		do {
			ms = ld64(src);
			msz = ld64(src + 8);
			ms0 = ms;
			ms0z = msz;
			ms = paddb(ms, mtz);
			msz = paddb(msz, mt);
			ms0 = paddb(ms0, mt);
			ms0z = paddb(ms0z, mtz);
			st64(src, ms);
			st64(src + 8, msz);
			st64(targ, ms0);
			st64(targ + 8, ms0z);
			src += 16;
			targ += 16;
		} while (--len8 != 0);
	}
	/* 收尾：把 ta 的头 16 字节再写一遍到 targ */
	src = ta;
	ms = ld64(src);
	msz = ld64(src + 8);
	st64(targ, ms);
	st64(targ + 8, msz);
}
}  /* namespace lasieve_ns */
