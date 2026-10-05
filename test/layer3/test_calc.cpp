/* 计算器的边界、参数作用域和错误恢复回归测试。 */
#include "testkit.h"
#include "calc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* 直接检查解析器的错误返回，不把旧的 ans 当成新结果。 */
extern int calc(str_t *in, meta_t *metadata);

static void expect_calc(tk_ctx *tk, meta_t *meta, const char *input,
                        const char *expected)
{
    str_t str;
    int status;
    sInit(&str);
    toStr((char *)input, &str);
    status = calc(&str, meta);
    if (expected != NULL) {
        TK_CHECKF(tk, status == 0, "rejected expression: %s", input);
        TK_CHECKF(tk, strcmp(str.s, expected) == 0,
                  "%s: got %s, expected %s", input, str.s, expected);
    } else {
        TK_CHECKF(tk, status != 0, "accepted invalid expression: %s", input);
    }
    sFree(&str);
}

static void t_string_boundary(tk_ctx *tk)
{
    str_t str;
    char text[GSTR_MAXSIZE + 1];
    memset(text, '7', GSTR_MAXSIZE);
    text[GSTR_MAXSIZE] = '\0';
    sInit(&str);
    toStr(text, &str);
    TK_CHECK(tk, str.alloc >= GSTR_MAXSIZE + 1);
    TK_CHECK(tk, str.nchars == GSTR_MAXSIZE + 1);
    TK_CHECK(tk, strcmp(str.s, text) == 0);
    sFree(&str);
}

static void t_stack_growth(tk_ctx *tk)
{
    int type, i, round;
    for (type = 0; type <= 1; type++) {
        bstack_t stack;
        str_t str;
        char text[32];
        sInit(&str);
        stack_init(2, &stack, type);
        for (round = 0; round < 3; round++) {
            for (i = 0; i < 65; i++) {
                snprintf(text, sizeof text, "%d", i);
                toStr(text, &str);
                push(&str, &stack);
            }
            for (i = 0; i < 65; i++) {
                TK_CHECK(tk, pop(&str, &stack) == 1);
                snprintf(text, sizeof text, "%d", type ? i : 64 - i);
                TK_CHECK(tk, strcmp(str.s, text) == 0);
            }
            TK_CHECK(tk, pop(&str, &stack) == 0);
        }
        stack_free(&stack);
        sFree(&str);
    }
}

static void t_long_expression(tk_ctx *tk)
{
    meta_t meta = {0};
    char input[2400];
    int i;
    calc_init(1);
    for (i = 0; i < 600; i++) {
        input[2 * i] = '1';
        input[2 * i + 1] = '+';
    }
    input[1199] = '\0';
    expect_calc(tk, &meta, input, "600");
    /* 扩容后遇到非法字符也必须释放当前分配的数组。 */
    input[1199] = '@';
    input[1200] = '\0';
    expect_calc(tk, &meta, input, NULL);
    expect_calc(tk, &meta, "2+3", "5");
    calc_finalize();
}

static void t_nested_expression(tk_ctx *tk)
{
    meta_t meta = {0};
    char input[260];
    memset(input, '(', 100);
    input[100] = '7';
    memset(input + 101, ')', 100);
    input[201] = '\0';
    calc_init(1);
    expect_calc(tk, &meta, input, "7");
    expect_calc(tk, &meta, "-(5)", "-5");
    expect_calc(tk, &meta, "1+-(5)", "-4");
    expect_calc(tk, &meta, "-(-5)", "5");
    expect_calc(tk, &meta, "1--(5)", "6");
    expect_calc(tk, &meta, "3*-(2+1)", "-9");
    expect_calc(tk, &meta, "-(2)^2", "-4");
    expect_calc(tk, &meta, "2^3^2", "512");
    expect_calc(tk, &meta, "(8>>1)+1", "5");
    expect_calc(tk, &meta, "8>>(1)+1", "2");
    expect_calc(tk, &meta, "gcd(8,6)+gcd(9,6)", "5");
    expect_calc(tk, &meta, "gcd(8,gcd(9,6))", "1");
    calc_finalize();
}

static void t_invalid_expression(tk_ctx *tk)
{
    static const char *invalid[] = {
        "(1+2", "1+2)", "gcd(1)", "1+gcd(2)", "gcd(1,2,3)",
        "sqrt()", "1+", "gcd(,2)", "gcd(2,)", "(1,2)", "2(3)",
        "1/0", "1%0", "sqrt(-1)", "nroot(16,0)", "nroot(-16,2)",
        "modexp(2,3,0)", "modexp(2,-1,4)", "modinv(2,4)",
        "2^-1", "nroot(16,-2)", "nroot(16,18446744073709551616)",
        "totient(0)", "totient(-1)", "facm(8,0)", "facm(-1,2)",
        "fib(-1)", "luc(-1)", "rand(-1)", "randb(-1)", "randp(-1)",
        "fac2(-1)", "binom(8,-1)", "jacobi(2,4)", "jacobi(2,-3)",
        "1<<-1", "1>>18446744073709551616", "shift(1,18446744073709551616)",
        "redc(1,0,8)", "redc(1,2,8)", "redc(1,3,0)", "redc(1,17,4)",
        "redc(100,3,4)", "rsa(0)", "rsa(64)", "rsa(4097)", "llt(1)",
        "primes(20,10)", "primes(-1,10)", "primes(0,10,2)",
        "bigprimes(10,5,7)", "bigprimes(1,10,0)", "sigma(0,1)",
        "sigma(5,-1)", "divisors(0)", "semiprimes(1,1)", "toom3(1,2,0)",
        "fftmul(1,2,0,0)"
    };
    meta_t meta = {0};
    size_t i;
    calc_init(1);
    for (i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        expect_calc(tk, &meta, invalid[i], NULL);
        expect_calc(tk, &meta, "6*7", "42");
    }
    expect_calc(tk, &meta, "nroot(-27,3)", "-3");
    expect_calc(tk, &meta, "modexp(2,-1,5)", "3");
    expect_calc(tk, &meta, "modinv(3,7)", "5");
    expect_calc(tk, &meta, "facm(8,3)", "80");
    expect_calc(tk, &meta, "shift(7,-3)", "0");
    expect_calc(tk, &meta, "redc(10,7,4)", "5");
    expect_calc(tk, &meta, "0b10+0o10+0d10+0x10", "36");
    expect_calc(tk, &meta, "08", "8");
    expect_calc(tk, &meta, "123abc+1", NULL);
#if ULONG_MAX > 4294967295UL
    expect_calc(tk, &meta, "shift(7,-9223372036854775808)", "0");
    expect_calc(tk, &meta, "popcnt(-1)", "18446744073709551615");
    expect_calc(tk, &meta, "hamdist(0,-1)", "18446744073709551615");
#endif
    calc_finalize();
}

static void t_preprocessor_status(tk_ctx *tk)
{
    meta_t meta = {0};
    char *result;
    char input[2200];
    size_t i;
    calc_init(1);
    result = process_expression("{ ans = 40, ans=ans+2 }", &meta, 1, 0);
    TK_CHECK(tk, result != NULL && strcmp(result, "42") == 0);
    free(result);
    result = process_expression("ans=41,1/0,ans=99", &meta, 1, 0);
    TK_CHECK(tk, result == NULL);
    free(result);
    expect_calc(tk, &meta, "ans", "41");
    result = process_expression("OBASE=3", &meta, 1, 0);
    TK_CHECK(tk, result == NULL);
    free(result);
    expect_calc(tk, &meta, "OBASE", "10");
    result = process_expression("IBASE=16", &meta, 1, 0);
    TK_CHECK(tk, result != NULL && strcmp(result, "16") == 0);
    free(result);
    expect_calc(tk, &meta, "A+10", "26");
    result = process_expression("IBASE=0d10", &meta, 1, 0);
    TK_CHECK(tk, result != NULL && strcmp(result, "10") == 0);
    free(result);
    result = process_expression("OBASE=16", &meta, 1, 0);
    TK_CHECK(tk, result != NULL && strcmp(result, "10") == 0);
    free(result);
    result = process_expression("OBASE=10", &meta, 1, 0);
    TK_CHECK(tk, result != NULL && strcmp(result, "10") == 0);
    free(result);
    for (i = 0; i < 700; i++) {
        input[3*i] = '1';
        input[3*i+1] = ';';
        input[3*i+2] = ',';
    }
    input[2100] = '\0';
    result = process_expression(input, &meta, 1, 0);
    TK_CHECK(tk, result != NULL && strcmp(result, "1") == 0);
    free(result);
    result = process_expression("if(1,2,3)", &meta, 1, 0);
    TK_CHECK(tk, result == NULL);
    free(result);
    calc_finalize();
}

static void t_assignment_boundary(tk_ctx *tk)
{
    meta_t meta = {0};
    char input[160];
    char *result;
    calc_init(1);
    result = process_expression("ans=42", &meta, -1, 0);
    TK_CHECK(tk, strcmp(result, "42") == 0);
    free(result);
    memset(input, 'a', 120);
    strcpy(input + 120, "=7");
    result = process_expression(input, &meta, -1, 0);
    TK_CHECK(tk, result == NULL);
    free(result);
    result = process_expression("{}", &meta, -1, 0);
    TK_CHECK(tk, strcmp(result, "42") == 0);
    free(result);
    memset(input, 'a', 39);
    strcpy(input + 39, "=9");
    result = process_expression(input, &meta, -1, 0);
    TK_CHECK(tk, strcmp(result, "9") == 0);
    free(result);
    input[39] = '\0';
    expect_calc(tk, &meta, input, "9");
    calc_finalize();
}

static unsigned long gcd_ref(unsigned long a, unsigned long b)
{
    while (b != 0) {
        unsigned long r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static void t_factor_state(tk_ctx *tk)
{
    fact_obj_t src, dest;
    int parameters_only;
    init_factobj(&src);
    init_factobj(&dest);
    src.autofact_obj.max_siqs = 77;
    src.autofact_obj.max_nfs = 101;
    for (parameters_only = 0; parameters_only <= 1; parameters_only++) {
        copy_factobj(&dest, &src, parameters_only);
        TK_CHECK(tk, dest.autofact_obj.max_siqs == 77);
        TK_CHECK(tk, dest.autofact_obj.max_nfs == 101);
    }
    free_factobj(&dest);
    free_factobj(&src);
}

static void t_factor_functions(tk_ctx *tk)
{
    meta_t meta = {0};
    fact_obj_t fobj;
    unsigned long n, k;
    char input[64], expected[64];
    fobj.argc = 99;
    init_factobj(&fobj);
    TK_CHECK(tk, fobj.argc == 0);
    TK_CHECK(tk, fobj.argv == NULL);
    TK_CHECK(tk, fobj.input_str[0] == '\0');
    fobj.VFLAG = -1;
    fobj.LOGFLAG = 0;
    meta.fobj = &fobj;
    calc_init(1);
    expect_calc(tk, &meta, "1+trial(15)", "2");
    expect_calc(tk, &meta, "gcd(12,trial(15)+5)", "6");
    expect_calc(tk, &meta, "primes(10,30)", "6");
    expect_calc(tk, &meta, "primes(10,30,1)", "6");
    expect_calc(tk, &meta, "primes(10,30,0)", "6");
    expect_calc(tk, &meta, "primes(14,16,0)", "0");
    expect_calc(tk, &meta, "llt(2)", "1");
    for (n = 1; n <= 50; n++) {
        unsigned long phi = 0;
        unsigned long sigma = 0;
        unsigned long divisors = 0;
        for (k = 1; k <= n; k++)
        {
            phi += gcd_ref(n, k) == 1;
            if (n % k == 0) { sigma += k; divisors++; }
        }
        snprintf(input, sizeof input, "totient(%lu)", n);
        snprintf(expected, sizeof expected, "%lu", phi);
        expect_calc(tk, &meta, input, expected);
        snprintf(input, sizeof input, "sigma(%lu,1)", n);
        snprintf(expected, sizeof expected, "%lu", sigma);
        expect_calc(tk, &meta, input, expected);
        snprintf(input, sizeof input, "sigma(%lu,0)", n);
        snprintf(expected, sizeof expected, "%lu", divisors);
        expect_calc(tk, &meta, input, expected);
    }
    calc_finalize();
    free_factobj(&fobj);
}

static void t_factor_list_copy(tk_ctx *tk)
{
    fact_obj_t src, dest;
    yfactor_list_t fresh;
    mpz_t n, prod;
    unsigned long p;
    init_factobj(&src);
    init_factobj(&dest);
    mpz_inits(n, prod, NULL);
    mpz_set_ui(n, 30);
    new_factorization(&src, n);
    for (p = 2; p <= 3; p++) {
        mpz_set_ui(n, p);
        add_to_factor_list(src.factors, n, -1, 1, 0);
    }
    generate_factorization_str(src.factors);
    copy_factor_list(dest.factors, src.factors, 0);
    TK_CHECK(tk, dest.factors->num_factors == 3);
    TK_CHECK(tk, dest.factors->alloc_factors >= 3);
    TK_CHECK(tk, strcmp(dest.factors->factorization_str,
                        src.factors->factorization_str) == 0);
    TK_CHECK(tk, dest.factors->factorization_str != src.factors->factorization_str);
    get_prod_of_factors(dest.factors, prod);
    TK_CHECK(tk, mpz_cmp_ui(prod, 30) == 0);
    copy_factor_list(dest.factors, dest.factors, 0);
    get_prod_of_factors(dest.factors, prod);
    TK_CHECK(tk, mpz_cmp_ui(prod, 30) == 0);

    /* 从较大的已分解列表复制到小列表，再复制空列表。 */
    mpz_set_ui(n, 7);
    new_factorization(&src, n);
    copy_factor_list(dest.factors, src.factors, 0);
    TK_CHECK(tk, dest.factors->num_factors == 1);
    TK_CHECK(tk, dest.factors->factorization_str == NULL);
    get_prod_of_factors(dest.factors, prod);
    TK_CHECK(tk, mpz_cmp_ui(prod, 7) == 0);
    mpz_set_ui(n, 1);
    new_factorization(&src, n);
    generate_factorization_str(src.factors);
    copy_factor_list(&fresh, src.factors, 1);
    TK_CHECK(tk, fresh.num_factors == 0);
    TK_CHECK(tk, fresh.factorization_str != NULL && fresh.factorization_str[0] == '\0');
    TK_CHECK(tk, fresh.str_alloc >= 1);
    clear_factor_list(&fresh);
    mpz_clears(n, prod, NULL);
    free_factobj(&dest);
    free_factobj(&src);
}

static const tk_test tests[] = {
    {"string_boundary", t_string_boundary, "fast calc-string"},
    {"stack_growth", t_stack_growth, "fast calc-stack"},
    {"long_expression", t_long_expression, "fast calc-long"},
    {"nested_expression", t_nested_expression, "fast calc-nested"},
    {"invalid_expression", t_invalid_expression, "fast calc-invalid"},
    {"preprocessor_status", t_preprocessor_status, "fast calc-preprocessor"},
    {"assignment_boundary", t_assignment_boundary, "fast calc-assignment"},
    {"factor_state", t_factor_state, "fast calc-state"},
    {"factor_list_copy", t_factor_list_copy, "fast calc-copy"},
    {"factor_functions", t_factor_functions, "fast calc-factor"}
};

const tk_module tk_module_calc = {
    "calc", "calculator memory boundaries, argument scopes and error recovery",
    tests, (int)(sizeof tests / sizeof tests[0])
};
