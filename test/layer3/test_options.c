/* 命令行参数和配置文件的边界回归。 */
#define _POSIX_C_SOURCE 200809L
#include "testkit.h"
#include "cmdOptions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__)
#include <unistd.h>
#include <sys/wait.h>
#endif

static void free_options(options_t *options)
{
    int i;
    for (i = 0; i < options->num_tune_info; i++)
        free(options->tune_info[i]);
    free(options->tune_info);
    free(options->inputExpr);
    free(options);
}

static void t_defaults(tk_ctx *tk)
{
    options_t *o = initOpt();
    TK_EQ_U64(tk, o->threads, 1);
    TK_EQ_U64(tk, o->gpucurves, 0);
    TK_EQ_U64(tk, o->use_cgbn, 0);
    TK_EQ_U64(tk, o->use_gpuecm, 0);
    TK_EQ_U64(tk, o->use_gpudev, 0);
    TK_EQ_U64(tk, o->cadoMsieve, 0);
    TK_CHECK(tk, o->convert_poly_path[0] == '\0');
    TK_CHECK(tk, strcmp(o->cado_dir, o->ggnfs_dir) == 0);
    free_options(o);
}

static void t_numbers(tk_ctx *tk)
{
    options_t *o = initOpt();
    char bound[] = "1e9";
    applyOpt("B1ecm", bound, o);
    TK_EQ_U64(tk, o->B1ecm, 1000000000);
    TK_CHECK(tk, strcmp(bound, "1e9") == 0);
    applyOpt("B1ecm", "1.25e3", o);
    TK_EQ_U64(tk, o->B1ecm, 1250);
    applyOpt("B1ecm", "18446744073709551615", o);
    TK_EQ_U64(tk, o->B1ecm, UINT64_MAX);
    applyOpt("B2pm1", "18446744073709551615", o);
    TK_EQ_U64(tk, o->B2pm1, UINT64_MAX);
    applyOpt("rhomax", "4294967295", o);
    TK_EQ_U64(tk, o->rhomax, UINT32_MAX);
    applyOpt("ns", "123,456", o);
    TK_EQ_U64(tk, o->sieveQstart, 123);
    TK_EQ_U64(tk, o->sieveQstop, 456);
    applyOpt("ns", "123", o);
    TK_EQ_U64(tk, o->sieveQstop, 0);
    applyOpt("np", "4294967294,4294967295", o);
    TK_EQ_U64(tk, o->polystop, UINT32_MAX);
    applyOpt("work", "12.5", o);
    TK_CHECK(tk, o->work == 12.5);
    free_options(o);
}

static void t_arguments(tk_ctx *tk)
{
    options_t *o = initOpt();
    char *args[] = { "yafu", "-e", "expr(6*7)", "-max_siqs", "-1" };
    TK_EQ_U64(tk, processOpts(5, args, o), 2);
    TK_CHECK(tk, strcmp(o->inputExpr, "expr(6*7)") == 0);
    TK_CHECK(tk, o->max_siqs == -1);
    free_options(o);
    o = initOpt();
    {
        char *literal[] = { "yafu", "--", "-91" };
        processOpts(3, literal, o);
        TK_CHECK(tk, strcmp(o->inputExpr, "-91") == 0);
    }
    free_options(o);
}

#if defined(__unix__)
static void t_invalid(tk_ctx *tk)
{
    const char *cases[][2] = {
        { "threads", "0" }, { "threads", "4294967296" },
        { "threads", "" }, { "threads", NULL }, { "rhomax", "-1" },
        { "rhomax", "1junk" }, { "B1ecm", "1e1000" },
        { "B1ecm", "1e-1" }, { "B1ecm", "e10" },
        { "B1ecm", "18446744073709551616" }, { "work", "nan" },
        { "work", "inf" }, { "work", "1junk" }, { "stopbase", "1" },
        { "stopbase", "63" }, { "siqsSSalloc", "16" }, { "siqsBT", "0" },
        { "ns", "5,4" }, { "ns", "1," }, { "np", "1,2,3" },
        { "np", "1" }, { "no-such-option", NULL }
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        int status;
        pid_t child;
        fflush(NULL);
        child = fork();
        TK_REQUIRE(tk, child >= 0, "fork failed");
        if (child == 0)
        {
            options_t *o = initOpt();
            applyOpt((char *)cases[i][0], (char *)cases[i][1], o);
            free_options(o);
            _exit(0);
        }
        TK_REQUIRE(tk, waitpid(child, &status, 0) == child, "waitpid failed");
        TK_CHECKF(tk, WIFEXITED(status) && WEXITSTATUS(status) == 1,
            "%s did not reject invalid input", cases[i][0]);
    }
}

static void t_ini(tk_ctx *tk)
{
    char name[] = "/tmp/yafu-options-XXXXXX";
    int fd = mkstemp(name);
    FILE *file;
    options_t *o;
    TK_REQUIRE(tk, fd >= 0, "mkstemp failed");
    file = fdopen(fd, "w");
    TK_REQUIRE(tk, file != NULL, "fdopen failed");
    fputs("  % comment\r\n threads = 2 \r\nB1ecm=1e6\n"
          "logfile=\njsonlog=a=b.json\nkeep_afb=0\nsilent\n"
          "script=folder/a b.txt", file);
    fclose(file);
    o = initOpt();
    TK_CHECK(tk, readINI(name, o));
    TK_EQ_U64(tk, o->threads, 2);
    TK_EQ_U64(tk, o->B1ecm, 1000000);
    TK_CHECK(tk, o->factorlog[0] == '\0');
    TK_CHECK(tk, strcmp(o->jsonlog, "a=b.json") == 0);
    TK_CHECK(tk, strcmp(o->scriptfile, "folder/a b.txt") == 0);
    TK_CHECK(tk, o->keep_afb == 0);
    free_options(o);
    unlink(name);
}
#endif

static const tk_test tests[] = {
    { "defaults", t_defaults, "fast options" },
    { "numbers", t_numbers, "fast options" },
    { "arguments", t_arguments, "fast options" },
#if defined(__unix__)
    { "invalid", t_invalid, "fast options" },
    { "ini", t_ini, "fast options" },
#endif
};

const tk_module tk_module_options = {
    "options", "CLI and ini parsing", tests, sizeof(tests) / sizeof(tests[0])
};
