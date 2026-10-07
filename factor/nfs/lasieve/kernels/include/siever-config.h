
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

#define U32_MAX 0xffffffff
#define I32_MAX INT_MAX


namespace lasieve_ns {
int asm_cmp(ulong*a,ulong*b);
}  /* namespace lasieve_ns */


#define HAVE_ASM_GETBC
void ASM_ATTR asm_getbc(u32_t,u32_t,u32_t,u32_t*,u32_t*,u32_t*,u32_t*);
#define ASM_SCHEDSIEVE
namespace lasieve_ns {
void ASM_ATTR schedsieve(unsigned char,unsigned char*,u16_t*,u16_t*);

void ASM_ATTR schedsieve_1(unsigned char,unsigned char*,u16_t*,u16_t*);
}  /* namespace lasieve_ns */

#define ASM_SCHEDTDSIEVE2
u16_t**ASM_ATTR tdsieve_sched2buf(u16_t**,u16_t*,unsigned char*,u16_t**,u16_t**);

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

#define HAVE_MM_LASIEVE_SETUP
#define HAVE_MM_LASIEVE_SETUP2
#define HAVE_MM_LASIEVE_SETUP_64
#define  MAX_FB_PER_P 2
#define ASM_RESCALE


#if 1
#define VERY_LARGE_Q
#endif


u32_t*ASM_ATTR asm_lasieve_mm_setup0(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup1(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup2(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup3(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup20(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup21(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup22(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup23(u32_t*,u32_t*,size_t,u32_t,u32_t,u32_t,u32_t,u32_t*);


u32_t*ASM_ATTR asm_lasieve_mm_setup0_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup1_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup2_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup3_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup20_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup21_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup22_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);
u32_t*ASM_ATTR asm_lasieve_mm_setup23_64(u32_t*,u32_t*,size_t,u64_t,u64_t,u64_t,u64_t,u32_t*);


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

/* 计时计数器的入口。C 时代靠隐式声明，转成 C++ 之后必须显式声明，
 * 而且必须落在上面那段 extern "C" 里 —— 定义在汇编里，是裸名字。
 * 只声明函数；zeitcounter / zeitsum 这些是数据符号，不在这里动。 */

/* 计时计数器的入口。C 时代靠隐式声明，转成 C++ 之后必须显式声明，
 * 而且必须落在上面那段 extern "C" 里 —— 定义在汇编里，是裸名字。
 * 只声明函数；zeitcounter / zeitsum 这些是数据符号，不在这里动。 */
namespace lasieve_ns {
void initzeit(ulong t);
void printzeit(ulong i);
void zeitA(ulong i);
void zeitB(ulong i);
void zeita(ulong i);
void zeitb(ulong i);
}  /* namespace lasieve_ns */


/* 计时计数器的入口。C 时代靠隐式声明，转成 C++ 之后必须显式声明，
 * 而且必须落在上面那段 extern "C" 里 —— 定义在汇编里，是裸名字。
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
