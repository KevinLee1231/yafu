

void adjust_mpz_bufsize(mpz_t**x,size_t*alloc_ptr,size_t size,size_t increment);
int string2mpz(mpz_t rop,char*x,int base);
#if __GNU_MP_VERSION < 3 || (__GNU_MP_VERSION == 3 && __GNU_MP_VERSION_MINOR == 0)




#endif

#define mpz_add_si(r,o,s) \
  ({ int _s; _s= (s); \
     _s>=0 ? mpz_add_ui(r,o,(mp_limb_t)(_s)) : mpz_sub_ui(r,o,(mp_limb_t)(-_s)); })

#define mpz_mul_ull mpz_mul_ui
#define mpz_mul_sll mpz_mul_si
#define mpz_fits_sllong_p mpz_fits_slong_p
#define mpz_fits_ullong_p mpz_fits_ulong_p
#define mpz_get_sll mpz_get_si
#define mpz_set_sll mpz_set_si
#define mpz_get_ull mpz_get_ui
#define mpz_set_ull mpz_set_ui
#define mpz_add_ull mpz_add_ui
#define mpz_tdiv_q_ull mpz_tdiv_q_ui
/*:1*/