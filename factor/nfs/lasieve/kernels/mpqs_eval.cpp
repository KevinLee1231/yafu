/* (Copyright (C) 2002,2004 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 四个入口做同一件事：按 32 字节一块扫筛数组，把高位为 1 的字节按位置从小到
 * 大收集进 buffer（每条 16 位），最多收 nmax 条，返回条数。带 0 的变体额外把
 * 扫过的区间清零。
 *
 * 三处不能「顺手改对」的语义，都照抄：
 *
 * 1. buffer 里存的不是块内偏移。汇编每轮把 sv 减去入参 sv_arg 变成「相对
 *    sievebegin 的字节偏移」，命中循环里 sv 加上累加器 r11 存进 buffer，再把
 *    r11 减回去。所以存的是 block_offset + Σ(b_j + 1)：一个从 sievebegin 起算
 *    的累加位移。下游解码依赖这个编码。
 *
 * 2. 清零时机。MMX 版是「先处理、后清零」：清零动作挂在 leaq 32(sv),sv 之后
 *    的 -32(sv) 上，即下一轮循环的头部。SSE 版是取完就立刻清（值已在寄存器
 *    里），对外表现一致。这里统一写成处理完再清。
 *
 * 3. 判满的条件。汇编比的是 buffer 与 buffer+nmax*2：存完一条后 buffer
 *    前进 2，jbe 判的是 buffer_ub <= buffer，也就是「已存条数 >= nmax」才算
 *    满。注意不是「> nmax」，所以 nmax==0 时存第一条就停。
 *
 * 另外一处是搬过来之后才暴露的坑，见下面 u64 m 那行的注释。
 */

#include <stdint.h>
#include <string.h>

#include <immintrin.h>

#include "siever-config.h"
#include "mpqs-config.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef uint32_t u32;
typedef uint64_t u64;

/* 注意：形参是 unsigned *，但汇编里 8(sv)/16(sv)/24(sv) 和 leaq 32(sv),sv
 * 全是字节偏移。所以内部一律用 unsigned char * 走字节 —— 在 unsigned * 上写
 * sv + 8 会前进 8 个元素 = 32 字节，sv += 32 更是 128 字节。
 *
 * movemask8 对应 MMX 版：4 次 8 字节 pmovmskb，再移位 OR。不能把 4 个加载
 * OR 进一个 128 位寄存器后只取一次 movemask —— _mm_loadl_epi64 会清高 64 位，
 * OR 完位位置全错。
 * movemask16 对应 SSE 版：汇编用 movaps，要求 16 字节对齐，所以是对齐加载。 */
static inline u32 movemask8(const unsigned char *sv)
{
	__m128i a = _mm_loadl_epi64((const __m128i *)(sv));
	__m128i b = _mm_loadl_epi64((const __m128i *)(sv + 8));
	__m128i c = _mm_loadl_epi64((const __m128i *)(sv + 16));
	__m128i d = _mm_loadl_epi64((const __m128i *)(sv + 24));
	u32 m0 = (u32)_mm_movemask_epi8(a);
	u32 m1 = (u32)_mm_movemask_epi8(b);
	u32 m2 = (u32)_mm_movemask_epi8(c);
	u32 m3 = (u32)_mm_movemask_epi8(d);
	return m0 | (m1 << 8) | (m2 << 16) | (m3 << 24);
}

/* movemask16 对应 SSE 版：汇编是 movaps 两次 + 两次 pmovmskb + 移位拼接，
 * 所以要 16 字节对齐，用对齐加载。
 * 不能写成 movemask(_mm_or_si128(a,b))：那样 bit i 变成「第 i 字节和第 i+16 字节
 * 的高位或」，高位半边被折到低位半边上，位位置全错。 */
static inline u32 movemask16(const unsigned char *sv)
{
	__m128i a = _mm_load_si128((const __m128i *)(sv));
	__m128i b = _mm_load_si128((const __m128i *)(sv + 16));
	return (u32)_mm_movemask_epi8(a) | ((u32)_mm_movemask_epi8(b) << 16);
}

#define DEF_EVAL(NAME, MASKFN, CLEAR)                                       \
size_t NAME(unsigned *sievebegin, unsigned *sieveend, u16_t *buffer,         \
            unsigned nmax)                                                  \
{                                                                            \
	unsigned char *svbase = (unsigned char *)sievebegin;                     \
	unsigned char *sv = svbase;                                             \
	unsigned char *svend = (unsigned char *)sieveend;                       \
	u16_t *buf = buffer;                                                   \
	u32 acc = 0;                      /* 汇编：r11，从 0 起 */             \
	unsigned n = 0;                                                       \
	int full = 0;                                                          \
                                                                             \
	while (sv < svend) {                                                   \
		/* 掩码必须是 64 位。汇编的掩码寄存器是 r10，shrq %cl,%r10 是 64 位  \
		 * 右移，计数屏蔽成 6 位，所以 bsfq 拿到 31 时移的是真的 32 位，掩码  \
		 * 随之归零。写成 u32 之后右移 32 位是未定义行为，而 32 位 shr/shrx  \
		 * 只取计数的低 5 位，32 & 31 == 0 —— 移了个寂寞，同一位被反复当成    \
		 * 新命中，一路写到 nmax 才停，返回值直接等于 nmax。 */             \
		u64 m = MASKFN(sv);                                               \
		if (m) {                                                          \
			u32 base = (u32)(sv - svbase);   /* 汇编：subq sv_arg,sv */    \
			acc = 0;                                               \
			do {                                                      \
				u32 b = (u32)__builtin_ctzll(m);                   \
				acc += b;              /* addq %rcx,%r11 */        \
				m >>= (b + 1);         /* shrq %cl,%r10 */         \
				/* 汇编 addq %r11,sv / movw svw,(buffer) / subq %r11,sv \
				 * 存的是 base + acc，不是 acc */                   \
				buf[n] = (u16_t)(base + acc);                        \
				n++;                                                \
				acc++;                  /* incq %r11 */            \
				/* 汇编 cmpq buffer,buffer_ub; jbe 判的是  \
				 * buffer_ub <= buffer，即存完 n 条后 n >= nmax */\
				if (n >= nmax) {                                    \
					full = 1;                                    \
					break;                                       \
				}                                               \
			} while (m);                                             \
		}                                                                   \
		if (full)                                                        \
			break;                                                       \
		if (CLEAR)                                                      \
			memset(sv, 0, 32);   /* 汇编：下一轮循环头里清上一块 */    \
		sv += 32;                                                        \
	}                                                                       \
	/* 提前填满时，汇编从当前块一次清到末尾。 */                           \
	if (CLEAR && full) {                                                   \
		while (sv < svend) {                                             \
			memset(sv, 0, 32);                                         \
			sv += 32;                                                   \
		}                                                               \
	}                                                                       \
	return (size_t)n;                                                      \
}

DEF_EVAL(asm_evaluate, movemask8, 0)
DEF_EVAL(asm_evaluate0, movemask8, 1)
DEF_EVAL(asm_evaluate_xmm, movemask16, 0)
DEF_EVAL(asm_evaluate0_xmm, movemask16, 1)
}  /* namespace lasieve_ns */
