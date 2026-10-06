/* mpqs_nextpol —— 由 mpqs_nextpol.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 六个入口，都是 16 位 SIMD 上的定点算术：更新下一段多项式的起点。整段没有
 * 一条读 x86 标志位 —— 循环末尾的 jnz 只数迭代次数，所以逐条 intrinsics 翻译
 * 即可，不需要任何标量回退。
 *
 * 翻译时照抄的几点：
 *
 * 1. 所有加减都是 paddw / psubw，即模 2^16 回绕，不是饱和也不是升到 32 位。
 *
 * 2. 那个反复出现的「if (>= p) -= p」在汇编里是四步：
 *        movaps res,%tmp; paddw one,%tmp; pcmpgtw p,%tmp; pand p,%tmp; psubw %tmp,res
 *    判的是 (res+1) > p，按有符号 16 位比。res = 0xffff 时 res+1 回绕成 0，
 *    判不出来就不减 p。这个边界必须照抄，不能改成无符号比较。
 *
 * 3. 蒙哥马利约减的进位是「h==0 才加 1」，靠 pcmpeqw 取全 1 再 pand one
 *    变成 0/1。
 *
 * 4. 指针步长和每轮消耗的字数不是一回事，照抄汇编的 leaq：
 *      np11 / np10 / np3plus_xmm / np3minus_xmm：np_px 步长 16 个 u16
 *      np3plus / np3minus（MMX）：np_p 步长 8 个 u16
 *    步长比消耗量大一倍是原样如此，不是笔误。
 *
 * 载入用 loadu 而不是 load：汇编那边是 movaps，要求 16 字节对齐；这里放宽成
 * 不对齐，取到的值完全一样，但少一个「数组碰巧没对齐就崩」的隐患。这些数组
 * 在 mpqs.cpp 里是普通的 unsigned short 数组，对齐只有 2。
 */

#include <stdint.h>

#include <emmintrin.h>

#include "siever-config.h"
#include "mpqs-config.h"

typedef u16_t u16;
typedef u32_t u32;

/* mpqs.cpp 里定义的那几个数组。文件作用域的变量在 C++ 里不做名字改编，
 * 子 Makefile 按 I 值用 -Dmpqs_FB_start=mpqs_FB_startI11 统一改名，这里跟着
 * 走就行。 */
extern u16 mpqs_FB_start[];      /* 2*MPQS_MAX_FBSIZE：每素数两个 u16 */
extern u16 mpqs_FB_np_px[];      /* 2*MPQS_MAX_FBSIZE */
extern u16 mpqs_FB_np_p[];       /* 2*MPQS_MAX_FBSIZE */
extern u16 mpqs_FB_disp[];       /* MPQS_MAX_FBSIZE */
extern u16 mpqs_FB_mm_inv[];     /* MPQS_MAX_FBSIZE */

static inline __m128i zero128(void)
{
	return _mm_setzero_si128();
}

/* res -= (res >= p ? p : 0)，判据照抄汇编的 (res+1) > p（有符号 16 位）。 */
static inline __m128i mod_p(__m128i res, __m128i p, __m128i one)
{
	__m128i t = _mm_cmpgt_epi16(_mm_add_epi16(res, one), p);
	return _mm_sub_epi16(res, _mm_and_si128(t, p));
}

/* 蒙哥马利约减：out = high(a*b) + high(low(a*b)*mm_inv*p)，进位按 h==0 修正。
 * 拆成 hi / lo 两步，是为了让 np11 和 np10 共用同一段算术。
 *
 * 两处高乘都必须是 _mm_mulhi_epu16：汇编写的是 pmulhuw（无符号高乘），
 * 用 _mm_mulhi_epi16 是有符号版本，0xffff 这样的输入会直接算错。 */
static inline __m128i mm_hi(__m128i a, __m128i b, __m128i mm_inv, __m128i one)
{
	__m128i hi = _mm_mulhi_epu16(a, b);
	__m128i h = _mm_mullo_epi16(_mm_mullo_epi16(a, b), mm_inv);
	__m128i carry = _mm_and_si128(_mm_cmpeq_epi16(h, zero128()), one);
	hi = _mm_sub_epi16(_mm_add_epi16(hi, one), carry);
	return hi;
}

static inline __m128i mm_finish(__m128i hi, __m128i h, __m128i p)
{
	return _mm_add_epi16(hi, _mm_mulhi_epu16(h, p));
}

/* --- 64 位（MMX）寄存器上的 punpckhwd ---------------------------------------
 *
 * MMX 寄存器是 64 位，punpckhwd 取的是每个操作数的高 32 位（字 2、3）；而
 * _mm_unpackhi_epi16 取的是 128 位值的高 8 个字。这里数据都在低 64 位，直接
 * 调 unpackhi 会拿到全 0，所以先把字 2、3 挪到低半边再 unpacklo。返回值的
 * 低 64 位和汇编一致，高 64 位是 0 —— 汇编那边压根不存在。
 */
static inline __m128i mmx_unpackhwd(__m128i x)
{
	return _mm_unpacklo_epi16(_mm_srli_si128(x, 4), _mm_srli_si128(x, 4));
}

/* --- asm_next_pol11_xmm(len) ------------------------------------------------
 *
 * 每轮 8 个素数：
 *   pi = fbs[16i..16i+7]，cc = fbs[16i+8..]
 *   p = np_px[16i..16i+7]，sqrt = np_px[16i+8..16i+15]
 *   h = pi*sqrt；蒙哥马利约减；cc1 = h mod p
 *   cc 奇数先加 p 再右移一位
 *   cc = disp + (p - cc)，mod p
 *   cc2 = p - cc1；cc1 = cc1 + cc mod p；cc2 = cc2 + cc mod p
 *   回写 (cc1, cc2)
 */
void asm_next_pol11_xmm(u32_t len)
{
	const __m128i one = _mm_set1_epi16(1);

	for (u32_t i = 0; i < len; i++) {
		const size_t k = (size_t)i;
		const __m128i p     = _mm_loadu_si128((const __m128i *)(mpqs_FB_np_px + 16 * k));
		const __m128i sq    = _mm_loadu_si128((const __m128i *)(mpqs_FB_np_px + 16 * k + 8));
		const __m128i inv   = _mm_loadu_si128((const __m128i *)(mpqs_FB_mm_inv + 8 * k));
		const __m128i disp  = _mm_loadu_si128((const __m128i *)(mpqs_FB_disp + 8 * k));
		const __m128i pi    = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k));
		__m128i cc = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k + 8));

		__m128i h  = _mm_mullo_epi16(_mm_mullo_epi16(pi, sq), inv);
		__m128i r  = mm_finish(mm_hi(pi, sq, inv, one), h, p);
		__m128i cc1 = mod_p(r, p, one);

		/* cc 奇数先加 p 再右移一位 */
		__m128i odd = _mm_and_si128(
			_mm_cmpeq_epi16(_mm_and_si128(cc, one), one), p);
		cc = _mm_srli_epi16(_mm_add_epi16(odd, cc), 1);

		/* cc = disp + (p - cc)，mod p */
		cc = mod_p(_mm_add_epi16(_mm_sub_epi16(p, cc), disp), p, one);

		__m128i cc2 = _mm_sub_epi16(p, cc1);
		cc1 = mod_p(_mm_add_epi16(cc1, cc), p, one);
		cc2 = mod_p(_mm_add_epi16(cc2, cc), p, one);

		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k),
		                 _mm_unpacklo_epi16(cc1, cc2));
		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k + 8),
		                 _mm_unpackhi_epi16(cc1, cc2));
	}
}

/* --- asm_next_pol10_xmm(len, invptr, ropptr, bimul) -------------------------
 *
 * 每轮 8 个素数：
 *   pi *= cc；蒙哥马利约减；直接回写（这一步不减 p）
 *   cc *= bimul；蒙哥马利约减；mod p 后写 ropptr
 *   fbs[2i+1] = (fbs[2i+1] + cc) mod p
 *
 * bimul 那个 16 位常量不是均匀的。汇编先把 0x0001000100010001 乘上 bimul，
 * 再 movq（注意是 64 位搬运，不是 movd）把乘积的低 64 位放进 xmm1 —— 这 64
 * 位里的四个 16 位字各不相同。接着 pslldq $8 把它们挪到高半边，再和 movq 出
 * 来的原向量相加，于是
 *     xmm1 = [ b0, b1, b2, b3, b0, b1, b2, b3 ]
 * 这里必须照抄。写成 _mm_set1_epi16((u16)bimul) 会把 8 个道全设成同一个值。
 */
void asm_next_pol10_xmm(u32_t len, u16 *invtabptr, u16 *ropptr, u32_t bimul)
{
	const __m128i one = _mm_set1_epi16(1);
	/* mulq %rcx 保留低 64 位，movq 取的就是这 64 位 */
	const unsigned long long r = 0x0001000100010001ULL * (unsigned long long)bimul;
	const __m128i bmul = _mm_setr_epi16((short)(u16)(r),
	                                    (short)(u16)(r >> 16),
	                                    (short)(u16)(r >> 32),
	                                    (short)(u16)(r >> 48),
	                                    (short)(u16)(r),
	                                    (short)(u16)(r >> 16),
	                                    (short)(u16)(r >> 32),
	                                    (short)(u16)(r >> 48));

	for (u32_t i = 0; i < len; i++) {
		const size_t k = (size_t)i;
		const __m128i p   = _mm_loadu_si128((const __m128i *)(mpqs_FB_np_px + 16 * k));
		const __m128i inv = _mm_loadu_si128((const __m128i *)(mpqs_FB_mm_inv + 8 * k));
		const __m128i cc0 = _mm_loadu_si128((const __m128i *)(invtabptr + 8 * k));
		const __m128i pi  = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k));

		__m128i h = _mm_mullo_epi16(_mm_mullo_epi16(pi, cc0), inv);
		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k),
		                 mm_finish(mm_hi(pi, cc0, inv, one), h, p));

		h = _mm_mullo_epi16(_mm_mullo_epi16(cc0, bmul), inv);
		__m128i cc = mod_p(mm_finish(mm_hi(cc0, bmul, inv, one), h, p), p, one);
		_mm_storeu_si128((__m128i *)(ropptr + 8 * k), cc);

		__m128i bbb = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k + 8));
		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k + 8),
		                 mod_p(_mm_add_epi16(bbb, cc), p, one));
	}
}

/* --- asm_next_pol3plus_xmm / asm_next_pol3minus_xmm ------------------------
 *
 * 每轮 8 个素数（16 个 fbs 项）。素数在 np_px 里两两复制（punpcklwd/hwd），
 * 所以每个模数用两次：前 8 个 fbs 用 np_px 的低 8 个字，后 8 个用高 8 个字。
 */
void asm_next_pol3plus_xmm(u32_t len, u16 *SI_add)
{
	const __m128i one = _mm_set1_epi16(1);

	for (u32_t i = 0; i < len; i++) {
		const size_t k = (size_t)i;
		const __m128i pp = _mm_loadu_si128((const __m128i *)(mpqs_FB_np_px + 16 * k));
		const __m128i aa = _mm_loadu_si128((const __m128i *)(SI_add + 8 * k));
		const __m128i plo = _mm_unpacklo_epi16(pp, pp);
		const __m128i phi = _mm_unpackhi_epi16(pp, pp);
		const __m128i alo = _mm_unpacklo_epi16(aa, aa);
		const __m128i ahi = _mm_unpackhi_epi16(aa, aa);

		__m128i slo = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k));
		__m128i shi = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k + 8));
		slo = mod_p(_mm_add_epi16(slo, alo), plo, one);
		shi = mod_p(_mm_add_epi16(shi, ahi), phi, one);
		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k), slo);
		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k + 8), shi);
	}
}

void asm_next_pol3minus_xmm(u32_t len, u16 *SI_add)
{
	const __m128i one = _mm_set1_epi16(1);

	for (u32_t i = 0; i < len; i++) {
		const size_t k = (size_t)i;
		const __m128i pp = _mm_loadu_si128((const __m128i *)(mpqs_FB_np_px + 16 * k));
		const __m128i aa = _mm_loadu_si128((const __m128i *)(SI_add + 8 * k));
		const __m128i plo = _mm_unpacklo_epi16(pp, pp);
		const __m128i phi = _mm_unpackhi_epi16(pp, pp);
		const __m128i alo = _mm_sub_epi16(_mm_unpacklo_epi16(aa, aa), plo);
		const __m128i ahi = _mm_sub_epi16(_mm_unpackhi_epi16(aa, aa), phi);

		__m128i slo = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k));
		__m128i shi = _mm_loadu_si128((const __m128i *)(mpqs_FB_start + 16 * k + 8));
		slo = mod_p(_mm_sub_epi16(slo, alo), plo, one);
		shi = mod_p(_mm_sub_epi16(shi, ahi), phi, one);
		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k), slo);
		_mm_storeu_si128((__m128i *)(mpqs_FB_start + 16 * k + 8), shi);
	}
}

/* --- asm_next_pol3plus / asm_next_pol3minus（MMX 版）------------------------
 *
 * 数据都在低 64 位：每轮 4 个素数（np_p 读 4 个、步长 8 个），SI_add 读 4 个、
 * 步长 4 个，mpqs_FB_start 读 4+4 个、步长 8 个。
 *
 * 注意本构建定义了 HAVE_XMM_MUL，mpqs.cpp 只填 mpqs_FB_np_px，mpqs_FB_np_p
 * 从未被初始化（它在 #else 分支里）。这两个 MMX 入口是给不带 XMM 的构建用的，
 * 这里照样逐位翻译，差分测试会自己给它填数据。
 */
void asm_next_pol3plus(u32_t len, u16 *SI_add)
{
	const __m128i one = _mm_set1_epi16(1);

	for (u32_t i = 0; i < len; i++) {
		const size_t k = (size_t)i;
		const __m128i pp = _mm_loadl_epi64((const __m128i *)(mpqs_FB_np_p + 8 * k));
		const __m128i aa = _mm_loadl_epi64((const __m128i *)(SI_add + 4 * k));
		const __m128i plo = _mm_unpacklo_epi16(pp, pp);
		const __m128i phi = mmx_unpackhwd(pp);
		const __m128i alo = _mm_unpacklo_epi16(aa, aa);
		const __m128i ahi = mmx_unpackhwd(aa);

		__m128i slo = _mm_loadl_epi64((const __m128i *)(mpqs_FB_start + 8 * k));
		__m128i shi = _mm_loadl_epi64((const __m128i *)(mpqs_FB_start + 8 * k + 4));
		slo = mod_p(_mm_add_epi16(slo, alo), plo, one);
		shi = mod_p(_mm_add_epi16(shi, ahi), phi, one);
		_mm_storel_epi64((__m128i *)(mpqs_FB_start + 8 * k), slo);
		_mm_storel_epi64((__m128i *)(mpqs_FB_start + 8 * k + 4), shi);
	}
}

void asm_next_pol3minus(u32_t len, u16 *SI_add)
{
	const __m128i one = _mm_set1_epi16(1);

	for (u32_t i = 0; i < len; i++) {
		const size_t k = (size_t)i;
		const __m128i pp = _mm_loadl_epi64((const __m128i *)(mpqs_FB_np_p + 8 * k));
		const __m128i aa = _mm_loadl_epi64((const __m128i *)(SI_add + 4 * k));
		const __m128i plo = _mm_unpacklo_epi16(pp, pp);
		const __m128i phi = mmx_unpackhwd(pp);
		const __m128i alo = _mm_sub_epi16(_mm_unpacklo_epi16(aa, aa), plo);
		const __m128i ahi = _mm_sub_epi16(mmx_unpackhwd(aa), phi);

		__m128i slo = _mm_loadl_epi64((const __m128i *)(mpqs_FB_start + 8 * k));
		__m128i shi = _mm_loadl_epi64((const __m128i *)(mpqs_FB_start + 8 * k + 4));
		slo = mod_p(_mm_sub_epi16(slo, alo), plo, one);
		shi = mod_p(_mm_sub_epi16(shi, ahi), phi, one);
		_mm_storel_epi64((__m128i *)(mpqs_FB_start + 8 * k), slo);
		_mm_storel_epi64((__m128i *)(mpqs_FB_start + 8 * k + 4), shi);
	}
}