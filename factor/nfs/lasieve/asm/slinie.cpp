/* slinie —— 由 factor/nfs/lasieve/asm/ 下的四个 m4 模板翻译而来
 * (Copyright (C) 2002 Jens Franke, T.Kleinjung，gnfs4linux，GPL)。
 *
 * 「排素数」内核：给定一段因子基记录和筛数组，把这一段里每个素数 p 的
 * 筛法权值 log(p) 累加到筛数组上它命中的位置上。同一段内核有四个变体，
 * 按素数区间分：命中的点越少，一次排的点数越少，内层循环越短。
 *
 *   slinieG.asm  → slinie    通用版：每段一个 do-while，一轮 4 个点
 *   slinie3A.asm → slinie3   每段固定 3 个点（最后一个带条件）
 *   slinie2A.asm → slinie2   每段固定 2 个点（最后一个带条件）
 *   slinie1A.asm → slinie1   每段固定 1 个点（第二个带条件）
 *
 * 四个入口一共，一个文件。
 *
 * ===================== 生产路径 =====================
 *
 * gnfs-lasieve4e.cpp 里四处调用点分别是
 *   3638: slinie (...)   被 #ifdef ASM_LINESIEVER   包住
 *   3679: slinie3(...)   被 #ifdef ASM_LINESIEVER3  包住
 *   3716: slinie2(...)   被 #ifdef ASM_LINESIEVER2  包住
 *   3752: slinie1(...)   被 #if defined(ASM_LINESIEVER1) &&
 *                          !defined(AVX512_SIEVE1) &&
 *                          !defined(CONTIGUOUS_SMALLSIEVE) 包住
 * 这四个宏都由 asm/include/siever-config.h 无条件 #define，而
 * AVX512_SIEVE1 / CONTIGUOUS_SMALLSIEVE 在整个仓库里**一次都没有被定义**
 * （grep 全仓无命中）。所以 Linux/gcc 下走的一定是汇编本，gnfs-lasieve4e.cpp
 * 里那段 `#else` 的手写 C 是从不编译的兜底路径。
 *
 * 那个 `#else` C 版（也就是任务里说的「C 参考实现」）**不在
 * asm/slinie.c 里 —— 仓库里从来没有过这个文件**（git log 全分支无记录）。
 * 它是 CTANGLE 的产物，直接内联在 gnfs-lasieve4e.cpp 的调用点里。
 * 四个 C 版入口和四个汇编入口同名同签名，所以不需要额外留一份符号。
 *
 * 那份 C 版和汇编的关系（拿两边各自链接汇编单独跑同一批输入实测过，
 * 见 Temp 里的 cfall_slinie.c）：**在各自真实收到的素数范围内，两者
 * 逐字节一致**（四个变体都是 0 处不同）。唯一一处分歧是 slinieG 收到
 * p > n_i/3 的素数时：汇编的内层是 do-while，无论如何都先做一轮 4 次
 * addb，于是会在筛选带**末尾之外**多写几个点；而 C 版那条是前置判断的
 * for 循环，yy_ub = y + n_i - 3*p 已经为负，一轮都不做。slinieG 在生产上
 * 只收小素数（大的走 slinie3/2/1），所以这个分歧碰不到。
 *
 * ===================== 参数 =====================
 *
 * 声明（asm/include/siever-config.h:84-90）：
 *     u32_t *slinie (u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval);
 *     u32_t *slinie1(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval);
 *     u32_t *slinie2(...);  u32_t *slinie3(...);
 *
 * 三个参数**都不是长度**，头两个也不是记录个数：
 *
 *   aux_ptr      因子基记录数组的当前记录（每条 8 字节 = 4 个 u16）。
 *                记录布局：x[0]=prime x[1]=proot x[2]=log x[3]=root，
 *                对应汇编的 (%rdi) / 2(%rdi) / 4(%rdi) / 6(%rdi)。
 *                x[1] 在 gnfs-lasieve4e.cpp:2938-2945 填的是 modmul32 的结果，
 *                所以 proot < prime；x[3] 在 3162/3239 填的是 `(...) % p`，
 *                所以 root < prime。这两条是输入约束，见文末契约。
 *   aux_ptr_ub   **下一段记录的起点**（不是长度、不是个数）。
 *                入口 `subq $8,%rsi` 减 8 字节 = 4 个 u16 元素 = 1 条记录；
 *                回边 `leaq 8(%rdi),%rdi` 加 1 条记录。写成
 *                `for (i = 0; i < n; i++)` 是全错的。
 *   sieve_interval  **筛数组的首地址**（不是长度）。
 *                汇编 `movq %rdx,%rax` 直接取它的**数值**当基址用，
 *                之后 `leaq (%rax,%rcx),%rcx` 把基址和偏移相加。
 *                声明成 `unsigned char*` 而不是 size_t 就是这个意思。
 *
 * ===================== 必须照抄的语义 =====================
 *
 * 0) 记录循环的两处比较。AT&T 里 `cmpq %rdi,%rsi` 算的是 **rsi - rdi**，
 *    所以 `jbe`（CF|ZF）判的是 **rsi <= rdi**，即 aux_ptr_ub <= aux_ptr。
 *    入口那条 `jbe slinie*_ende` 和回边那条 `ja slinie*_fbi_loop` 都是
 *    「aux_ptr_ub > aux_ptr 才继续」。写成 `aux_ptr <= aux_ptr_ub` 就整个反了，
 *    入口直接早退、一个素数都不处理（筛数组只差一点点，肉眼很难看出来，
 *    但返回值会整整差 j_per_strip*n_i = L1_SIZE）。下面代码里统一写成
 *    `ub_ptr <= x`，两处都别改。
 *
 * 0b) 回边那条 cmpq 读的是 `leaq 8(aux_ptr),aux_ptr` **之前**的 aux_ptr。
 *     那条 leaq 夹在 cmpq 和 ja 中间纯粹是给流水线挪位置，它不改标志位，
 *     所以分支条件用的是**自增前**的 x。必须先比后加。实测处理条数：
 *
 *         nwords = (aux_ptr_ub - aux_ptr) 以 u16 元素计
 *         条数 = (nwords + 3) / 4          （nwords >= 1；nwords <= 0 时 0 条）
 *
 *     即 nwords=1..4 → 1 条，5..8 → 2 条，9..12 → 3 条，60 → 15 条。
 *     「先 x += 4 再比」会正好少处理最后一条（nwords=8 时只做 1 条），
 *     差的是最后一条记录的全部落点，肉眼几乎看不出来，但差分测试一测就中。
 *     真实调用点传的 nwords 是 4 的倍数，所以真实路径上两种写法结果一样 ——
 *     这也正是它容易被改坏而不被发现的原因。
 *
 * 0c) `movq %rdx,%rax`（sieve_ptr_ub = 筛数组基址）在 fbi 循环**里面**，
 *     每个素数都重置一次。C++ 里若把 `ub = base` 写在记录循环外面，
 *     ub 会跨记录累加，第 k 条记录的落点整体偏移 k*j_per_strip*n_i。
 *
 * 1) leaq 的第三个操作数是**变址倍数(scale)，不是偏移**。这一份文件里
 *    两处都极容易看错：
 *
 *      leaq (sieve_ptr,prime,2),sieve_ptr   →  sp += 2*prime，**不是** sp += prime+2
 *      leaq (sieve_ptr_ub,prime,4),sieve_ptr_ub → ub += 4*prime，**不是** ub += prime+4
 *
 *    看成「+2 字节 / +4 字节」的话，slinieG 的内层步长会从 4*prime 变成
 *    2*prime+4，而每段 sieve_ptr_ub 的净增量会从 n_i 变成 n_i-3*prime，
 *    于是 4 段的边界全都错位。判据是每段末尾 sieve_ptr_ub 必须正好 +n_i：
 *      slinieG   n_i - 5*prime + 4*prime + prime = n_i   ✓
 *      slinie1/2/3  n_i - prime + prime = n_i            ✓
 *    这一点和 gnfs-lasieve4e.cpp 里 `#else` 的 C 版对得上：那边每个 n_i
 *    块是 y = sieve_interval + j*n_i，段内上界是 y+n_i 或 y+n_i-3*p。
 *
 * 2) cmovncq 判的是 `addq %r12,%r8`（root += proot）那条 add 的进位。
 *    proot = proot_src - prime 是在 64 位里算的（proot_src < prime 时 proot
 *    是个接近 2^64 的巨大正数），cmovncq 判的是那条 add 的**进位 CF**，
 *    而 CF 的含义是**无符号溢出**、不是「结果是负数」：
 *        CF = 1  <=>  root + proot >= 2^64  <=>  root + proot_src >= prime
 *    所以
 *        root' = (root + proot_src >= prime) ? root + proot_src - prime
 *                                             : root + proot_src
 *    和 gnfs-lasieve4e.cpp 里 C 版的 `r = r + pr; if (r >= p) r -= p;` 一字不差。
 *    写成「和是不是负数」就正好反过来，root 一路变成 2^64 附近的巨值，
 *    sieve_ptr 跟着飞出数组。这里直接按硬件的 CF 写（无符号加法溢出即 t < root），
 *    不依赖任何范围假设；-Ofast 下也安全，因为全程没有有符号溢出。
 *
 * 3) cmpq 和 jbe 之间夹着一条 leaq，而 leaq 不改标志位，所以分支用的是
 *    **加 prime 之前**的 sieve_ptr_ub。slinie1/2/3A 的段尾都是这个形状：
 *
 *      cmpq %rcx,%rax          ; 拿的是下面那条 leaq 之前的 ub
 *      movb %r11b,(%rcx)       ; 存内存，同样不改标志位
 *      leaq (%rax,%r9),%rax    ; ub += prime
 *      jbe  next_j             ; 判的仍是上面那条 cmp
 *
 *    写成「先加 prime 再比」就在 ub 落在 [ub_before, ub_before+prime) 的
 *    那一段上差整整一个 prime 的宽度，条件命中与否整个反过来。
 *    条件本身是 `ub > sp`（jbe 跳过），换成 ub 加完 prime 之后再比就是错的。
 *
 * 4) slinieG 的内层是 **do-while**：`ja slinie_loop<i>` 排在两条
 *    `leaq (sieve_ptr,prime,2)` 之后，所以哪怕 sieve_ptr 一上来就 >= ub，
 *    那一轮 4 次 addb 也照做。写成 `while (sp < ub)` 会漏掉头一轮。
 *    而段尾那两级补齐（`ub += 4*prime` 之后那一次、`ub += prime` 之后那一次）
 *    各自用的是**已经加过前面那些量**的 ub，条件逐级放宽。
 *
 * 5) j_per_strip == 1（I=16）时 m4 走 slinie1/2/3A 里的 `divert` 分支：
 *    第一份函数体（forloop 版）被 divert(-1) 丢进 /dev/null，只有文件末尾
 *    那份展开的单段版本被输出（此时 j_per_strip_minus1 虽然被定义成 1，
 *    但那份代码根本不输出）。所以 I=16 时只有 1 段。
 *    这里用一个 `for (j = 0; j < LS_J_PER_STRIP; ++j)` 循环对全部 I 都成立：
 *    末尾那份展开段和 forloop 副本的**指令序列完全一样**，只有标签名不同
 *    （next_fbi vs next_j<i>），而两个标签都是紧跟在跳转之后的向前跳转，
 *    等价于一条 `if`。
 *
 * 6) 四个函数都**不写回因子基数组**：slinieG/1/3A 里的
 *    `# movq root,root_src` 是注释掉的，slinie2A 连注释都没有。
 *    对应 C 版里那句同样被 `#if 0` 关掉的 `x[3] = r;`。
 *    所以 aux 数组是纯只读的，root 每个素数都从头从 x[3] 重新算。
 *
 * 7) sieve_log 是 `movb 4(%rdi),%r10b` 零扩展出来的**完整 8 位**值
 *    （0..255），不是 log(p) 那种小值；addb 是模 256 回绕。
 *
 * 8) 返回值：声明是 u32_t*，但汇编 ret 时 %rax 里是循环最后的
 *    sieve_ptr_ub（早退路径上则是调用方留在 %rax 里的随机值，ABI 没有任何
 *    约定）。四个调用点都把它当语句用、直接丢弃。
 *    这里照抄成返回最后的 sieve_ptr_ub，好让差分测试顺带把整条 ub 算术链
 *    也校上；早退路径返回的是刚初始化的 ub，那条路径的返回值不比。
 *
 * 9) m4 的 define(sv1,%bl) 和 define(auxreg,%rbx) 是同一个 rbx 的两个视图，
 *    模板注释里写了「sv1 不用时 bx 可以当辅助寄存器」。这四个内核没用 sv1，
 *    rbx 只被当 64 位/8 位的临时值用（proot 的中转、5*prime），C++ 无对应物。
 *
 * ===================== 契约（差分测试的输入必须落在里面） =====================
 *
 * 上面第 2 条那两个范围就是全部的输入约束，从真实调用点查得：
 *   - prime = 因子基里的素数，>= 3。
 *   - proot  = x[1]，gnfs-lasieve4e.cpp:2938-2945 填的是 modmul32(...) 的结果，
 *              所以 **proot < prime**。proot >= prime 时 cmovnc 那支永远不成立，
 *              root 每段只加 proot 而不减 prime，一路涨到 2^64 附近，
 *              sieve_ptr 飞出数组，汇编和 C++ 都会段错误 —— 不可比，不构成用例。
 *   - root   = x[3]，gnfs-lasieve4e.cpp:3162/3239 填的是 `(...) % p`，
 *              所以 **root < prime**。
 *   - 满足上面两条时 root 始终留在 [0, 2*prime)，sieve_ptr >= 筛数组起点。
 *     落笔最远的是 slinie3：sp = base + j*n_i + root + 2*prime，末级还可能再存
 *     sp+prime，于是最大偏移 < (JPS-1)*n_i + 5*prime < L1_SIZE + 5*prime。
 *     差分测试按 L1_SIZE + 5*pmax 再加前后各 4 KiB 哨兵来覆盖。
 *   - aux_ptr_ub > aux_ptr（真实调用点恒成立）。传 aux_ptr_ub == aux_ptr 或
 *     更小则两边都直接返回，一个素数都不处理。
 */

#include <stdint.h>

#include "siever-config.h"

#ifndef I_bits
#error "I_bits 必须由 Makefile 的 -DI_bits=<11..16> 传入（与汇编的 -Dn_i_bits=I-1 对应）"
#endif

/* ls-defs.asm 里 l1_bits 固定 15；siever-config.h 的 L1_BITS 必须一致。 */
#include "lasieve_ns.h"

namespace lasieve_ns {
static_assert(L1_BITS == 15, "ls-defs.asm 的 l1_bits 固定为 15");
static_assert(I_bits >= 2 && I_bits <= 16, "I_bits 超出 per-I 库的范围");

/* ls-defs.asm: n_i = 2**n_i_bits, j_per_strip = 2**(l1_bits - n_i_bits) */
#define LS_L1_BITS      L1_BITS
#define LS_NI_BITS      (I_bits - 1)
#define LS_NI           ((u64_t)1 << LS_NI_BITS)
#define LS_J_PER_STRIP  ((unsigned)(1u << (LS_L1_BITS - LS_NI_BITS)))


/* 汇编里 sieve_ptr(%rcx) / sieve_ptr_ub(%rax) 都当**裸的 64 位数值**用：
 * sieve_ptr_ub 初值就是筛数组基址的数值，sieve_ptr 是「基址 + 字节偏移」。
 * 这里保持同样的表示，免得写成 C 指针后落到「指向数组尾之外」那种 UB 上。
 * SV() 把那个数值当地址解引用，等价于汇编的 (%rcx) / (%rcx,%r9)。 */
#define SV(addr)        (*(unsigned char *)(uintptr_t)(addr))
#define SV_AT(addr,off) (*(unsigned char *)(uintptr_t)((addr) + (off)))

/* 一个素数记录的公共入口动作。四个 .asm 的段首这 8 条指令一字不差：
 *   movq root,sieve_ptr / xorq auxreg,auxreg / addq proot,root /
 *   leaq (sieve_ptr_ub,sieve_ptr),sieve_ptr / cmovncq prime,auxreg /
 *   addq $n_i,sieve_ptr_ub / addq auxreg,root
 * sp 是返回值，root 和 ub 是带出来的累加量。
 *
 * cmovncq 判的是 addq proot,root 那条 add 的进位 CF。CF 的含义是**无符号
 * 溢出**，不是「结果是负数」—— proot = proot_src - prime 在 64 位里算，
 * proot_src < prime 时它是个接近 2^64 的巨大正数，于是
 *     CF = 1  <=>  root + proot >= 2^64  <=>  root >= prime - proot_src
 *                <=>  root + proot_src >= prime
 * 所以：
 *     CF == 0（cmovnc 生效，auxreg = prime） <=> root + proot_src <  prime
 *     CF == 1（auxreg 保持 0）                 <=> root + proot_src >= prime
 * 也就是
 *     root' = (root + proot_src >= prime) ? root + proot_src - prime
 *                                         : root + proot_src
 * 和 gnfs-lasieve4e.cpp 里 C 版的 `r = r + pr; if (r >= p) r -= p;` 逐字一致，
 * root 始终留在 [0, 2*prime)。**写成「和是不是负数」就正好反过来**，
 * root 会一路减成 2^64 附近的巨值，sieve_ptr 跟着飞出数组。
 * 这里直接按硬件的 CF 写（无符号加法溢出即 t < root），不依赖上面任何范围假设。
 */
static inline u64_t
sl_seg_head(u64_t root, u64_t proot, u64_t prime, u64_t ub, u64_t *root_out)
{
	const u64_t sp = root;                 /* movq root,sieve_ptr */
	const u64_t t = root + proot;          /* addq proot,root，64 位回绕 */
	const int cf = (t < root);             /* 那条 add 的进位 */
	const u64_t aux = cf ? (u64_t)0 : prime;   /* xorq + cmovncq */
	*root_out = t + aux;                   /* addq auxreg,root */
	return ub + sp;                        /* leaq (sieve_ptr_ub,sieve_ptr),sieve_ptr */
}

u32_t *
slinie(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval)
{
	const u64_t base = (u64_t)(uintptr_t)sieve_interval;
	u64_t ub = base;                        /* sieve_ptr_ub, asm %rax */
	u16_t *x = aux_ptr;
	u16_t *ub_ptr = aux_ptr_ub;

	if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
		return (u32_t *)(uintptr_t)ub;      /* 汇编这条路上 %rax 是调用方的值 */
	ub_ptr -= 4;                            /* subq $8,%rsi = 8 字节 = 4 个 u16 元素 */

	for (;;) {
		/* `movq %rdx,%rax` 在 fbi 循环**里面**：每个素数都把 sieve_ptr_ub
		 * 重新置成筛数组基址。漏了这一句，ub 会跨记录累加，第 k 条记录的
		 * 落笔整体偏移 k*j_per_strip*n_i，差得不多但足以飞出数组。 */
		ub = base;
		const u64_t prime = (u64_t)x[0];            /* movzwq (%rdi),%r9 */
		const u64_t proot = (u64_t)x[1] - prime;    /* movzwq 2(%rdi) - prime */
		const unsigned char lg = (unsigned char)x[2];/* movb 4(%rdi),%r10b */
		u64_t root = (u64_t)x[3];                   /* movzwq 6(%rdi),%r8 */

		unsigned j;

		for (j = 0; j < LS_J_PER_STRIP; j++) {
			u64_t sp, again;

			sp = sl_seg_head(root, proot, prime, ub, &root);
			ub += LS_NI;                       /* addq $n_i,sieve_ptr_ub */
			ub -= 5 * prime;                   /* leaq (prime,prime,4) → 5*prime */

			for (;;) {                         /* slinie_loop<i>：do-while */
				SV_AT(sp, prime) = (unsigned char)(SV_AT(sp, prime) + lg);
				SV(sp)            = (unsigned char)(SV(sp)            + lg);
				sp += 2 * prime;              /* leaq (sieve_ptr,prime,2) → 倍数 2 */
				SV_AT(sp, prime) = (unsigned char)(SV_AT(sp, prime) + lg);
				SV(sp)            = (unsigned char)(SV(sp)            + lg);
				again = (ub > sp);            /* cmpq %rcx,%rax + ja */
				sp += 2 * prime;              /* leaq：这条在跳转之前，无条件执行 */
				if (!again)
					break;
			}

			ub += 4 * prime;                    /* leaq (sieve_ptr_ub,prime,4) → 倍数 4 */
			if (ub > sp) {                      /* cmpq + jbe：ub <= sp 就跳过 */
				SV(sp)            = (unsigned char)(SV(sp)            + lg);
				SV_AT(sp, prime) = (unsigned char)(SV_AT(sp, prime) + lg);
				sp += 2 * prime;
			}
			ub += prime;
			if (ub > sp)
				SV(sp) = (unsigned char)(SV(sp) + lg);
		}

		/* cmpq 读的是 leaq **之前**的 aux_ptr。那条 leaq 夹在 cmpq 和 ja
		 * 中间纯粹是给流水线挪位置，它不改标志位，所以分支判的是
		 * 「aux_ptr_ub（已减 4 元素）与**当前** x 谁大」。写成先 x += 4 再比
		 * 会少处理最后一条记录：实测条数是 (nwords + 3) / 4，
		 * nwords = 8 时是 2 条而不是 1 条，nwords = 1..4 时才是 1 条。 */
		if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
			break;
		x += 4;                                /* leaq 8(aux_ptr),aux_ptr */
	}
	return (u32_t *)(uintptr_t)ub;             /* 汇编 ret 时 %rax = 最后的 sieve_ptr_ub */
}

u32_t *
slinie3(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval)
{
	const u64_t base = (u64_t)(uintptr_t)sieve_interval;
	u64_t ub = base;
	u16_t *x = aux_ptr;
	u16_t *ub_ptr = aux_ptr_ub;

	if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
		return (u32_t *)(uintptr_t)ub;
	ub_ptr -= 4;

	for (;;) {
		ub = base;            /* movq %rdx,%rax：每个素数都重置，见 slinie 的说明 */
		const u64_t prime = (u64_t)x[0];
		const u64_t proot = (u64_t)x[1] - prime;
		const unsigned char lg = (unsigned char)x[2];
		u64_t root = (u64_t)x[3];

		unsigned j;

		for (j = 0; j < LS_J_PER_STRIP; j++) {
			u64_t sp, v, again;

			sp = sl_seg_head(root, proot, prime, ub, &root);
			ub += LS_NI;
			/* movb (sieve_ptr,prime),sv0 在 subq prime 之前取，不能挪 */
			v = SV_AT(sp, prime);
			ub -= prime;
			SV(sp) = (unsigned char)(SV(sp) + lg);
			v = (unsigned char)(v + lg);
			SV_AT(sp, prime) = (unsigned char)v;   /* movb sv0,(sieve_ptr,prime) */
			sp += 2 * prime;                       /* leaq (sieve_ptr,prime,2) */
			v = SV(sp);
			v = (unsigned char)(v + lg);
			again = (ub > sp);                     /* cmpq 在 movb 之前、leaq 之后 */
			SV(sp) = (unsigned char)v;              /* movb sv0,(sieve_ptr) */
			ub += prime;                            /* 条件已判完，这句无条件 */
			if (again)
				SV_AT(sp, prime) = (unsigned char)(SV_AT(sp, prime) + lg);
		}

		/* cmpq 读的是 leaq **之前**的 aux_ptr，判的是 (aux_ptr_ub - 4 元素)
		 * 与**当前** x：先比后加，实测条数 (nwords + 3) / 4。 */
		if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
			break;
		x += 4;
	}
	return (u32_t *)(uintptr_t)ub;
}

u32_t *
slinie2(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval)
{
	const u64_t base = (u64_t)(uintptr_t)sieve_interval;
	u64_t ub = base;
	u16_t *x = aux_ptr;
	u16_t *ub_ptr = aux_ptr_ub;

	if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
		return (u32_t *)(uintptr_t)ub;
	ub_ptr -= 4;

	for (;;) {
		ub = base;            /* movq %rdx,%rax：每个素数都重置，见 slinie 的说明 */
		const u64_t prime = (u64_t)x[0];
		const u64_t proot = (u64_t)x[1] - prime;
		const unsigned char lg = (unsigned char)x[2];
		u64_t root = (u64_t)x[3];

		unsigned j;

		for (j = 0; j < LS_J_PER_STRIP; j++) {
			u64_t sp, v, again;

			sp = sl_seg_head(root, proot, prime, ub, &root);
			ub += LS_NI;
			v = SV_AT(sp, prime);                 /* movb (sieve_ptr,prime),sv0 */
			ub -= prime;                          /* subq prime,sieve_ptr_ub */
			SV(sp) = (unsigned char)(SV(sp) + lg);
			v = (unsigned char)(v + lg);
			sp += prime;                          /* leaq (prime,sieve_ptr) */
			again = (ub > sp);
			SV(sp) = (unsigned char)v;             /* movb sv0,(sieve_ptr) */
			ub += prime;
			if (again)
				SV_AT(sp, prime) = (unsigned char)(SV_AT(sp, prime) + lg);
		}

		/* cmpq 读的是 leaq **之前**的 aux_ptr，判的是 (aux_ptr_ub - 4 元素)
		 * 与**当前** x：先比后加，实测条数 (nwords + 3) / 4。 */
		if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
			break;
		x += 4;
	}
	return (u32_t *)(uintptr_t)ub;
}

u32_t *
slinie1(u16_t *aux_ptr, u16_t *aux_ptr_ub, unsigned char *sieve_interval)
{
	const u64_t base = (u64_t)(uintptr_t)sieve_interval;
	u64_t ub = base;
	u16_t *x = aux_ptr;
	u16_t *ub_ptr = aux_ptr_ub;

	if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
		return (u32_t *)(uintptr_t)ub;
	ub_ptr -= 4;

	for (;;) {
		ub = base;            /* movq %rdx,%rax：每个素数都重置，见 slinie 的说明 */
		const u64_t prime = (u64_t)x[0];
		const u64_t proot = (u64_t)x[1] - prime;
		const unsigned char lg = (unsigned char)x[2];
		u64_t root = (u64_t)x[3];

		unsigned j;

		for (j = 0; j < LS_J_PER_STRIP; j++) {
			u64_t sp, v, again;

			sp = sl_seg_head(root, proot, prime, ub, &root);
			ub += LS_NI;
			ub -= prime;
			v = SV(sp);                            /* movb (sieve_ptr),sv0 */
			v = (unsigned char)(v + lg);
			again = (ub > sp);                     /* cmpq %rcx,%rax */
			SV(sp) = (unsigned char)v;             /* movb sv0,(sieve_ptr) */
			ub += prime;
			if (again)
				SV_AT(sp, prime) = (unsigned char)(SV_AT(sp, prime) + lg);
		}

		/* cmpq 读的是 leaq **之前**的 aux_ptr，判的是 (aux_ptr_ub - 4 元素)
		 * 与**当前** x：先比后加，实测条数 (nwords + 3) / 4。 */
		if ((u64_t)(uintptr_t)ub_ptr <= (u64_t)(uintptr_t)x)
			break;
		x += 4;
	}
	return (u32_t *)(uintptr_t)ub;
}
}  /* namespace lasieve_ns */
