#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/time.h>
#include "../ytools/ytools.h"
#include "batch_factor.h"
#include "gmp.h"

// build line:
// clang -O2 -g -I. -I../../ytools/ -I../../include -I../../ms_include -I../../top/aprcl -I../../../gmp-install/6.2.0-aocc/include -L../../../gmp-install/6.2.0-aocc/lib -march=icelake-client -DUSE_AVX512F -DIFMA ../../ytools/util.c batch_factor.c process_batch.c tinyecm.c microecm.c prime_sieve.c micropm1.c -o bfact -lm -lgmp
// 
// usage: bfact lpb pmin pmax relsfilein relsfileout

#define MAX_RELATION_FACTORS 32

static int parse_u64(const char* text, uint64_t* value)
{
    char* end;
    const char* cursor;
    unsigned long long parsed;

    if (*text == '\0')
        return -1;
    for (cursor = text; *cursor != '\0'; cursor++)
        if (*cursor < '0' || *cursor > '9')
            return -1;

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
        return -1;
    *value = (uint64_t)parsed;
    return 0;
}

static int parse_factor_list(char* text, uint32_t* factors, uint32_t* count)
{
    uint32_t n = 0;

    while (*text != '\0' && *text != '\r' && *text != '\n')
    {
        char* end;
        unsigned long parsed;

        if (n == MAX_RELATION_FACTORS)
            return -1;
        errno = 0;
        parsed = strtoul(text, &end, 16);
        if (errno != 0 || end == text || parsed > UINT32_MAX)
            return -1;
        factors[n++] = (uint32_t)parsed;
        if (*end == '\0' || *end == '\r' || *end == '\n')
            break;
        if (*end != ',')
            return -1;
        text = end + 1;
    }

    *count = n;
    return 0;
}

static void write_u32_factor(FILE* out, int* first, uint32_t factor)
{
    if (factor <= 1)
        return;
    if (!*first)
        fputc(',', out);
    fprintf(out, "%" PRIx32, factor);
    *first = 0;
}

static void write_mpz_factor(FILE* out, int* first, const mpz_t factor)
{
    if (mpz_cmp_ui(factor, 1) <= 0)
        return;
    if (!*first)
        fputc(',', out);
    gmp_fprintf(out, "%Zx", factor);
    *first = 0;
}

int main(int argc, char** argv)
{
    relation_batch_t rb;
    uint64_t lpb, pmin_arg, pmax;
    uint32_t pmin;
    const char* infile;
    const char* outfile;
    char buf[1024];
    uint32_t fr[32], fa[32], numr = 0, numa = 0;
    mpz_t res1, res2;
    struct timeval start;
    struct timeval stop;
    double ttime;
    uint64_t lcg_state = 42;
    uint32_t i;
    uint32_t line = 0;
    uint32_t numfull = 0;

    if (argc != 6)
    {
        fprintf(stderr, "usage: %s lpb pmin pmax relsfilein relsfileout\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (parse_u64(argv[1], &lpb) != 0 || parse_u64(argv[2], &pmin_arg) != 0 ||
        parse_u64(argv[3], &pmax) != 0)
    {
        fprintf(stderr, "lpb, pmin, and pmax must be unsigned decimal integers\n");
        return EXIT_FAILURE;
    }
    if (pmin_arg > UINT32_MAX)
    {
        fprintf(stderr, "pmin must fit in 32 bits\n");
        return EXIT_FAILURE;
    }
    pmin = (uint32_t)pmin_arg;
    infile = argv[4];
    outfile = argv[5];

    if (lpb > 40)
    {
        printf("expected large prime bound at most 40 bits, saw %" PRIu64 "\n", lpb);
        return EXIT_FAILURE;
    }

    lpb = 1ull << lpb;

    for (i = 0; i < 4; i++)
    {
        rb.num_uecm[i] = 0;
        rb.num_uecm_a[i] = 0;
    }
    rb.num_tecm = 0;
    rb.num_tecm2 = 0;
    rb.num_qs = 0;
    rb.num_tecm_a = 0;
    rb.num_tecm2_a = 0;
    rb.num_qs_a = 0;
    rb.num_attempt = 0;
    rb.num_success = 0;
    for (i = 0; i < 8; i++)
    {
        rb.num_abort[i] = 0;
        rb.num_abort_a[i] = 0;
    }
    mpz_init(res1);
    mpz_init(res2);

    printf("initializing relation batch...\n");

    gettimeofday(&start, NULL);
    relation_batch_init(stdout, &rb, pmin, pmax, lpb, lpb, NULL, 1);
    gettimeofday(&stop, NULL);
    ttime = ytools_difftime(&start, &stop);

    printf("init took %1.2f sec, reading input file...\n", ttime);

    if (strcmp(infile, outfile) == 0)
    {
        fprintf(stderr, "input and output files must be different\n");
        relation_batch_free(&rb);
        mpz_clear(res1);
        mpz_clear(res2);
        return EXIT_FAILURE;
    }

    FILE* fid = fopen(infile, "r");
    if (fid == NULL)
    {
        printf("could not open %s to read\n", infile);
        relation_batch_free(&rb);
        mpz_clear(res1);
        mpz_clear(res2);
        return EXIT_FAILURE;
    }

    FILE* fout = fopen(outfile, "w");
    if (fout == NULL)
    {
        fprintf(stderr, "could not open %s to write\n", outfile);
        fclose(fid);
        relation_batch_free(&rb);
        mpz_clear(res1);
        mpz_clear(res2);
        return EXIT_FAILURE;
    }

    gettimeofday(&start, NULL);

    while (fgets(buf, sizeof(buf), fid) != NULL)
    {
        int64_t a;
        uint32_t b;
        int parsed_chars;
        char* tok;

        line++;
        char* ptr;

        if (strchr(buf, '\n') == NULL)
        {
            int ch = fgetc(fid);
            if (ch != '\n' && ch != EOF)
            {
                while ((ch = fgetc(fid)) != '\n' && ch != EOF)
                    ;
                printf("could not read relation %u, line exceeds input buffer\n", line);
                continue;
            }
        }

        tok = strtok(buf, ":");
        if (tok == NULL)
        {
            printf("could not read relation %u, no lfactors token\n", line);
            continue;
        }

        ptr = strchr(tok, ',');
        if (ptr == NULL)
        {
            printf("could not read relation %u, invalid lfactors token\n", line);
            continue;
        }
        *ptr = '\0';
        if (mpz_set_str(res1, tok, 10) != 0 || mpz_set_str(res2, ptr + 1, 10) != 0)
        {
            printf("could not read relation %u, invalid large cofactor\n", line);
            continue;
        }

        // gmp_printf("large factors: %Zd, %Zd\n", res1, res2);

        tok = strtok(NULL, ":");
        if (tok == NULL)
        {
            printf("could not read relation %u, no a/b token\n", line);
            continue;
        }

        if (sscanf(tok, "%" SCNd64 ",%" SCNu32 "%n", &a, &b, &parsed_chars) != 2 ||
            tok[parsed_chars] != '\0')
        {
            printf("could not read relation %u, invalid a/b token\n", line);
            continue;
        }

        tok = strtok(NULL, ":");
        if (tok == NULL)
        {
            printf("could not read relation %u, no rfactors token\n", line);
            continue;
        }
        if (parse_factor_list(tok, fr, &numr) != 0)
        {
            printf("could not read relation %u, invalid or oversized rfactors token\n", line);
            continue;
        }

        tok = strtok(NULL, ":");
        if (tok == NULL)
        {
            printf("could not read relation %u, no afactors token\n", line);
            continue;
        }

        if (parse_factor_list(tok, fa, &numa) != 0)
        {
            printf("could not read relation %u, invalid or oversized afactors token\n", line);
            continue;
        }

        if ((mpz_sgn(res1) > 0) && (mpz_sgn(res2) > 0))
        {
            int first = 1;

            numfull++;
            fprintf(fout, "%" PRId64 ",%" PRIu32 ":", a, b);
            write_mpz_factor(fout, &first, res1);
            for (i = 0; i < numr; i++)
                write_u32_factor(fout, &first, fr[i]);
            fputc(':', fout);
            first = 1;
            write_mpz_factor(fout, &first, res2);
            for (i = 0; i < numa; i++)
                write_u32_factor(fout, &first, fa[i]);
            fputc('\n', fout);
        }
        else
        {
            relation_batch_add(a, b, fr, numr, res1, fa, numa, res2, &rb);
        }
    }
    if (ferror(fid))
    {
        fprintf(stderr, "error while reading %s\n", infile);
        fclose(fid);
        fclose(fout);
        relation_batch_free(&rb);
        mpz_clear(res1);
        mpz_clear(res2);
        return EXIT_FAILURE;
    }
    fclose(fid);

    gettimeofday(&stop, NULL);
    ttime = ytools_difftime(&start, &stop);
    printf("file parsing took %1.2f sec, found %d fulls, batched %u rels, now running batch solve...\n", 
        ttime, numfull, rb.num_relations);

    gettimeofday(&start, NULL);
    relation_batch_run(&rb, &lcg_state);
    gettimeofday(&stop, NULL);

    ttime = ytools_difftime(&start, &stop);
    printf("relation_batch_run took %1.4f sec producing %u relations\n",
        ttime, rb.num_success);

    {
        int nwrote = 0;
        line = 0;
        for (i = 0; i < rb.num_relations; i++)
        {
            if (rb.relations[i].success > 0)
            {
                int j, k;
                int first = 1;

                uint32_t* f = rb.factors + rb.relations[i].factor_list_word;

                fprintf(fout, "%" PRId64 ",%" PRIu32 ":",
                    rb.relations[i].a, rb.relations[i].b);
                for (j = 0; j < 3; j++)
                    write_u32_factor(fout, &first, rb.relations[i].lp_r[j]);
                for (k = 0; k < rb.relations[i].num_factors_r; k++)
                    write_u32_factor(fout, &first, f[k]);
                fprintf(fout, ":");
                first = 1;
                for (j = 0; j < 3; j++)
                    write_u32_factor(fout, &first, rb.relations[i].lp_a[j]);

                f = rb.factors + rb.relations[i].factor_list_word + rb.relations[i].num_factors_r;
                for (k = 0; k < rb.relations[i].num_factors_a; k++)
                    write_u32_factor(fout, &first, f[k]);
                fprintf(fout, "\n");
                nwrote++;
            }
        }
        int write_error = ferror(fout);
        if (fclose(fout) != 0)
            write_error = 1;
        if (write_error)
        {
            fprintf(stderr, "error while writing %s\n", outfile);
            relation_batch_free(&rb);
            mpz_clear(res1);
            mpz_clear(res2);
            return EXIT_FAILURE;
        }
        printf("wrote %u full and %d factored relations to %s\n",
            numfull, nwrote, outfile);
    }
    printf("ECM stats R:\n");
    for (i = 0; i < 4; i++)
    {
        printf("%u;  ", rb.num_uecm[i]);
    }
    printf("%u;  ", rb.num_tecm);
    printf("%u;  ", rb.num_tecm2);
    printf("%u;  ", rb.num_qs);
    printf("\nECM stats A:\n");
    for (i = 0; i < 4; i++)
    {
        printf("%u;  ", rb.num_uecm_a[i]);
    }
    printf("%u;  ", rb.num_tecm_a);
    printf("%u;  ", rb.num_tecm2_a);
    printf("%u;  ", rb.num_qs_a);

    printf("\nAbort stats R:\n");
    for (i = 0; i < 8; i++)
    {
        printf("%u;  ", rb.num_abort[i]);
    }
    printf("\nAbort stats A:\n");
    for (i = 0; i < 8; i++)
    {
        printf("%u;  ", rb.num_abort_a[i]);
    }
    printf("\n");

    mpz_clear(res1);
    mpz_clear(res2);
    relation_batch_free(&rb);

    return 0;
}
