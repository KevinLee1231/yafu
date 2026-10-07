/* mpqs_gauss —— 由 mpqs_gauss.asm 翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 一个入口 asm_gauss(void)，GF(2) 上的增量高斯消元。全部状态在 mpqs_gauss_*
 * 全局里，函数本身没有返回值（反汇编末尾 `movl tmp_j,%eax` 留下的值没人用）。
 *
 * 每轮做三件事：
 *   1) 找一行 j，其第 k32 个 u32 里的第 (k&31) 位为 1；
 *   2) 把它换到 mpqs_gauss_j 那一行，记 d[k]=j、c[j]=k；
 *   3) 把 mpqs_gauss_col[] 当列号表，对表里的列做 row[col] ^= row[j][0..k32]。
 *
 * ------------------------------------------------------------------
 * 照抄、不能「顺手改好」的地方。
 *
 * 1. k 是**先减再判**：`movl mpqs_gauss_k(%rip),%eax ; decl %eax ;
 *    movl %eax,mpqs_gauss_k(%rip) ; jl end`。k 原本为 0 时减成 0xffffffff，
 *    `jl`（看符号位）跳走，退出时 mpqs_gauss_k 停在 0xffffffff。调用点
 *    mpqs.cpp:2378 靠 `if (!(mpqs_gauss_k+1))` 认这个。照抄这个顺序。
 *
 * 2. 找 j 的循环**不检查 mpqs_gauss_c[j]**。C 参考实现 mpqs.cpp:2395 写的是
 *    `while ((j<m) && (c[j]>=0 || ((row[j][k32]&mask)==0))) j++;`，汇编这边
 *    只有矩阵位这一条判据。之所以等价，靠的是不变量：c[j]>=0 的行恰好是
 *    [0, mpqs_gauss_j)，而搜索从 mpqs_gauss_j 起步（见 3）。
 *
 * 3. `movl mpqs_gauss_j(%rip),%ecx ; movq (%rsi,%rcx,8),%rsi ; decl %ecx`：
 *    先用 mpqs_gauss_j 取 row[mpqs_gauss_j]，再把计数器减一，loopj 的
 *    `incl %ecx` 又加回来 —— 所以第一次检查的行号还是 mpqs_gauss_j。
 *    另一头 `incl mpqs_gauss_j(%rip)` 只加一，不是 C 参考里那句
 *    `while (c[mpqs_gauss_j]>=0) mpqs_gauss_j++;`。照抄「只加一」。
 *
 * 4. 交换两行时 `movq (%rax,%rdx,4),%mm0` 的 rdx 是 **u32 下标**，每轮 +2，
 *    即按 8 字节（两个 u32）一格换，条件 `cmpl n32,%edx ; jb`（无符号）。
 *    n32 是偶数（mpqs.cpp:2244 `if (mpqs_gauss_n32&1) mpqs_gauss_n32++;`），
 *    正好覆盖整行。照抄这个 64 位粒度。
 *
 * 5. 记 d[k]=j、c[j]=k 用的是 **movw**，只写 16 位。j、k <= 512，写得下。
 *
 * 6. 收集要消元的列号进 mpqs_gauss_col[]。三个循环体一模一样：
 *      movl (%rsi),%r9d ; andl %r8d,%r9d ; movq $0,%r9
 *      movw %cx,(%rdi) ; cmovnzq %rdx,%r9 ; inc ; addq %r9,%rdi
 *    即「行号=列号写进 col[rdi]；该行第 k32 个字命中掩码就 rdi += 2」。
 *    三处细节：
 *      a) 掩码判定用的是 `andl` 留下的 ZF（中间的 `movq $0,%r9` 不改标志位），
 *         所以判的是 (row[t][k32] & mask) != 0。
 *      b) 每轮的 `leaq (%rsi,%rax,4),%rsi`（rax = n32）都排在判界**之前**，
 *         所以每轮都推进；searchloopAend 里还有一次推进。所以 searchloopA
 *         跑完之后 rsi 已经指到第 tmp_j+1 行，searchloopB 正好从 t = j+1
 *         开始，没有 off-by-one。
 *      c) `test %ecx,%ecx ; jz searchloopAend` 在 j == 0 时整段跳过 loopj，
 *         但 searchloopAend 那条推进照样执行 —— 少跑的是零轮，指针推进不变。
 *    写完的 col[] 里一个列号都没有（rdi == &col[0]）时，汇编直接回 whileloop，
 *    本轮不做消元。
 *
 * 7. 消元分四条路径，**异或的字数和起点各不相同**：
 *      k32 < 2  entry1  一次 movq  = 2 个 u32，起点 row[j][0]
 *      k32 < 4  entry2  两次 movq  = 4 个 u32，起点 row[j][0]
 *      k32 < 6  entry3  三次 movq  = 6 个 u32，起点 row[j][0]
 *      k32 >= 6 outerloop0  标量，l = 0..k32（含），起点 row[j][0]
 *    全部从字 0 开始（rsi 在 searchloopBend 之后被重新加载成 row[tmp_j]，
 *    没有加 k32 偏移）。所以 k32>=1 时和 C 参考的 `for (l=0;l<=k32;l++)`
 *    一致；**k32 == 0 时 entry1 会多异或一个字（word 1）**，那是汇编的实际
 *    行为，照抄 —— 改成「按 k32+1 个字异或」在 k32>=2 时反而会和汇编不一致。
 *
 * 8. `emms` 只清 MMX/x87 状态，C++ 用 SSE2 没有对应物，不搬。
 *
 * 9. entry1/2/3 的循环体里 `movq (%rsi,%r9,8),%rax` 在最后一次也会执行，
 *    读的是 row[col[n]]（n = 列号个数）。纯读、无副作用，C++ 里挪到轮首
 *    没有可观察差别；但那一读会越过 mpqs_gauss_col[] 末尾一格，所以契约里
 *    列号个数 <= MPQS_GAUSS_MAX-1。
 */

#include <stdint.h>

#include <emmintrin.h>

#include "siever-config.h"
#include "mpqs-config.h"     /* asm_gauss 的声明在这个头的 块里 */

#include "lasieve_ns.h"

namespace lasieve_ns {
typedef unsigned char u8;
typedef u16_t u16;
typedef u32_t u32;
typedef u64_t u64;

/* mpqs.cpp 里定义的那几个符号。文件作用域的变量在 C++ 里不做名字改编，
 * 子 Makefile 按 I 值用 -D 统一改名，定义和引用一起变，对得上。 */
extern u32_t **mpqs_gauss_row;
extern u32_t *mpqs_gauss_mat;
extern i16_t mpqs_gauss_c[];      /* MPQS_GAUSS_MAX = 512 */
extern i16_t mpqs_gauss_d[];
extern i16_t mpqs_gauss_col[];
extern u32_t mpqs_gauss_m, mpqs_gauss_n, mpqs_gauss_n32;
extern u32_t mpqs_gauss_k, mpqs_gauss_j;

/* 64 位一格地交换两行里第 l、l+1 两个 u32（movq 的粒度是 8 字节，不要求
 * 对齐，所以用 loadl/storel_epi64 而不是 load/store_si128）。 */
static void swap64(u32_t *a, u32_t *b, u32_t l)
{
	__m128i ta = _mm_loadl_epi64((const __m128i *)(a + l));
	__m128i tb = _mm_loadl_epi64((const __m128i *)(b + l));

	_mm_storel_epi64((__m128i *)(b + l), ta);
	_mm_storel_epi64((__m128i *)(a + l), tb);
}

/* entry1/2/3：words 个 64 位字（= 2*words 个 u32），全部从 rowj 的字 0 开始。
 * 见头注 7。 */
static void xor_words(u32_t *target, const u32_t *rowj, unsigned words)
{
	unsigned w;

	for (w = 0; w < words; w++) {
		const __m128i v = _mm_loadl_epi64((const __m128i *)(rowj + 2 * w));
		__m128i t = _mm_loadl_epi64((const __m128i *)(target + 2 * w));

		t = _mm_xor_si128(t, v);
		_mm_storel_epi64((__m128i *)(target + 2 * w), t);
	}
}

void asm_gauss(void)
{
	u32_t tmp_j = 0;      /* %r10d；只在轮内被赋过值之后才被读 */

	for (;;) {
		const u32_t k = mpqs_gauss_k - 1;
		u32_t mask;
		u32_t k32;
		u32_t j;
		u32_t t;
		u32_t *rsi;
		i16_t *col;
		u32_t ncol;
		u32_t i;

		mpqs_gauss_k = k;
		if ((int32_t)k < 0)                    /* jl end */
			break;

		mask = 1u << (k & 31);                /* shll %cl,%r8d */
		k32 = k >> 5;                         /* shrl $5,%ecx ; mov k32 */

		/* ---- 找 j（头注 2/3）----
		 * 起点的指针算式照抄：
		 *     movq (%rsi,%rcx,8),%rsi   rsi = row[j]
		 *     movl %eax,%eax           rax = k32（零扩展）
		 *     subq %rdx,%rax           rax = k32 - n32（64 位）
		 *     decl %ecx                ecx = j-1
		 *     leaq (%rsi,%rax,4),%rsi  rsi = row[j] + 4*(k32-n32) = row[j-1][k32]
		 * 然后 loopj 是「先 incl %ecx 再判界、再推指针」，所以第一轮
		 * 检查的**行号和指针是配对的**：ecx = j、rsi = row[j][k32]。
		 * 换句话说那条 decl 就是为了让第一轮从 mpqs_gauss_j 起步 ——
		 * 直接从 mpqs_gauss_j+1 起步会整体错一行。
		 * t 用 u32：j == 0 时 decl 之后是 0xffffffff，incl 回到 0，
		 * 靠 32 位回绕正好对上，不能用有符号或提前判零。 */
		{
			u32_t tj = mpqs_gauss_j - 1;    /* decl %ecx */
			u32_t *q =
				(u32_t *)((u8 *)mpqs_gauss_row[mpqs_gauss_j]
					  + 4 * ((long)k32 - (long)mpqs_gauss_n32));

			for (;;) {
				tj++;                          /* incl %ecx */
				if (tj >= mpqs_gauss_m)         /* jnc end_of_loop */
					break;
				q += mpqs_gauss_n32;           /* leaq (%rsi,%rax,4),%rsi */
				if (q[0] & mask) {             /* andl %r8d,%edx */
					j = tj;
					break;
				}
			}

			if (tj >= mpqs_gauss_m) {
				/* end_of_loop：movw $-1 ; mov %r9w,(%rdi,%rax,2)
				 * 然后 movl %ecx,tmp_j 并跳到 end。这里 ecx == m。 */
				mpqs_gauss_d[k] = -1;
				tmp_j = tj;
				break;
			}

			/* ---- 交换行 j 和 mpqs_gauss_j（头注 4）---- */
			if (j != mpqs_gauss_j) {          /* jz noxch_j */
				u32_t *a = mpqs_gauss_row[j];
				u32_t *b = mpqs_gauss_row[mpqs_gauss_j];
				u32_t l;

				for (l = 0; l < mpqs_gauss_n32; l += 2)
					swap64(a, b, l);
				j = mpqs_gauss_j;          /* xch_loop_end: movl j,%ecx */
			}
		}

		/* ---- 头注 5/3：记 d[k]=j、c[j]=k，然后 j 只加一 ---- */
		mpqs_gauss_d[k] = (i16_t)(u16)j;
		mpqs_gauss_c[j] = (i16_t)(u16)k;
		mpqs_gauss_j++;
		tmp_j = j;

		/* ---- 头注 6：收集要消元的列号 ---- */
		col = mpqs_gauss_col;
		rsi = mpqs_gauss_mat + k32;           /* leaq (%rsi,%rdx,4),%rsi */

		if (j != 0) {                         /* test %ecx,%ecx ; jz Aend */
			t = 0;                       /* xorl %ecx,%ecx */
			do {
				col[0] = (i16_t)(u16)t;  /* movw %cx,(%rdi) */
				if (rsi[0] & mask)     /* cmovnzq %rdx,%r9 */
					col++;
				t++;
				rsi += mpqs_gauss_n32;
			} while (t < tmp_j);           /* cmpl tmp_j,%ecx ; jb */
		}

		/* searchloopAend：incl %ecx，再推一次 rsi —— 无论上面跑没跑过 */
		t = j + 1;
		rsi += mpqs_gauss_n32;

		if (t < mpqs_gauss_m) {               /* cmp m,%ecx ; jae Bend */
			do {
				col[0] = (i16_t)(u16)t;
				if (rsi[0] & mask)
					col++;
				t++;
				rsi += mpqs_gauss_n32;
			} while (t < mpqs_gauss_m);
		}

		/* 一个列号都没有 -> 本轮不做消元，直接回 whileloop */
		if (col == mpqs_gauss_col)            /* cmpq %r8,%rdi ; jz */
			continue;

		ncol = (u32_t)(col - mpqs_gauss_col);

		/* ---- 头注 7：消元 ----
		 * 列号是 movzwq 读出来的（零扩展 16 位）。 */
		if (k32 < 2) {
			for (i = 0; i < ncol; i++)
				xor_words(mpqs_gauss_row[(u32)(u16)mpqs_gauss_col[i]],
					  mpqs_gauss_row[tmp_j], 1);
		} else if (k32 < 4) {
			for (i = 0; i < ncol; i++)
				xor_words(mpqs_gauss_row[(u32)(u16)mpqs_gauss_col[i]],
					  mpqs_gauss_row[tmp_j], 2);
		} else if (k32 < 6) {
			for (i = 0; i < ncol; i++)
				xor_words(mpqs_gauss_row[(u32)(u16)mpqs_gauss_col[i]],
					  mpqs_gauss_row[tmp_j], 3);
		} else {
			/* outerloop0：标量，l = 0..k32（含） */
			const u32_t *rowj = mpqs_gauss_row[tmp_j];

			for (i = 0; i < ncol; i++) {
				u32_t *tgt =
					mpqs_gauss_row[(u32)(u16)mpqs_gauss_col[i]];
				u32_t l;

				for (l = 0; l <= k32; l++)      /* cmpl %ecx,k32 */
					tgt[l] ^= rowj[l];
			}
		}
	}
}
}  /* namespace lasieve_ns */
