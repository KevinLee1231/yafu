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

#include "common.h"

#include "qs_impl.h"
#include "ytools.h"
#include "poly_macros_32k.h"
#include "poly_macros_common.h"
#include "poly_macros_common_avx2.h"
#include <immintrin.h>

// protect avx2 code under MSVC builds.  USE_AVX2 should be manually
// enabled at the top of qs.h for MSVC builds on supported hardware.
// this code is exclusive to 64-bit gcc format so it gets a GCC_ASM64X guard.
#if defined( USE_AVX2 ) && defined(GCC_ASM64X) //

// we have put (j - bound_val + i) | (root1 & 0x7fff) into ymm10.
#define UPDATE_ROOT1_NEW(it) \
		"movl   (%%r10,%%r8,4),%%r14d \n\t"	    /* numptr_p[bnum] */ \
        "incl   (%%r10,%%r8,4) \n\t"		    /* store new numptr to memory */ \
        "shll   $" BUCKET_BITStxt ",%%r8d \n\t"	/* bnum << BUCKET_BITS */ \
        "vpextrd $" it ",%%xmm10,%%edi \n\t"	/* load this lanes (j - bound_val + i) | (root1 & 0x7fff) */ \
		"addl   %%r14d,%%r8d \n\t"				/* (bnum << 11) + numptr_p[bnum] */ \
        "movl   %%edi,(%%r11,%%r8,4) \n\t"		/* store new fb_index/loc to memory */

#define UPDATE_ROOT2_NEW(it) \
		"movl   (%%r10,%%r9,4),%%r14d \n\t"	    /* numptr_p[bnum] */ \
        "incl   (%%r10,%%r9,4) \n\t"		    /* store new numptr to memory */ \
        "shll   $" BUCKET_BITStxt ",%%r9d \n\t"	/* bnum << BUCKET_BITS */ \
        "vpextrd $" it ",%%xmm11,%%edi \n\t"	/* load this lanes (j - bound_val + i) | (root1 & 0x7fff) */ \
		"addl   %%r14d,%%r9d \n\t"				/* (bnum << 11) + numptr_p[bnum] */ \
        "movl   %%edi,(%%r11,%%r9,4) \n\t"		/* store new fb_index/loc to memory */


// macro for iteratively adding an element (specified by r8d) to the end of a bucket list
#define UPDATE_ROOT1_LOOP_NEW(it) \
        "vpextrd $" it ",%%xmm10,%%edi \n\t"	/* load this lanes (j - bound_val + i) | (root1 & 0x7fff) */ \
		"1:		\n\t"	 						/* beginning of loop */ \
		"movl   %%r8d,%%ebx \n\t"				/* copy root1 to ebx */ \
        "movl   %%r8d,%%eax \n\t"				/* copy root1 to eax */ \
		"shrl   $15,%%ebx \n\t"	                /* right shift root1 by 15  = bnum */ \
        "andl   $32767,%%eax \n\t"	            /* root1 & BLOCKSIZEm1 */ \
		"movl   %%ebx,%%ecx \n\t"				/* copy bnum to start making address */ \
		"movl   (%%r10,%%rbx,4),%%r14d \n\t"	/* numptr_p[bnum] */ \
		"shll   $" BUCKET_BITStxt ",%%ecx \n\t"	/* bnum << BUCKET_BITS */ \
        "orl	%%eax,%%edi \n\t"				/* combine two words to reduce write port pressure */ \
		"addl   %%r14d,%%ecx \n\t"				/* (bnum << 11) + numptr_p[bnum] */ \
		"addl   $1,(%%r10,%%rbx,4) \n\t"		/* store new numptr to memory */ \
		"movl   %%edi,(%%r11,%%rcx,4) \n\t"		/* store new fb_index/loc to memory */ \
		"addl	%%edx,%%r8d \n\t"				/* increment root by prime */ \
        "andl   $0xffff0000, %%edi \n\t"        /* clear low half to re-use high half */ \
		"cmpl   %%r13d,%%r8d \n\t"				/* root > interval? */ \
		"jb		1b \n\t"						/* repeat if necessary */

// macro for iteratively adding an element (specified by r9d) to the end of a bucket list
#define UPDATE_ROOT2_LOOP_NEW(it) \
		"vpextrd $" it ",%%xmm10,%%edi \n\t"	/* load this lanes (j - bound_val + i) | (root1 & 0x7fff) */ \
		"1:		\n\t"	 						/* beginning of loop */ \
		"movl   %%r9d,%%ebx \n\t"				/* copy root2 to ebx */ \
        "movl   %%r9d,%%eax \n\t"				/* copy root2 to eax */ \
		"shrl   $15,%%ebx \n\t"	                /* right shift root2 by 15  = bnum */ \
        "andl   $32767,%%eax \n\t"	            /* root2 & BLOCKSIZEm1 */ \
		"movl   %%ebx,%%ecx \n\t"				/* copy bnum to start making address */ \
		"movl   (%%r10,%%rbx,4),%%r14d \n\t"	/* numptr_p[bnum] */ \
		"shll   $" BUCKET_BITStxt ",%%ecx \n\t"	/* bnum << BUCKET_BITS */ \
        "orl	%%eax,%%edi \n\t"				/* combine two words to reduce write port pressure */ \
		"addl   %%r14d,%%ecx \n\t"				/* (bnum << 11) + numptr_p[bnum] */ \
		"addl   $1,(%%r10,%%rbx,4) \n\t"		/* store new numptr to memory */ \
		"movl   %%edi,(%%r11,%%rcx,4) \n\t"		/* store new fb_index/loc to memory */ \
		"addl	%%edx,%%r9d \n\t"				/* increment root by prime */ \
        "andl   $0xffff0000, %%edi \n\t"        /* clear low half to re-use high half */ \
		"cmpl   %%r13d,%%r9d \n\t"				/* root > interval? */ \
		"jb		1b \n\t"						/* repeat if necessary */
   

#define CHECK_NEW_SLICE_ASM_NEW \
		"cmpl   104(%%rsi),%%r15d	\n\t"		/* compare j with check_bound */ \
			/* note this is the counter j, not the byte offset j */ \
		"jge     1f \n\t"						/* jump into "if" code if comparison works */ \
			/* else, this is the "else-if" check */ \
		"movl   %%r15d,%%ebx \n\t"				/* copy j into ebx */ \
		"subl   96(%%rsi),%%ebx \n\t"			/* ebx = j - bound_val */ \
		"cmpl   $0xffff,%%ebx \n\t"				/* compare to 2^16 */ \
		"jbe    2f \n\t"						/* exit CHECK_NEW_SLICE if this comparison fails too */ \
			/* now we are in the else-if block of CHECK_NEW_SLICE */ \
		"xorq	%%rdx, %%rdx \n\t"				/* clear rdx */ \
		"movl   100(%%rsi),%%edx \n\t"		/* move bound_index into rdx */ \
		"movq   64(%%rsi),%%r9 \n\t"			/* move lp_bucket_p ptr into r9 */ \
		"movq	16(%%r9),%%r8 \n\t"			/* move lp_bucket_p->logp ptr into r8 */ \
		"movq	56(%%rsi),%%r14 \n\t"			/* move updata_data.logp pointer into r14 */ \
		"movzbl (%%r14,%%r15,1),%%ebx \n\t"		/* bring in logp */ \
		"movb	%%bl, 108(%%rsi) \n\t"		/* shove logp into output */ \
		"movb   %%bl, (%%r8,%%rdx,1) \n\t"		/* mov logp into lp_bucket_p->logp[bound_index] */ \
		"incq   %%rdx \n\t"						/* increment bound_index locally */ \
		"movl   %%edx,100(%%rsi) \n\t"		/* copy bound_index back to structure */ \
		"movq	8(%%r9),%%r8 \n\t"			/* move lp_bucket_p->fb_bounds ptr into r8 */ \
		"movl   %%r15d,(%%r8,%%rdx,4) \n\t"		/* mov j into lp_bucket_p->fb_bounds[bound_index] */ \
			/* note this is the counter j, not the byte offset j */ \
		"movl   %%r15d,96(%%rsi) \n\t"		/* bound_val = j */ \
            "movq	120(%%rsi),%%rdi \n\t"			/* edi = polyscratch */ \
            "vmovdqa     (%%rdi), %%ymm15 \n\t" /* (j - bound_val) == 0, so just move in the lane offset */ \
            "vpslld	$16, %%ymm15, %%ymm15 \n\t"	/* put (j - bound_val + i) into high half of 32-bit words */ \
		"xorq	%%rbx, %%rbx \n\t"				/* clear rbx */ \
		"movl   92(%%rsi),%%ebx \n\t"			/* put numblocks into ebx */ \
		"shll	$2,%%ebx \n\t"					/* numblocks * 4 (translate to bytes) */ \
		"shll	$1,%%ebx \n\t"					/* numblocks << 1 (negative blocks are contiguous) */ \
		"addq   %%rbx,8(%%rsi) \n\t"			/* numptr_p += (numblocks << 1) */ \
		"addq   %%rbx,0(%%rsi) \n\t"			/* numptr_n += (numblocks << 1) */ \
		"shlq   $" BUCKET_BITStxt ",%%rbx \n\t"	/* numblocks << (BUCKET_BITS + 1) */ \
			/* note also, this works because we've already left shifted by 1 */ \
		"addq   %%rbx,24(%%rsi) \n\t"			/* sliceptr_p += (numblocks << 11) */ \
		"addq   %%rbx,16(%%rsi) \n\t"			/* sliceptr_n += (numblocks << 11) */ \
		"addl   $" HALFBUCKET_ALLOCtxt ",104(%%rsi) \n\t"		/* add 2^(BUCKET_BITS-1) to check_bound */ \
		"cmp	%%rax,%%rax \n\t"				/* force jump */ \
		"je		2f \n\t"						/* jump out of CHECK_NEW_SLICE */ \
		"1:		\n\t"									\
			/* now we are in the if block of CHECK_NEW_SLICE */ \
		"xorl   %%ecx,%%ecx \n\t"				/* ecx = room  = 0 */ \
		"xorq	%%rbx, %%rbx \n\t"				/* loop counter = 0 */ \
		"cmpl   92(%%rsi),%%ebx \n\t"			/* compare with numblocks */ \
		"jae    3f \n\t"						/* jump past loop if condition met */ \
			/* condition not met, put a couple things in registers */ \
		"movq	8(%%rsi),%%r10 \n\t"			/* numptr_p into r10 */ \
		"movq	0(%%rsi),%%r11 \n\t"			/* numptr_n into r11 */ \
		"5:		\n\t"							\
			/* now we are in the room loop */ \
			/* room is in register ecx */ \
		"movl   (%%r10,%%rbx,4),%%eax \n\t"		/* value at numptr_p + k */ \
		"movl   (%%r11,%%rbx,4),%%edx \n\t"		/* value at numptr_n + k */ \
		"cmpl   %%ecx,%%eax \n\t"				/* *(numptr_p + k) > room ? */ \
		"cmova  %%eax,%%ecx \n\t"				/* new value of room if so */ \
		"cmpl   %%ecx,%%edx \n\t"				/* *(numptr_p + k) > room ? */ \
		"cmova  %%edx,%%ecx \n\t"				/* new value of room if so */ \
		"incq   %%rbx \n\t"						/* increment counter */ \
		"cmpl   92(%%rsi),%%ebx \n\t"			/* compare to numblocks */ \
		"jl     5b \n\t"						/* iterate loop if condition met */ \
		"3:		\n\t"							\
		"movl   $" BUCKET_ALLOCtxt ",%%ebx \n\t"	/* move bucket allocation into register for subtraction */ \
		"subl   %%ecx,%%ebx \n\t"				/* room = bucket_alloc - room */ \
		"cmpl   $31,%%ebx \n\t"					/* answer less than 32? */ \
		"movl   %%ebx,%%ecx \n\t"				/* copy answer back to room register */ \
		"jle    4f \n\t"						/* jump if less than */ \
		"sarl   $1,%%ebx	\n\t"					/* room >> 1 (copy of room) */ \
		"addl   %%ebx,104(%%rsi) \n\t"		/* add (room >> 1) to check_bound */ \
		"cmpq	%%rax,%%rax \n\t"				/* force jump */ \
		"je     2f \n\t"						/* jump out of CHECK_NEW_SLICE */ \
		"4:		\n\t"							\
			/* now we are inside the (room < 2) block */ \
		"xorq	%%rax, %%rax \n\t" \
		"movl   %%r15d,%%eax \n\t"				/* copy j to scratch reg */ \
		"shll   $0x4,%%eax \n\t"				/* multiply by 16 bytes per j */ \
		"xorq	%%rdx, %%rdx \n\t" \
		"movl   100(%%rsi),%%edx \n\t"		/* move bound_index into rdx */ \
		"movq   64(%%rsi),%%r9 \n\t"			/* move lp_bucket_p ptr into r9 */ \
		"movq	16(%%r9),%%r8 \n\t"			/* move lp_bucket_p->logp ptr into r8 */ \
		"movq	56(%%rsi),%%r14 \n\t"			/* move updata_data.logp pointer into r14 */ \
		"movzbl (%%r14,%%r15,1),%%ebx \n\t"		/* bring in logp */ \
		"movb	%%bl, 108(%%rsi) \n\t"		/* shove logp into output */ \
		"movb   %%bl,(%%r8,%%rdx,1) \n\t"		/* mov logp into lp_bucket_p->logp[bound_index] */ \
		"incq   %%rdx \n\t"						/* increment bound_index locally */ \
		"movl   %%edx,100(%%rsi) \n\t"		/* copy bound_index back to structure */ \
		"movq	8(%%r9),%%r8 \n\t"			/* move lp_bucket_p->fb_bounds ptr into r8 */ \
		"movl   %%r15d,(%%r8,%%rdx,4) \n\t"		/* mov j into lp_bucket_p->fb_bounds[bound_index] */ \
			/* note this is the counter j, not the byte offset j */ \
		"movl   %%r15d,96(%%rsi) \n\t"		/* bound_val = j */ \
            "movq	120(%%rsi),%%rdi \n\t"			/* edi = polyscratch */ \
            "vmovdqa     (%%rdi), %%ymm15 \n\t" /* (j - bound_val) == 0, so just move in the lane offset */ \
            "vpslld	$16, %%ymm15, %%ymm15 \n\t"	/* put (j - bound_val + i) into high half of 32-bit words */ \
		"xorq	%%rbx, %%rbx \n\t" \
		"movl   92(%%rsi),%%ebx \n\t"			/* put numblocks into ebx */ \
		"shll	$2,%%ebx \n\t"					/* numblocks * 4 (bytes) */ \
		"shll	$1,%%ebx \n\t"					/* numblocks << 1 */ \
		"addq   %%rbx,8(%%rsi) \n\t"			/* numptr_p += (numblocks << 1) */ \
		"addq   %%rbx,0(%%rsi) \n\t"			/* numptr_n += (numblocks << 1) */ \
		"shll   $" BUCKET_BITStxt ",%%ebx \n\t"	/* numblocks << (BUCKET_BITS + 1) */ \
			/* note also, this works because we've already left shifted by 1 */ \
		"addq   %%rbx,24(%%rsi) \n\t"			/* sliceptr_p += (numblocks << 11) */ \
		"addq   %%rbx,16(%%rsi) \n\t"			/* sliceptr_n += (numblocks << 11) */ \
		"addl   $" HALFBUCKET_ALLOCtxt ",104(%%rsi) \n\t"		/* add 2^(BUCKET_BITS-1) to check_bound */ \
		"2:		\n\t"


//this is in the poly library, even though the bulk of the time is spent
//bucketizing large primes, because it's where the roots of a poly are updated
#endif

// this code is portable between compilers, but needs these instruction sets.
#if defined( USE_AVX2 ) && defined(USE_BMI2)

__m256i _mm256_mask_add_op2_epi32(__m256i m, __m256i a, __m256i b) {
    return _mm256_add_epi32(a, _mm256_and_si256(m, b));
}

__m256i _mm256_mask_sub_op2_epi32(__m256i m, __m256i a, __m256i b) {
    return _mm256_sub_epi32(a, _mm256_and_si256(m, b));
}

__m256i _emu_mm256_cmplt_epi32(__m256i a, __m256i b) {
    __m256i m = _mm256_cmpgt_epi32(b, a);
    m = _mm256_andnot_si256(_mm256_cmpeq_epi32(a, b), m);

    return m;
}


#endif

// requires compiling with avx2 support.  includes macros that are compatible
// with gcc or msvc compilers (possibly others, via intrinsics).
#if defined( USE_AVX2 )

void nextRoots_32k_avx2_small(static_conf_t *sconf, dynamic_conf_t *dconf)
{
    //update the roots 
    sieve_fb_compressed *fb_p = dconf->comp_sieve_p;
    sieve_fb_compressed *fb_n = dconf->comp_sieve_n;
    int *rootupdates = dconf->rootupdates;

    update_t update_data = dconf->update_data;

    uint32_t startprime = 2;
    uint32_t bound = sconf->factor_base->B;

    char v = dconf->curr_poly->nu[dconf->numB];
    char sign = dconf->curr_poly->gray[dconf->numB];
    int *ptr;
    uint16_t *sm_ptr;

    uint32_t med_B = sconf->factor_base->med_B;

    uint32_t j;
    int k;
    uint32_t root1, root2, prime;
    uint8_t logp = 0;

    CLEAN_AVX2;

    k = 0;
    ptr = &rootupdates[(v - 1) * bound + startprime];

    if (sign > 0)
    {

        for (j = startprime; j<sconf->sieve_small_fb_start; j++, ptr++)
        {
            prime = update_data.prime[j];
            root1 = update_data.firstroots1[j];
            root2 = update_data.firstroots2[j];

            COMPUTE_NEXT_ROOTS_P;

            //we don't sieve these, so ordering doesn't matter
            update_data.firstroots1[j] = root1;
            update_data.firstroots2[j] = root2;

            fb_p->root1[j] = (uint16_t)root1;
            fb_p->root2[j] = (uint16_t)root2;
            fb_n->root1[j] = (uint16_t)(prime - root2);
            fb_n->root2[j] = (uint16_t)(prime - root1);
            if (fb_n->root1[j] == prime)
                fb_n->root1[j] = 0;
            if (fb_n->root2[j] == prime)
                fb_n->root2[j] = 0;

        }

        // do one at a time up to the 10bit boundary, where
        // we can start doing things 16 at a time and be
        // sure we can use aligned moves (static_data_init).		
        for (j = sconf->sieve_small_fb_start;
            j < sconf->factor_base->fb_10bit_B; j++, ptr++)
        {
            // as soon as we are aligned, use more efficient AVX2 based methods...
            if ((j & 15) == 0)
                break;

            prime = update_data.prime[j];
            root1 = (uint32_t)update_data.sm_firstroots1[j];
            root2 = (uint32_t)update_data.sm_firstroots2[j];

            COMPUTE_NEXT_ROOTS_P;

            if (root2 < root1)
            {
                update_data.sm_firstroots1[j] = (uint16_t)root2;
                update_data.sm_firstroots2[j] = (uint16_t)root1;

                fb_p->root1[j] = (uint16_t)root2;
                fb_p->root2[j] = (uint16_t)root1;
                fb_n->root1[j] = (uint16_t)(prime - root1);
                fb_n->root2[j] = (uint16_t)(prime - root2);
            }
            else
            {
                update_data.sm_firstroots1[j] = (uint16_t)root1;
                update_data.sm_firstroots2[j] = (uint16_t)root2;

                fb_p->root1[j] = (uint16_t)root1;
                fb_p->root2[j] = (uint16_t)root2;
                fb_n->root1[j] = (uint16_t)(prime - root2);
                fb_n->root2[j] = (uint16_t)(prime - root1);
            }
        }

        // update 16 at a time using AVX2
        sm_ptr = &dconf->sm_rootupdates[(v - 1) * med_B];
        
        {
            small_update_t h;
        
            h.first_r1 = update_data.sm_firstroots1;		// 0
            h.first_r2 = update_data.sm_firstroots2;		// 8
            h.fbp1 = fb_p->root1;							// 16
            h.fbp2 = fb_p->root2;							// 24
            h.fbn1 = fb_n->root1;							// 32
            h.fbn2 = fb_n->root2;							// 40
            h.primes = fb_p->prime;							// 48
            h.updates = sm_ptr;								// 56
            h.start = j;									// 64
            h.stop = sconf->factor_base->med_B;		// 68

            COMPUTE_16X_SMALL_PROOTS_AVX2;
        
            j = h.stop;
        }
    }
    else
    {
        /////////////////////////////////////////////////////////////////////////////////
        // sign < 0
        /////////////////////////////////////////////////////////////////////////////////

        for (j = startprime; j<sconf->sieve_small_fb_start; j++, ptr++)
        {
            prime = update_data.prime[j];
            root1 = update_data.firstroots1[j];
            root2 = update_data.firstroots2[j];

            COMPUTE_NEXT_ROOTS_N;

            //we don't sieve these, so ordering doesn't matter
            update_data.firstroots1[j] = root1;
            update_data.firstroots2[j] = root2;

            fb_p->root1[j] = (uint16_t)root1;
            fb_p->root2[j] = (uint16_t)root2;
            fb_n->root1[j] = (uint16_t)(prime - root2);
            fb_n->root2[j] = (uint16_t)(prime - root1);
            if (fb_n->root1[j] == prime)
                fb_n->root1[j] = 0;
            if (fb_n->root2[j] == prime)
                fb_n->root2[j] = 0;

        }

        // do one at a time up to the 10bit boundary, where
        // we can start doing things 16 at a time and be
        // sure we can use aligned moves (static_data_init).	
        for (j = sconf->sieve_small_fb_start;
            j < sconf->factor_base->fb_10bit_B; j++, ptr++)
        {

            // as soon as we are aligned, use more efficient AVX2 based methods...
            if ((j & 15) == 0)
                break;

            prime = update_data.prime[j];
            root1 = (uint32_t)update_data.sm_firstroots1[j];
            root2 = (uint32_t)update_data.sm_firstroots2[j];

            COMPUTE_NEXT_ROOTS_N;

            if (root2 < root1)
            {
                update_data.sm_firstroots1[j] = (uint16_t)root2;
                update_data.sm_firstroots2[j] = (uint16_t)root1;

                fb_p->root1[j] = (uint16_t)root2;
                fb_p->root2[j] = (uint16_t)root1;
                fb_n->root1[j] = (uint16_t)(prime - root1);
                fb_n->root2[j] = (uint16_t)(prime - root2);
            }
            else
            {
                update_data.sm_firstroots1[j] = (uint16_t)root1;
                update_data.sm_firstroots2[j] = (uint16_t)root2;

                fb_p->root1[j] = (uint16_t)root1;
                fb_p->root2[j] = (uint16_t)root2;
                fb_n->root1[j] = (uint16_t)(prime - root2);
                fb_n->root2[j] = (uint16_t)(prime - root1);
            }
        }

        // update 16 at a time using AVX2 and no branching		
        sm_ptr = &dconf->sm_rootupdates[(v - 1) * med_B];
        {
            small_update_t h;
        
            h.first_r1 = update_data.sm_firstroots1;		// 0
            h.first_r2 = update_data.sm_firstroots2;		// 8
            h.fbp1 = fb_p->root1;							// 16
            h.fbp2 = fb_p->root2;							// 24
            h.fbn1 = fb_n->root1;							// 32
            h.fbn2 = fb_n->root2;							// 40
            h.primes = fb_p->prime;							// 48
            h.updates = sm_ptr;								// 56
            h.start = j;									// 64
            h.stop = sconf->factor_base->med_B;		// 68

            COMPUTE_16X_SMALL_NROOTS_AVX2;
        
            j = h.stop;
        }
    }

    CLEAN_AVX2;

    return;
}

#endif // USE_AVX2
