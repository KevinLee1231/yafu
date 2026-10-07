/*----------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Ben Buhrow. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

Some parts of the code (and also this header), included in this 
distribution have been reused from other sources. In particular I 
have benefitted greatly from the work of Jason Papadopoulos's msieve @ 
www.boo.net/~jasonp, Scott Contini's mpqs implementation, and Tom St. 
Denis Tom's Fast Math library.  Many thanks to their kind donation of 
code to the public domain.
       				   --bbuhrow@gmail.com 11/24/09
----------------------------------------------------------------------*/

#ifndef my_types
#define my_types

#include <stdlib.h>

/* system-specific stuff ---------------------------------------*/

#ifdef __APPLE__
    #include <malloc/malloc.h>
#else
    #include <malloc.h>
#endif


	#include <sys/types.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <errno.h>
	#include <pthread.h>


/* system-independent header files ------------------------------------*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/timeb.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <signal.h>
#include <memory.h>
#include <float.h>


#include <stdint.h>

// to see what defines are set do:
//gcc -dM -E - < nul

/* basic types  -------------------------------------------------------*/

	
	//for gettimeofday using gcc
	#include <sys/time.h>

#if defined (__INTEL_COMPILER)
#define ALIGNED_MEM __declspec(align(64))
#else
#define ALIGNED_MEM __attribute__((aligned(64)))
#endif

#if defined(__GNUC__) && defined(USE_AVX512F) && !defined(__INTEL_COMPILER)
#define _MM_SCALE_1 1
#define _MM_SCALE_2 2
#define _MM_SCALE_4 4
#endif

	//check for MINGWXX first, because mingw also defines x86_64 and/or i386
		#include <mm_malloc.h>
		#define align_free _aligned_free //_mm_free
		#define strto_fpdigit strtoul
		#define strto_uint64 _strtoui64

		//sleep in milliseconds
		#define MySleep(x) Sleep((x))

		typedef unsigned char uint8;
		typedef unsigned short uint16;
		typedef unsigned int uint32;
		typedef long long unsigned int uint64;
		typedef unsigned int fp_digit;
		typedef long long unsigned int fp_word;
		typedef int fp_signdigit;
		typedef long long int fp_signword;

		#define MAX_DIGIT 0xffffffff
		#define BITS_PER_DIGIT 32
		#define DEC_DIGIT_PER_WORD 9
		#define HEX_DIGIT_PER_WORD 8
		#define HIBITMASK 0x80000000
		#define MAX_HALF_DIGIT 0xffff
		#define MAX_DEC_WORD 0x3b9aca00
		#define ADDRESS_BITS 2

		#define PRId64 "lld"
		#define PRIu64 "llu"
		#define PRIx64 "llx"
		
		#ifndef RS6K
		typedef char int8;
		typedef short int16;
		typedef int32_t int32;
		typedef int64_t int64;
		#endif


        // sleep in milliseconds		
        #define MySleep(x) usleep((x)*1000)	
		#define strto_fpdigit strtoull
		#define strto_uint64 strtoull

		#define align_free free
		typedef unsigned char uint8;
		typedef unsigned short uint16;
		typedef uint32_t uint32;
		typedef uint64_t uint64;
		typedef uint64_t fp_digit;
		typedef uint64_t fp_word;
		typedef int64_t fp_signdigit;
		typedef int64_t fp_signword;
		#define MAX_DIGIT 0xffffffffffffffff
		#define BITS_PER_DIGIT 64
		#define DEC_DIGIT_PER_WORD 19
		#define HEX_DIGIT_PER_WORD 16
		#define HIBITMASK 0x8000000000000000
		#define MAX_HALF_DIGIT 0xffffffff
		#define MAX_DEC_WORD 0x8AC7230489E80000
		#define ADDRESS_BITS 3

		#define PRId64 "ld"
		#define PRIu64 "lu"
		#define PRIx64 "lx"

		#ifndef RS6K
		typedef char int8;
		typedef short int16;
		typedef int32_t int32;
		typedef int64_t int64;
		#endif


	
		//sleep in milliseconds
		#define MySleep(x) usleep((x)*1000)
		#define strto_fpdigit strtoul
		#define strto_uint64 strtoull
		#define align_free free

		typedef unsigned char uint8;
		typedef unsigned short uint16;
		typedef uint32_t uint32;
		typedef uint64_t uint64;
		typedef uint32_t fp_digit;
		typedef uint64_t fp_word;
		typedef int32_t fp_signdigit;
		typedef int64_t fp_signword;
		#define MAX_DIGIT 0xffffffff
		#define MAX_HALF_DIGIT 0xffff
		#define BITS_PER_DIGIT 32
		#define DEC_DIGIT_PER_WORD 9
		#define HEX_DIGIT_PER_WORD 8
		#define HIBITMASK 0x80000000
		#define MAX_DEC_WORD 0x3b9aca00
		#define ADDRESS_BITS 2

		#define PRId64 "lld"
		#define PRIu64 "llu"
		#define PRIx64 "llx"
		
		#ifndef RS6K
		typedef char int8;
		typedef short int16;
		typedef int32_t int32;
		typedef int64_t int64;
		#endif

	
		#error "unrecognized architecture in gcc"



	#error "unrecognized compiler"

#endif /* my types */


