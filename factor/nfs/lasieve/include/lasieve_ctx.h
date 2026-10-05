/* lasieve_ctx.h -- 每个 siever 实例自己的状态
 *
 * siever 原本把 ECM / PM1 的全部缓存、montgomery 预计算栈、统计量都放在
 * 文件级全局里，一个进程只能有一份。yafu 要同时跑 6 个 siever（I 值
 * 11..16），状态必须按实例分开。
 *
 * 状态存放在本结构体里；LASIEVE_CTX 指向当前实例（thread-local），用来
 * 避免给几百个函数签名逐个加参数。真正持有状态的是结构体本身，LASIEVE_CTX
 * 只是访问路径。
 *
 * 汇编直接读的那几个符号不在这里，见 los_ctx.h 顶部说明。
 */
#ifndef _LASIEVE_LASIEVE_CTX_H_
#define _LASIEVE_LASIEVE_CTX_H_

#include <gmp.h>
#include <stddef.h>
#include "siever-config.h"
#include "montgomery_mul.h"

#ifdef __cplusplus
extern "C" {  /* yafu-cpp-linkage */
#endif

/* 各 .c 原本各自 #define uchar；在头里给一个受保护的统一版本 */
#ifndef LASIEVE_UCHAR_DEFINED
#define LASIEVE_UCHAR_DEFINED
#define uchar unsigned char
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ECM 的 B2 scheme 描述（原先是 ecm.c 里的 typedef） */
typedef struct {
  u32_t B1;
  u32_t B2;
  u32_t D;
  uchar *rpc;
  uchar *tests;
  u32_t tablen;
  u32_t *tab;
  u32_t beginD;
  u32_t endD;
} lasieve_scheme2_t;

/* PM1 的 B1 scheme 描述（原先是 pm1.c 里的 typedef） */
typedef struct {
  u32_t B1;
  u32_t len;
  uchar *ex;
} lasieve_pm1_scheme1_t;

/* 一个 siever 实例的全部可变状态 */
typedef struct lasieve_ctx {
  /* --- ECM：素数位图与加法链，惰性初始化 --- */
  int      ecm_is_init;
  uchar   *ecm_prime_bit;
  u32_t    ecm_prime_max;
  uchar   *addition_chain;
  u32_t   *B1_ac;
  u32_t   *B1_prime;
  u32_t    B1_len;
  u32_t    B1_ac_maxlen;
  u32_t    B1_max;

  /* --- ECM：montgomery B1/B2 预计算与栈 --- */
  lasieve_scheme2_t *B2_scheme;
  size_t   B2_scheme_len;
  size_t   B2_scheme_alloc;
  ulong   *mm_stack;
  size_t   mm_stack_alloc;
  ulong  **mm_B1_x;
  ulong  **mm_B1_z;
  ulong  **mm_B2_inv;
  ulong  **mm_B2_tab1;
  ulong  **mm_B2_tab2;
  u32_t    mm_B2_inv_len;
  u32_t    mm_B2_tab1_len;
  u32_t    mm_B2_tab2_len;

  /* --- PM1：素数位图与 scheme 表，惰性初始化 --- */
  int      pm1_is_init;
  uchar   *pm1_prime_bit;
  u32_t    pm1_prime_max;
  u32_t    pm1_index1;
  u32_t    pm1_index2;
  lasieve_pm1_scheme1_t *B1_scheme;
  size_t   B1_scheme_len;
  size_t   B1_scheme_alloc;
  lasieve_scheme2_t     *pm1_B2_scheme;
  size_t   pm1_B2_scheme_len;
  size_t   pm1_B2_scheme_alloc;
  ulong   *pm1_mm_stack;
  size_t   pm1_mm_stack_alloc;
  ulong  **pm1_mm_B2_tab1;
  u32_t    pm1_mm_B2_tab1_len;
  ulong  **pm1_mm_B2_tab2;
  u32_t    pm1_mm_B2_tab2_len;

  /* --- 跨模块共享的 GMP 暂存 ---
         原先 ecm.c 和 pm1.c 各有一份 -fcommon tentative definition，链接时
         合并成同一块内存，两边都能 *fptr=&gmp_f 把因子交回调用方。现在两边
         都指到这里，共享关系不变。 */
  mpz_t    gmp_f;
  mpz_t    gmp_aux0;
  mpz_t    gmp_aux1;
  mpz_t    gmp_aux2;
  mpz_t    gmp_aux3;
} lasieve_ctx;

/* 分配一个清零的实例；ecm/pm1 惰性初始化，所以清零就是正确的初态。
 * 返回的实例里 gmp_* 已经 mpz_init 过了，ecm_init / pm1_init 不必再来一次。 */
lasieve_ctx *lasieve_ctx_new(void);
void         lasieve_ctx_free(lasieve_ctx *ctx);

/* 当前实例。进程内每个 siever 线程设一次。
 * 走函数而不是裸宏：实例没建好时立刻报错退出，而不是在几百个函数里
 * 静默地解引用空指针。 */
extern __thread lasieve_ctx *lasieve_current_ctx;
lasieve_ctx *lasieve_ctx_current(void);

#define LASIEVE_CTX (lasieve_ctx_current())

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
}  /* yafu-cpp-linkage */
#endif
#endif /* _LASIEVE_LASIEVE_CTX_H_ */
