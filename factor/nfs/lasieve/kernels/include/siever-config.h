
#ifndef __SIEVER_CONFIG_H__
#define __SIEVER_CONFIG_H__
#include "lasieve_ns.h"
#include <sys/types.h> 
#include <gmp.h> 






#define HAVE_CMOV

#ifdef _WIN64
#define ASM_ATTR   __attribute__((sysv_abi))
#else
#define ASM_ATTR
#endif

#ifdef _WIN64
void bzero(void*,size_t);
#endif

namespace lasieve_ns {
int psp(mpz_t n);
}  /* namespace lasieve_ns */



#define L1_BITS 15
#define ULONG_RI
typedef unsigned u32_t;
typedef int i32_t;
typedef short int i16_t;
typedef unsigned short u16_t;


#ifdef _WIN64
typedef unsigned long long u64_t;
typedef long long i64_t;
typedef unsigned short ushort;
typedef unsigned long long ulong;
#else
 typedef unsigned long u64_t;
typedef long i64_t;
typedef unsigned short ushort;
typedef unsigned long ulong;
#endif

#ifndef U32_MAX
/* 值与 <stdint.h> 的 U32_MAX 相同，但那个头文件不一定被包含进来；
 * 用 #ifndef 挡住，免得谁先包含了 stdint.h 就变成重定义。 */
#define U32_MAX 0xffffffff
#endif


namespace lasieve_ns {
int asm_cmp(ulong*a,ulong*b);
}  /* namespace lasieve_ns */


#define HAVE_ASM_GETBC
void ASM_ATTR asm_getbc(u32_t,u32_t,u32_t,u32_t*,u32_t*,u32_t*,u32_t*);
#define ASM_SCHEDSIEVE
namespace lasieve_ns {
void ASM_ATTR schedsieve(unsigned char,unsigned char*,u16_t*,u16_t*);
}  /* namespace lasieve_ns */

// tdsieve_sched2buf 曾在这里声明，并由 ASM_SCHEDTDSIEVE2 开关控制：
//   #if defined(ASM_SCHEDTDSIEVE2) && !defined(AVX512_TDSCHED)
//       b0 = tdsieve_sched2buf(...);
//   #else   ← AVX-512 gather 版或标量 C 版
//   #endif
// 这个开关原先无条件打开，但函数本身随 .asm -> .cpp 迁移一起没了，全仓库没有任何
// 定义。于是只有定义了 AVX512_TDSCHED 的构建（yafu 本体走 AVX512_ALL=1）能链接，
// 独立回归不传 AVX 开关时必然 undefined reference。开关与声明一并删除，
// 剩下的两条路径都是完整实现。

#define ASM_MPZ_TD
#define PREINVERT
#if 1
namespace lasieve_ns {
void MMX_TdAllocate(int,size_t,size_t);
u16_t*MMX_TdInit(int,u16_t*,u16_t*,u32_t*,int);
void MMX_TdUpdate(int,int);
u32_t*MMX_Td(u32_t*,int,u16_t);
}  /* namespace lasieve_ns */

#define MMX_TD
#define MMX_REGW 8
#endif
#define ASM_LINESIEVER
namespace lasieve_ns {
u32_t*ASM_ATTR slinie(u16_t*,u16_t*,unsigned char*);
}  /* namespace lasieve_ns */

#define ASM_LINESIEVER3
namespace lasieve_ns {
u32_t*ASM_ATTR slinie3(u16_t*,u16_t*,unsigned char*);
}  /* namespace lasieve_ns */

#define ASM_LINESIEVER2
namespace lasieve_ns {
u32_t*ASM_ATTR slinie2(u16_t*,u16_t*,unsigned char*);
}  /* namespace lasieve_ns */

#define ASM_LINESIEVER1
namespace lasieve_ns {
u32_t*ASM_ATTR slinie1(u16_t*,u16_t*,unsigned char*);
}  /* namespace lasieve_ns */

#define ASM_TDSLINIE1
namespace lasieve_ns {
u32_t*ASM_ATTR tdslinie1(u16_t*,u16_t*,unsigned char*,u32_t**);
}  /* namespace lasieve_ns */

#define ASM_TDSLINIE2
namespace lasieve_ns {
u32_t*ASM_ATTR tdslinie2(u16_t*,u16_t*,unsigned char*,u32_t**);
}  /* namespace lasieve_ns */

#define ASM_TDSLINIE3
namespace lasieve_ns {
u32_t*ASM_ATTR tdslinie3(u16_t*,u16_t*,unsigned char*,u32_t**);
}  /* namespace lasieve_ns */

#define ASM_TDSLINIE
namespace lasieve_ns {
u32_t*ASM_ATTR tdslinie(u16_t*,u16_t*,unsigned char*,u32_t**);
}  /* namespace lasieve_ns */

#define ASM_SEARCH0
namespace lasieve_ns {
u32_t ASM_ATTR lasieve_search0(unsigned char*,unsigned char*,unsigned char*,
unsigned char*,unsigned char*,u16_t*,unsigned char*);
}  /* namespace lasieve_ns */

#define  MAX_FB_PER_P 2
#define ASM_RESCALE


#if 1
#define VERY_LARGE_Q
#endif

namespace lasieve_ns {
void ASM_ATTR rescale_interval1(unsigned char*,u64_t);
void ASM_ATTR rescale_interval2(unsigned char*,u64_t);


u64_t ASM_ATTR asm_modadd64(u64_t,u64_t);
u64_t ASM_ATTR asm_modmul64(u64_t,u64_t);
}  /* namespace lasieve_ns */




/*:1*//*2:*/

#define FB_RAS 3

/*:2*//*4:*/

#define N_PRIMEBOUNDS 12

/* 计时计数器的入口。这批函数原先定义在内联汇编里，是裸名字；
 * C 时代靠隐式声明，转成 C++ 之后必须在 namespace lasieve_ns 里显式声明。
 * 本文件没有 extern "C" 块，所以下面一律带命名空间。
 * 只声明函数；zeitcounter / zeitsum 这些是数据符号，不在这里动。 */
namespace lasieve_ns {
ulong asmgetclock(void);
void initzeit(ulong t);
void printzeit(ulong i);
void zeitA(ulong i);
void zeitB(ulong i);
void zeita(ulong i);
void zeitb(ulong i);
}  /* namespace lasieve_ns */


#endif

/*:4*/
