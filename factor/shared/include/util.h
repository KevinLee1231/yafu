/*--------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Jason Papadopoulos. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

$Id: util.h 1005 2016-11-11 15:43:21Z jasonp_sf $
--------------------------------------------------------------------*/

#ifndef _MS_UTIL_H_
#define _MS_UTIL_H_

/* system-specific stuff ---------------------------------------*/
#define _POSIX_C_SOURCE 200112L
	#include <fcntl.h>
	#include <unistd.h>
	#include <errno.h>
	#include <pthread.h>
	#include <sys/resource.h>
	#include <sys/time.h>
	#include <float.h>
	#include <dlfcn.h>

/* system-independent header files ------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
	#include <inttypes.h>
#include <math.h>




#define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL


double msieve_difftime(struct timeval* start, struct timeval* end);

#define msieve_gettimeofday gettimeofday

/* basic types  -------------------------------------------------------*/

	typedef unsigned char uint8;
	typedef unsigned short uint16;
	typedef unsigned int uint32;
	typedef uint64_t uint64;
	
	#ifndef RS6K
	typedef char int8;
	typedef short int16;
	typedef int int32;
	typedef int64_t int64;
	#endif

	typedef void * libhandle_t;

/* useful functions ---------------------------------------------------*/

#define MIN(a,b) ((a) < (b)? (a) : (b))
#define MAX(a,b) ((a) > (b)? (a) : (b))

	#define INLINE inline

	#define INLINE /* nothing */

#if defined(__GNUC__) && __GNUC__ >= 3
	#define PREFETCH(addr) __builtin_prefetch(addr) 
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
	#define PREFETCH(addr) PreFetchCacheLine(PF_TEMPORAL_LEVEL_1, addr)
#else
	#define PREFETCH(addr) /* nothing */
#endif

#define WORDS_IN(type) (sizeof(type) / sizeof(uint32))

/* yafu's ytools.h declares xmalloc/xcalloc/xrealloc with external linkage
 * and its own enum cpu_type.  Both provide the same behaviour, so the first
 * header to arrive provides them and the other stands down -- otherwise a
 * translation unit that legitimately needs both fails to compile. */
#ifndef YA_ALLOC_DECLARED
#define YA_ALLOC_DECLARED

static INLINE void * xmalloc(size_t len) {
	void *ptr = malloc(len);
	if (ptr == NULL) {
		printf("failed to allocate %u bytes\n", (uint32)len);
		exit(-1);
	}
	return ptr;
}

static INLINE void * xcalloc(size_t num, size_t len) {
	void *ptr = calloc(num, len);
	if (ptr == NULL) {
		printf("failed to calloc %u bytes\n", (uint32)(num * len));
		exit(-1);
	}
	return ptr;
}

static INLINE void * xrealloc(void *iptr, size_t len) {
	void *ptr = realloc(iptr, len);
	if (ptr == NULL) {
		printf("failed to reallocate %u bytes\n", (uint32)len);
		exit(-1);
	}
	return ptr;
}

#endif /* YA_ALLOC_DECLARED */

void * aligned_malloc(size_t len, uint32 align);
void aligned_free(void *newptr);
uint64 read_clock(void);
double get_cpu_time(void);
double get_wall_time(void);
void set_idle_priority(void);
uint64 get_file_size(char *name);
uint64 get_ram_size(void);

libhandle_t load_dynamic_lib(const char *libname);
void unload_dynamic_lib(libhandle_t h);
void * get_lib_symbol(libhandle_t h, const char *symbol_name);

#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static INLINE uint32 
get_rand(uint32 *rand_seed, uint32 *rand_carry) {
   
	/* A multiply-with-carry generator by George Marsaglia.
	   The period is about 2^63. */

	#define RAND_MULT 2131995753

	uint64 temp;

	temp = (uint64)(*rand_seed) * 
		       (uint64)RAND_MULT + 
		       (uint64)(*rand_carry);
	*rand_seed = (uint32)temp;
	*rand_carry = (uint32)(temp >> 32);
	return (uint32)temp;
}

#ifndef YA_CPU_TYPE_DECLARED
#define YA_CPU_TYPE_DECLARED

/* for turning on CPU-specific code */

enum cpu_type {
	cpu_generic,
	cpu_pentium,
	cpu_pentium2,
	cpu_pentium3,
	cpu_pentium4,
	cpu_pentium_m,
	cpu_core,
	cpu_athlon,
	cpu_athlon_xp,
	cpu_opteron
};

#endif /* YA_CPU_TYPE_DECLARED */

void get_cache_sizes(uint32 *level1_cache, uint32 *level2_cache);
enum cpu_type get_cpu_type(void);

/* CPU-specific capabilities */

/* assume for all CPUs, even non-x86 CPUs. These guard
   assembly language that has other guards anyway, and
   the only CPU that doesn't have these instructions is
   the classic Pentium */

#define HAS_CMOV

#if defined(__x86_64__) && !defined(FORCE_GENERIC)
// protect non-x86 builds
#define HAS_MMX
#endif

#if defined(CPU_GENERIC)
	#define MANUAL_PREFETCH
		#define HAS_MANY_REGISTERS

#elif defined(CPU_PENTIUM2) 
	#define MANUAL_PREFETCH

#elif defined(CPU_ATHLON)
	#define MANUAL_PREFETCH
	#define HAS_AMD_MMX

#elif defined(CPU_PENTIUM3) 
	#define MANUAL_PREFETCH
	#define HAS_SSE

#elif defined(CPU_ATHLON_XP)
	#define HAS_SSE

#elif defined(CPU_PENTIUM4) || defined(CPU_PENTIUM_M) || \
	defined(CPU_CORE) || defined(CPU_OPTERON)
	#define HAS_SSE
	#define HAS_SSE2
		#define HAS_MANY_REGISTERS
#endif

#if !defined(HAS_SSE) && defined(__x86_64__)
	#define HAS_SSE
#endif
#if !defined(HAS_SSE2) && defined(__x86_64__)
	#define HAS_SSE2
#endif

/* this byzantine complexity sets up the correct assembly
   language syntax based on the compiler, OS and word size 
   
   Where an inline assembler segment is provided in both 
   GCC and MSC format (i.e. alternative sections), the 
   Intel compiler is configured using guards with an A
   suffix to prefer the native version (GCC on Linux/Unix, 
   MSC on Windows). Where an inline assembler segment is 
   only provided in GCC or MSC format but not both (i.e. 
   exclusive sections) the guards have an X suffix.

   The Intel compiler on Windows appears to have some
   bugs in its processing of GCC inline assembler code.
   These are escaped with the _ICL_WIN_ define
   */

#if defined(__INTEL_COMPILER)

	#define ASM_G __asm__
	#define ASM_M __asm

	/* for inline assembler on Unix/Linux */
	#if defined(__unix__)        
		#if defined(__x86_64__)
			#define GCC_ASM64A
			#define GCC_ASM64X
			#define MSC_ASM64X
		#elif defined(__i386__)
			#define GCC_ASM32A
			#define GCC_ASM32X
			#define MSC_ASM32X
		#endif
	#endif

	/* for inline assembler on Windows */
		#elif defined(_M_IX86)

#elif defined(__GNUC__)

	#define ASM_G __asm__

	#if defined(__x86_64__) 
		#define GCC_ASM64A
		#define GCC_ASM64X
	#elif defined(__i386__)
		#define GCC_ASM32A
		#define GCC_ASM32X
	#endif

#elif defined(_MSC_VER)

	#define ASM_M __asm

		#define MSC_ASM32A
		#define MSC_ASM32X
#endif

/* loop alignment directives need to know whether
   we're using MSVC */

	#define ALIGN_LOOP   ".p2align 4,,7 \n\t" 
#endif /* _MS_UTIL_H_ */
