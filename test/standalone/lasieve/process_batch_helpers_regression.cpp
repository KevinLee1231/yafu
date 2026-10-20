#define main process_batch_program_main
#include "factor/nfs/lasieve/process_batch.cpp"
#undef main
#include <assert.h>

/* 这个驱动链的是单份对象，lasieve_ns 解析成 lasieve_single；per-I 的
 * 名字现在都在命名空间里，所以要显式引进来。 */
using namespace lasieve_ns;
int main(void) {
    uint64_t value;
    uint32_t factors[32], count;
    char good[] = "a,10,ff\n";
    char trailing[] = "a,";
    char *out = NULL;
    size_t out_size = 0;
    FILE *stream;
    mpz_t z;
    int first = 1;
    assert(parse_u64("40", &value) == 0 && value == 40);
    assert(parse_u64("-1", &value) != 0);
    assert(parse_u64(" 1", &value) != 0);
    assert(parse_factor_list(good, factors, &count) == 0 && count == 3 && factors[2] == 255);
    assert(parse_factor_list(trailing, factors, &count) == 0 && count == 1);
    stream = open_memstream(&out, &out_size);
    assert(stream != NULL);
    mpz_init_set_ui(z, 17);
    write_mpz_factor(stream, &first, z);
    write_u32_factor(stream, &first, 19);
    assert(fclose(stream) == 0);
    assert(strcmp(out, "11,13") == 0);
    free(out);
    mpz_clear(z);

    /* 一侧最多 MAX_LARGE_PRIMES 个大素数，每一个都要写出来；
       值 1 表示这一槽没有大素数，写出时要跳过。
       大素数是 64 位的（lpb 最大 40 位），不能截成 32 位。 */
    {
        uint64_t lp[MAX_LARGE_PRIMES];
        int i;

        lp[0] = UINT64_C(0x100000001);
        lp[1] = UINT64_C(0xdeadbeef);
        lp[2] = 1;
        lp[3] = 1;

        out = NULL; out_size = 0; first = 1;
        stream = open_memstream(&out, &out_size);
        assert(stream != NULL);
        for (i = 0; i < MAX_LARGE_PRIMES; i++)
            write_u64_factor(stream, &first, lp[i]);
        assert(fclose(stream) == 0);
        assert(strcmp(out, "100000001,deadbeef") == 0);
        free(out);
    }
    return 0;
}
