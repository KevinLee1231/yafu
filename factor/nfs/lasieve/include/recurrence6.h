#include "lasieve_ns.h"
#include "siever-config.h"

#include <immintrin.h>

namespace lasieve_ns {
void rec_info_init(u32_t A, u32_t ub);
u32_t get_recurrence_info(u32_t* res_ptr, u32_t p, u32_t r, u32_t FBsize);
}  /* namespace lasieve_ns */


//ASM_ATTR

#ifdef AVX512_LASIEVE_SETUP
namespace lasieve_ns {
u32_t get_recurrence_info_16(u32_t* res_ptr, __m512i p, __m512i r, u32_t FBsize);
}  /* namespace lasieve_ns */

#endif

/*:8*/