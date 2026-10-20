/*
MIT License

Copyright (c) 2021 Ben Buhrow

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// ============================================================================
// a simple command line parser.
// supports defining required and options arguments to a program.
// supports defining command line switches with optional arguments.
//
// gcc -O2 cmdOptions.h cmdOptions.c demo.c -o demo
//
// example:
// ./demo "hello" -v -v --str "world"
// ============================================================================

#include "cmdOptions.h"
#include "ytools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <string_view>

#if defined(__INTEL_LLVM_COMPILER) || defined(__GNUC__)
#include <ctype.h>
#endif

// this function prints the help information specified by usageHelp
// and OptionHelp.
void printUsage(options_t* options);

// ========================================================================
// In this section, define the options, any long form aliases for them,
// whether they have an argument or not, and help strings to display
// if the user tries -h

// define help text for usage of the program.  
char usageHelp[MAXHELPLEN] = "usage: [optional_command_string] [options]";

// command line options visible to driver.c
char OptionArray[NUMOPTIONS][MAXOPTIONLEN] = {
    "B1pm1", "B1pp1", "B1ecm", "rhomax", "B2pm1",
    "B2pp1", "B2ecm", "qssave", "siqsB", "siqsTF",
    "siqsR", "siqsT", "siqsNB", "siqsM", "logfile",
    "batchfile", "seed", "sigma", "session", "threads",
    "v", "silent", "pfile", "pscreen", "forceDLP",
    "fmtmax", "noopt", "vproc", "noecm",
    "tune_info", "pretest_ratio", "xover", "one", "op",
    "of", "ou", "plan", "pretest", "no_expr",
    "o", "a", "r", "ggnfsT", "job",
    "ns", "np", "nc", "psearch", "R",
    "pbatch", "ecm_path", "siever", "ncr", "lathreads",
    "nc2", "nc3", "p", "work", "nprp",
    "ext_ecm", "testsieve", "nt", "aprcl_p", "aprcl_d",
    "filt_bump", "nc1", "gnfs", "e", "repeat",
    "ecmtime", "no_clk_test", "siqsTFSm", "script", "degree",
    "snfs_xover", "soe_block", "forceTLP", "siqsLPB", "siqsMFBD",
    "siqsMFBT", "siqsBDiv", "siqsBT", "prefer_gmpecm", "saveB1",
    "siqsNobat", "inmem", "prefer_gmpecm_stg2", "vpp1_work_file", "vpm1_work_file",
    "resume", "jsonpretty", "cadoMsieve", "cado_dir", "convert_poly_path",
    "gpucurves", "cgbn", "use_gpuecm", "use_gpudev", "prefer_avxecm_stg2",
    "stoplt", "stople", "stopeq", "stopgt", "stopge",
    "stopbase", "stopprime", "siqsSSidx", "siqsSSalloc", "skipSNFScheck",
    "obase", "minrels", "stopk", "stop_strict", "terse",
    "max_siqs", "max_nfs", "np1", "nps", "npr",
    "nfs_params", "poly_testsieve", "poly_percent_max", "td", "jsonlog",
    "forceQLP", "siqsMFBQ", "nfs_batch_3lp", "analysis", "keep_afb",
    "nfs_stage1_args", "cuda_sieve", "cuda_dev" };

// help strings displayed with -h
// needs to be the same length as the above arrays, even if 
// some are left blank
char OptionHelp[NUMOPTIONS][MAXHELPLEN] = {
    "(Integer < 64-bit): B1 bound of P-1 algorithm",
    "(Integer < 64-bit): B1 bound of P+1 algorithm",
    "(Integer < 64-bit): B1 bound of ECM algorithm",
    "(Integer < 32-bit): Iteration limit of Rho algorithm",
    "(Integer < 64-bit): B2 bound of P-1 algorithm",
    "(Integer < 64-bit): B2 bound of P+1 algorithm",
    "(Integer < 64-bit): B2 bound of ECM algorithm",
    "(String)          : name of SIQS save file",
    "(Integer < 32-bit): Size of SIQS factor base (number of primes)",
    "(Integer < 32-bit): SIQS trial factoring bound",
    "(Integer < 32-bit): SIQS Number of relations to gather",
    "(Integer < 32-bit): SIQS timeout in seconds",
    "(Integer < 32-bit): SIQS Number of sieve blocks",
    "(Integer < 32-bit): SIQS Large prime variation multiplier",
    "(String)          : Name of factoring logfile",
    "(String)          : Name of input batchfile",
    "(Integer < 64-bit): Seed for RNG",
    "(Integer < 64-bit): ECM sigma",
    "(String)          : Name of session logfile",
    "(Integer < 32-bit): Number of threads",
    "                  : Increase verbosity",
    "                  : Set verbosity to -1 (no output)",
    "                  : Output primes to file primes.dat",
    "                  : Output primes to screen",
    "                  : Force use of DLP variation in SIQS",
    "(Integer < 32-bit): Max iteration for Fermat factorization method",
    "                  : Do not optimize small prime variation cutoff in SIQS",
    "                  : Verbose output of processor info",
    "                  : Do not use ECM in factor()",
    "(String)          : Input tuning information for SIQS/NFS crossover",
    "(Floating point)  : ECM/NFS pretesting ratio: projected-difficulty * ratio = ECM depth",
    "(Floating point)  : SIQS/NFS crossover point (decimal digits)",
    "                  : Stop after one factor found",
    "(String)          : Filename where primes found during factor() are output",
    "(String)          : Filename where factors found during factor() are output",
    "(String)          : Filename where unfactored residues remaining after factor() are output",
    "(String)          : ECM pretesting plan (light = 2/9, deep = 1/3, normal = 4/13, none, or custom (pretest_ratio)",
    "(Integer < 32-bit): Only ECM pretest (no NFS or SIQS) to specified digit level",
    "                  : For use with op, of, or ou, to output expressions",
    "(String)          : NFS output filename",
    "                  : Algebraic side GGNFS sieving",
    "                  : Rational side GGNFS sieving",
    "(Integer < 32-bit): NFS sieving timeout in seconds",
    "(String)          : NFS input jobfile name",
    "(Start,Stop)      : Perform only sieving phase of NFS within specified 32-bit start,stop lattice Q's",
    "(Start,Stop)      : Perform only poly-search phase of NFS within specified 32-bit start,stop leading coefficients",
    "                  : Perform only post-processing phase of NFS, starting with filtering",
    "(String)          : NFS poly-search methology ('deep', 'wide', 'fast', 'min', 'avg', or 'good')",
    "                  : Resume NFS with an existing data/job file",
    "(Integer < 32-bit): Range of leading coefficients to distribute to threads during NFS poly-search",
    "(String)          : Path of GMP-ECM executable",
    "(Integer < 32-bit): Version of GGNFS to use (11,12,13,14,15,16)",
    "                  : Restart LA phase of NFS from an existing checkpoint",
    "(Integer < 32-bit): Number of threads to use in LA (if different from -threads)",
    "                  : Perform only LA phase of NFS",
    "                  : Perform only SQRT post-processing phase of NFS",
    "                  : Set yafu to idle priority",
    "(Floating point)  : Input work level (prior ECM work to specified number of digits)",
    "(Integer < 32-bit): Number of witnesses to use in Miller-Rabin PRP tests",
    "(Integer < 64-bit): B1 bound crossover point for switching to external ECM executable",
    "(Integer < 32-bit): Difficulty level above which SNFS testsieving is automatically performed to select best poly",
    "(String)          : Comma-delimited list of job files to GNFS test sieve",
    "(Integer < 32-bit): setting the threshold below which numbers are proved prime using APR-CL",
    "(Integer < 32-bit): setting the threshold above which numbers that are proved prime using APR-CL have additional verbosity",
    "(Floating point)  : If NFS filtering fails to produce a matrix, increment relations by specified percentage",
    "                  : Perform only filtering phase of NFS ",
    "                  : Force GNFS instead of SNFS",
    "(String)          : Supplies expression to execution",
    "(Integer < 32-bit): Repeat input command line specified number of times",
    "(Integer < 32-bit): Not currently implemented",
    "                  : Do not test clock speed when yafu initializes",
    "(Integer < 32-bit): SIQS small prime variation trial factoring bound",
    "(String)          : Filename of script to execute",
    "(Integer < 32-bit): Not currently implemented",
    "(Floating point)  : QS/SNFS crossover",
    "(Integer < 32-bit): Sieve of Eratosthenes block size in bytes",
    "                  : Force use of TLP variation in SIQS",
    "(Integer < 32-bit): SIQS large prime bound, in bits (2^bits)",
    "(Floating point)  : Exponent of SIQS DLP: attempt to split residues up to LPB^exponent",
    "(Floating point)  : Exponent of SIQS TLP: attempt to split residues up to LPB^exponent",
    "(Floating point)  : SIQS Batch Divisor: specifies max prime to include in batch GCD as divisor of max factorbase prime",
    "(Integer < 32-bit): SIQS Batch Target: how many residues to batch before doing batch GCD",
    "                  : Uses external GMP-ECM instead of internal AVX-ECM",
    "                  : Create savefile with B1 residues in AVX-ECM",
    "                  : Do not use SIQS Batch GCD",
    "(Integer < 32-bit): Digit level below which SIQS in-memory is used (no savefile)",
    "                  : Use GMP-ECM for stage 2",
    "(String)          : Filename for vecP+1 work input",
    "(String)          : Filename for vecP-1 work input",
    "(String)          : Filename to resume parallel P+1 or P-1",
    "                  : Output JSON info pretty printed (one pair per line) ",
    "                  : cadoMsieve",
    "                  : cado_dir",
    "                  : convert_poly_path",
    "(Integer < 32-bit): Number of gpu curves to run in a batch",
    "                  : use cgbn with gpu-ecm (external ecm binary enabled with this option must exist)",
    "                  : use gpu-ecm (external ecm binary enabled with this option must exist)",
    "(Integer < 32-bit): gpu device number",
    "                  : use AVX-ECM for stage 2",
    "(Integer < 32-bit): stop if a factor is found of less than <n> digits",
    "(Integer < 32-bit): stop if a factor is found of less than or equal to <n> digits",
    "(Integer < 32-bit): stop if a factor is found of equal to <n> digits",
    "(Integer < 32-bit): stop if a factor is found of greater than <n> digits",
    "(Integer < 32-bit): stop if a factor is found of greater than or equal to <n> digits",
    "(Integer < 32-bit): Base to use for stopXY options(default 10, range: 2 <= b <= 62)",
    "                  : Use for stopXY options to add constraint that number is prime",
    "(Integer < 32-bit): Factor base index at which to start using subset sum algorithm (default 0: unused)",
    "(Integer 0..15)    : Poly-bucket size exponent (default 12); 15 can request about 8 GiB per positive slice.",
    "                  : Set this to skip all checks for the existance of SNFS polynomials for the input",
    "(Integer==8,10,16): Output base in octal (8), decimal (default, 10), or hexadecimal (16)",
    "(Integer < 32-bit): Minimum relations to find in NFS before trying to filter",
    "(Integer < 32-bit): Stop factor() after finding k factors",
    "                  : Stop factor() after finding k factors, including small primes (trialdiv/rho)",
    "                  : Also output a one-line terse summary of the complete factorization found",
    "(Integer < 32-bit): do not start SIQS on composites above this size",
    "(Integer < 32-bit): do not start NFS on composites above this size",
    "                  : only run stage 1 of msieve nfs poly generation",
    "                  : only run stage 2 sizeopt of msieve nfs poly generation",
    "                  : only run stage 2 rootopt of msieve nfs poly generation",
    "(String)          : file containing nfs comma-delimited parameters, assumes algebraic-side sieving",
    "(Integer < 32-bit): input size (in bits) above which poly select will test sieve to est. sieve time as a stopping condition",
    "(Integer < 32-bit): the maximum threshold (in percent) of poly select compared to estimated sieve time",
    "                  : target density",
    "(String)          : Name of output JSON logfile",
    "                  : force the use of QLP SIQS (4 large primes)",
    "(Floating point)  : Exponent of SIQS QLP: attempt to split residues up to LPB^exponent",
    "                  : Use batch factorization of 3LP cofactors in NFS",
    "(Integer < 32-bit): analysis type for the sieve of Eratosthenes (default = 1 (find primes), 2 (find twins)",
    "                  : NFS: build the siever factor base cache once per job (needs sievers with cache validation)",
    "(String)          : raw args appended to msieve NFS poly stage1 (e.g.stage1_engine = cpu_hashtable",
    "(String)          : NFS: path to the cuda-sieve executable; sieve with it instead of gnfs-lasieve",
    "(String)          : NFS: comma separated CUDA device list for cuda_sieve, e.g. 0,1 (default 0)"
};

// indication of whether or not an option needs a corresponding argument.
// needs to be the same length as the above two arrays.
// 0 = no argument
// 1 = argument required
// 2 = argument optional
//
// 下标与 OptionArray 一一对应，全靠位置对齐，所以这一列读不出对应哪个选项。
// "e"（第 67 项）和 "obase"（第 109 项）曾经标成 0：它们于是被当成不收参数的
// 开关，表达式被丢掉，"e" 还会一路走到 applyArg(NULL) 里 strlen(NULL) 段错误，
// "obase" 则直接报 "invalid or out-of-range argument"。改哪个选项的 needsArg，
// 先用下标核对 OptionArray 里的名字，别照着下面的注释数——注释与实际下标早已错开。
// indication of whether or not an option needs a corresponding argument.
// needs to be the same length as the above two arrays.
// 0 = no argument
// 1 = argument required
// 2 = argument optional
//
// 这一列和 OptionArray 一一对应，全靠位置对齐，所以表里读不出对应哪个
// 选项。三张表必须同时增删条目：历史上删掉 "ggnfs_dir" 时只删了
// OptionArray 里的名字，OptionHelp 与 needsArg 忘了删，后面四十多个选项
// 的 needsArg 全部对错了别的选项——"e" 和 "obase" 当时是靠手工改值绕过去的，
// 其余的（-max_siqs、-op、-o、-work、-lathreads、-nfs_params、-analysis……）
// 一直是不对的：该收参数的被当成开关，-max_siqs 会直接报错退出；不该收参数
// 的反而把命令行里下一个参数吞掉。
//
// 改这张表时按选项名核对下标，不要照着行尾的注释数。要新增或删除选项，
// 三张表一起改，改完跑 test/layer3/test_options.cpp 里的
// t_needsarg_consistent：它逐个选项用空参数调一次 applyOpt，表和分支
// 对不上就会失败。
int needsArg[NUMOPTIONS] = {
    1,1,1,1,1,
    1,1,1,1,1,
    1,1,1,1,1,
    1,1,1,1,1,
    0,0,0,0,0,
    1,0,0,0,1,
    1,1,0,1,1,
    1,1,2,0,1,
    0,0,1,1,2,
    2,0,1,0,1,
    1,1,0,1,0,
    0,0,1,1,1,
    1,1,1,1,1,
    0,0,1,1,1,
    0,1,1,1,1,
    1,0,1,1,1,
    1,1,0,0,0,
    1,0,1,1,1,
    0,0,1,1,1,
    0,0,1,0,1,
    1,1,1,1,1,
    0,1,1,0,1,
    1,1,0,0,1,
    1,0,0,0,1,
    1,1,1,1,0,
    1,0,1,0,1,
    1,1
};

// command line option aliases, specified by '--'
// need the same number of strings here, even if
// some of them are blank (i.e., have no long form alias).
char LongOptionAliases[NUMOPTIONS][MAXOPTIONLEN] = {
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", "", "", "",
    "", "", ""
};


static void invalid_argument(std::string_view opt)
{
    fprintf(stderr, "invalid or out-of-range argument for option %.*s\n",
            (int)opt.size(), opt.data());
    exit(EXIT_FAILURE);
}

/* 转换前后都检查，避免空值、部分解析和窄整数截断。 */
static uint64_t parse_uint(const char *arg, std::string_view opt, uint64_t limit)
{
    char *end;
    unsigned long long value;
    if (arg == NULL || !isdigit((unsigned char)arg[0]))
        invalid_argument(opt);
    errno = 0;
    value = strtoull(arg, &end, 10);
    if (errno == ERANGE || *end != '\0' || value > limit)
        invalid_argument(opt);
    return (uint64_t)value;
}

static int parse_int(const char *arg, std::string_view opt)
{
    char *end;
    long value;
    if (arg == NULL || *arg == '\0' || isspace((unsigned char)*arg))
        invalid_argument(opt);
    errno = 0;
    value = strtol(arg, &end, 10);
    if (errno == ERANGE || end == arg || *end != '\0' || value < INT_MIN || value > INT_MAX)
        invalid_argument(opt);
    return (int)value;
}

static double parse_double(const char *arg, std::string_view opt)
{
    char *end;
    double value;
    if (arg == NULL || *arg == '\0' || isspace((unsigned char)*arg))
        invalid_argument(opt);
    errno = 0;
    value = strtod(arg, &end);
    if (errno == ERANGE || end == arg || *end != '\0' || !isfinite(value) || value < 0)
        invalid_argument(opt);
    return value;
}

static uint64_t parse_ecm_bound(const char *arg, std::string_view opt)
{
    char *end;
    long double value;
    if (strchr(arg, 'e') == NULL && strchr(arg, 'E') == NULL)
        return parse_uint(arg, opt, UINT64_MAX);
    /* 科学计数法不回写调用者缓冲区；转换前拒绝溢出和非整数。 */
    if (!(isdigit((unsigned char)*arg) || *arg == '.'))
        invalid_argument(opt);
    errno = 0;
    value = strtold(arg, &end);
    if (errno == ERANGE || end == arg || *end != '\0' || !isfinite(value) ||
        value < 0 || value >= 18446744073709551616.0L || floorl(value) != value)
        invalid_argument(opt);
    return (uint64_t)value;
}

static void parse_range(const char *arg, std::string_view opt, uint32_t *start,
    uint32_t *stop, int require_stop)
{
    const char *comma = strchr(arg, ',');
    char first[32];
    if (comma == NULL)
    {
        if (require_stop)
            invalid_argument(opt);
        *start = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
        *stop = 0;
        return;
    }
    if ((size_t)(comma - arg) >= sizeof(first))
        invalid_argument(opt);
    memcpy(first, arg, (size_t)(comma - arg));
    first[comma - arg] = '\0';
    *start = (uint32_t)parse_uint(first, opt, UINT32_MAX);
    *stop = (uint32_t)parse_uint(comma + 1, opt, UINT32_MAX);
    if (*stop < *start)
        invalid_argument(opt);
}


// ========================================================================
// modify this function to handle input arguments
// ========================================================================
void applyArg(char* arg, int argNum, options_t* options)
{
    // needsArg 为 0 的选项会以 NULL 调用到这里（"没给参数"与"没这个参数"同义），
    // 此时什么都不做，不能往下走 strlen/strcpy。
    if (arg == NULL)
    {
        return;
    }

    if (argNum == 0)
    {
        options->inputExpr = (char*)xrealloc(options->inputExpr,
            (strlen(arg) + 1) * sizeof(char));
        strcpy(options->inputExpr, arg);
    }

    return;
}

// ========================================================================
// modify this function to handle each option case, assigning values
// or arguments to the members of the options_t structure.
// ========================================================================
void applyOpt(std::string_view opt, char* arg, options_t* options)
{
    int i;
    for (i = 0; i < NUMOPTIONS; i++)
    {
        if (opt == OptionArray[i])
            break;
    }
    if (i == NUMOPTIONS || (needsArg[i] == 1 && arg == NULL))
        invalid_argument(opt);
    if (arg != NULL && strlen(arg) >= MAXARGLEN && i != 68)
        invalid_argument(opt);

    if (opt == OptionArray[0])
    {
        //"B1pm1"
        options->B1pm1 = parse_uint(arg, opt, UINT64_MAX);
    }
    else if (opt == OptionArray[1])
    {
        //"B1pp1"
        options->B1pp1 = parse_uint(arg, opt, UINT64_MAX);
    }
    else if (opt == OptionArray[2])
    {
        //"B1ecm"

        options->B1ecm = parse_ecm_bound(arg, opt);
    }
    else if (opt == OptionArray[3])
    {
        //"rhomax"
        options->rhomax = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[4])
    {
        //"B2pm1"
        options->B2pm1 = parse_uint(arg, opt, UINT64_MAX);
    }
    else if (opt == OptionArray[5])
    {
        // "B2pp1"
        options->B2pp1 = parse_uint(arg, opt, UINT64_MAX);
    }
    else if (opt == OptionArray[6])
    {
        // "B2ecm"
        options->B2ecm = parse_uint(arg, opt, UINT64_MAX);
    }
    else if (opt == OptionArray[7])
    {
        // qssave
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->qssave, arg);
        else
            printf("*** argument to savefile too long, ignoring ***\n");
    }
    else if (opt == OptionArray[8])
    {
        // siqsB
        options->siqsB = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[9])
    {
        // siqsTF
        options->siqsTF = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[10])
    {
        // siqsR
        options->siqsR = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[11])
    {
        // siqsT
        options->siqsT = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[12])
    {
        // siqsNB
        options->siqsNB = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[13])
    {
        // siqsM
        options->siqsM = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[14])
    {
        //argument is a string
        if ((strlen(arg) == 0) || (std::string_view(arg) == "NUL") ||
            (std::string_view(arg) == "NULL") || (std::string_view(arg) == "nul") ||
            (std::string_view(arg) == "null"))
        {
            strcpy(options->factorlog, "");
        }
        else
        {
            if (strlen(arg) < MAXARGLEN)
                strcpy(options->factorlog, arg);
            else
                printf("*** argument to logfile too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[15])
    {
        //argument is a string
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->batchfile, arg);
        }
        else
            printf("*** argument to batchfile too long, ignoring ***\n");
    }
    else if (opt == OptionArray[16])
    {
        printf("attempting to parse user rng seed from %s\n", arg);
        options->rand_seed = parse_uint(arg, opt, UINT64_MAX);
        //sscanf(arg, "%" PRIu64"", &options->rand_seed);
        printf("read: %" PRIu64"\n", options->rand_seed);
    }
    else if (opt == OptionArray[17])
    {
        options->sigma = parse_uint(arg, opt, UINT64_MAX);
    }
    else if (opt == OptionArray[18])
    {
        //argument is a string
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->sessionlog, arg);
        }
        else
        {
            printf("*** argument to sessionname too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[19])
    {
        options->threads = parse_uint(arg, opt, UINT32_MAX);
        if (options->threads == 0 || options->threads > INT_MAX)
            invalid_argument(opt);
    }
    else if (opt == OptionArray[20])
    {
        options->verbosity++;
    }
    else if (opt == OptionArray[21])
    {
        // silent
        options->verbosity = -1;
    }
    else if (opt == OptionArray[22])
    {
        options->pfile = 1;
    }
    else if (opt == OptionArray[23])
    {
        options->pscreen = 1;
    }
    else if (opt == OptionArray[24])
    {
        options->siqsForceDLP = 1;
    }
    else if (opt == OptionArray[25])
    {
        options->fermat_max = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[26])
    {
        options->no_opt = 1;
    }
    else if (opt == OptionArray[27])
    {
        options->vproc++;
    }
    else if (opt == OptionArray[28])
    {
        strcpy(options->fact_plan, "noecm");
    }
    else if (opt == OptionArray[29])
    {
        if (strlen(arg) < MAXARGLEN)
        {
            options->num_tune_info++;
            options->tune_info = (char**)xrealloc(options->tune_info, options->num_tune_info * sizeof(char*));
            options->tune_info[options->num_tune_info - 1] = (char*)xmalloc(MAXARGLEN * sizeof(char));
            strcpy(options->tune_info[options->num_tune_info-1], arg);
        }
        else
        {
            printf("*** argument to tune_info too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[30])
    {
        //argument "pretest_ratio"
        options->pretest_ratio = parse_double(arg, opt);
    }
    else if (opt == OptionArray[31])
    {
        //argument "xover"
        options->xover = parse_double(arg, opt);
    }
    else if (opt == OptionArray[32])
    {
        //argument "one"
        options->one_factor = 1;
    }
    else if (opt == OptionArray[33])
    {
        //argument "op".  argument is a string
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->opfile, arg);
        }
        else
        {
            printf("*** argument to -op too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[34])
    {
        //argument "of".  argument is a string
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->offile, arg);
        }
        else
        {
            printf("*** argument to -of too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[35])
    {
        //argument "ou".  argument is a string
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->oufile, arg);
        }
        else
        {
            printf("*** argument to -ou too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[36])
    {
        //argument "plan".  argument is a string
        if (strlen(arg) < MAXARGLEN)
        {
            // test for recognized options.  
            if (std::string_view(arg) == "none")
                strcpy(options->fact_plan, "none");
            else if (std::string_view(arg) == "noecm")
                strcpy(options->fact_plan, "noecm");
            else if (std::string_view(arg) == "light")
                strcpy(options->fact_plan, "light");
            else if (std::string_view(arg) == "deep")
                strcpy(options->fact_plan, "deep");
            else if (std::string_view(arg) == "normal")
                strcpy(options->fact_plan, "normal");
            else if (std::string_view(arg) == "custom")
                strcpy(options->fact_plan, "custom");
            else
            {
                printf("*** unknown plan option, ignoring ***\n");
                strcpy(options->fact_plan, "normal");
            }
        }
        else
        {
            printf("*** argument to -plan too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[37])
    {
        //argument "pretest"
        if (arg == NULL)
        {
            // no argument, use the value "1" to signify doing 
            // pretesting to the bounds computed by factor()
            options->pretest = 1;
        }
        else
        {
            // an optional argument to pretest is interpreted as
            // a maximum t-level to pretest to
            options->pretest = parse_uint(arg, opt, UINT32_MAX);
        }
    }
    else if (opt == OptionArray[38])
    {
        //argument "no_expr"
        options->want_output_expr = 0;
    }
    else if (opt == OptionArray[39])
    {
        //argument "o".  Indicates output filename ggnfs sieving.
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->nfs_outfile, arg);
        }
        else
        {
            printf("*** argument to -o too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[40])
    {
        //argument "a".  Indicates algebraic side special Q.
        options->alg_side = 1;
    }
    else if (opt == OptionArray[41])
    {
        //argument "r".  Indicates rational side special Q.
       options->rat_side = 1;
    }
    else if (opt == OptionArray[42])
    {
        //argument "ggnfsT".  Indicates timeout (in seconds) for NFS job.
        options->nfs_timeout = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[43])
    {
        //argument "job".  Indicates input .job file automated NFS.
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->nfs_jobfile, arg);
        }
        else
        {
            printf("*** argument to -job too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[44])
    {
        // argument "ns".  do nfs sieving
        if (arg != NULL)
        {
            // if an argument was supplied, parse the start and range of 
            //special Q in the format X,Y
            parse_range(arg, opt, &options->sieveQstart, &options->sieveQstop, 0);
        }
        else
        {
            options->sieveQstart = 1;
            options->sieveQstop = 1;
        }

    }
    else if (opt == OptionArray[45])
    {
        //argument "np".  do poly finding.

        if (arg != NULL)
        {
            // if an argument was supplied, parse the start and stop coefficient range in the
            // format X,Y
            parse_range(arg, opt, &options->polystart, &options->polystop, 1);
        }
        else
        {
            options->polystart = 1;
            options->polystop = 1;
        }
    }
    else if (opt == OptionArray[46])
    {
        //argument "nc".  Do post processing, starting with filtering
        options->nc = 1;
    }
    else if (opt == OptionArray[47])
    {
        //argument "psearch".  modify poly search methodology
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->poly_method, arg);

            if (std::string_view(arg) == "wide")
                strcpy(options->poly_method, arg); // fobj->nfs_obj.poly_option = 1;
            else if (std::string_view(arg) == "deep")
                strcpy(options->poly_method, arg); //fobj->nfs_obj.poly_option = 2;
            else if (std::string_view(arg) == "fast")
                strcpy(options->poly_method, arg); //fobj->nfs_obj.poly_option = 0;
            else if (std::string_view(arg) == "min")
                strcpy(options->poly_method, arg); //fobj->nfs_obj.poly_option = 3;
            else if (std::string_view(arg) == "avg")
                strcpy(options->poly_method, arg); //fobj->nfs_obj.poly_option = 4;
            else if (std::string_view(arg) == "good")
                strcpy(options->poly_method, arg); //fobj->nfs_obj.poly_option = 5;
            else
            {
                printf("option -psearch recognizes arguments 'deep', 'wide', 'fast', 'min', 'avg', or 'good'.\n  see docfile.txt for details\n");
                exit(1);
            }

        }
        else
        {
            printf("*** argument to -psearch too long, ignoring ***\n");
        }

    }
    else if (opt == OptionArray[48])
    {
        //argument "R".  nfs restart flag
        options->nfs_resume = 1;
    }
    else if (opt == OptionArray[49])
    {
        //argument "pbatch".  Indicates size of blocks of leading coefficients to
        //distribute to each thread in threaded NFS poly selection.
        options->poly_batch = parse_uint(arg, opt, UINT32_MAX);
        if (options->poly_batch == 0)
            options->poly_batch = 5000;
    }
    else if (opt == OptionArray[50])
    {
        // argument "ecm_path"
        //argument is a string
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->ecm_path, arg);
        else
            printf("*** argument to ecm_path too long, ignoring ***\n");
    }
    else if (opt == OptionArray[51])
    {
        // argument "siever"
        options->ggnfs_siever = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[52])
    {
        //argument "ncr".  linear algebra restart flag
        options->ncr = 1;
    }
    else if (opt == OptionArray[53])
    {
        // argument "lathreads"
        options->lathreads = parse_uint(arg, opt, UINT32_MAX);
        if (options->lathreads == 0 || options->lathreads > INT_MAX)
            invalid_argument(opt);
    }
    else if (opt == OptionArray[54])
    {
        //argument "nc2".  do linear algebra.
        options->nc2 = 1;
    }
    else if (opt == OptionArray[55])
    {
        //argument "nc3".  do nfs sqrt
        options->nc3 = 1;
    }
    else if (opt == OptionArray[56])
    {
        //argument "p".  set to idle priority.
        options->yafu_idle = 1;
    }
    else if (opt == OptionArray[57])
    {
        //argument "work"
        options->work = parse_double(arg, opt);
    }
    else if (opt == OptionArray[58])
    {
        //argument "nprp"
        options->num_prp_witnesses = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[59])
    {
        // argument "ext_ecm"
        options->ext_ecm_xover = parse_uint(arg, opt, UINT64_MAX);
    }
    else if (opt == OptionArray[60])
    {
        //argument "testsieve"
        options->snfs_testsieve_threshold = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[61])
    {
        // argument "nt"
        if (arg == NULL)
        {
            printf("expected argument for option %.*s\n",
                (int)opt.size(), opt.data());
            exit(1);
        }
        else if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->testsieve, arg);
        }
        else
        {
            printf("*** argument to nt too long, ignoring ***\n");
        }
    }
    else if (opt == OptionArray[62])
    {
        // argument "aprcl_p", setting the threshold below which numbers
        // are proved prime using APR-CL
        options->aprcl_p = parse_uint(arg, opt, UINT32_MAX);
        if (options->aprcl_p > 6021)
        {
            printf("APR-CL primality proving is possible only for numbers less"
                " than 6021 digits... setting limit to 6021 digits\n");
            options->aprcl_p = 6021;
        }
    }
    else if (opt == OptionArray[63])
    {
        // argument "aprcl_d", setting the threshold above which numbers
        // that are proved prime using APR-CL have additional verbosity enabled
        options->aprcl_d = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[64])
    {
        //argument "filt_bump"
        options->filt_bump = parse_double(arg, opt);
    }
    else if (opt == OptionArray[65])
    {
        //argument "nc1".  do msieve filtering.
        options->nc1 = 1;
    }
    else if (opt == OptionArray[66])
    {
        //argument "gnfs"
        options->force_gnfs = 1;
    }
    else if (opt == OptionArray[67])
    {
        applyArg(arg, 0, options);
    }
    else if (opt == OptionArray[68])
    {
        //argument "repeat"
        options->repeat = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[69])
    {
        //argument "ecmtime"

    }
    else if (opt == OptionArray[70])
    {
        //argument "no_clk_test"
        options->no_clk_test = 1;
    }
    else if (opt == OptionArray[71])
    {
        //argument "siqsTFSm"
        options->siqsTFSm = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[72])
    {
        //argument "script"
        if (strlen(arg) >= sizeof(options->scriptfile))
            invalid_argument(opt);
        strcpy(options->scriptfile, arg);
    }
    else if (opt == OptionArray[73])
    {
        //argument "degree"

    }
    else if (opt == OptionArray[74])
    {
        //argument "snfs_xover"
        options->qs_snfs_xover = parse_double(arg, opt);
        //fobj->autofact_obj.prefer_xover = 1;
    }
    else if (opt == OptionArray[75])
    {
        //argument "soe_block"
        options->soe_blocksize = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[76])
    {
        //argument "forceTLP"
        options->siqsForceTLP = 1;
    }
    else if (opt == OptionArray[77])
    {
        //argument "siqsLPB"
        // the maximum allowed large prime, in bits, in SIQS
        options->siqsLPB = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[78])
    {
        //argument "siqsMFBD"
        // the exponent of the large prime bound such that residues larger than
        // lpb^siqsMFBD are subjected to double large prime factorization attempts
        options->siqsMFBD = parse_double(arg, opt);
    }
    else if (opt == OptionArray[79])
    {
        //argument "siqsMFBT"
        // the exponent of the large prime bound such that residues larger than
        // lpb^siqsMFBT are subjected to triple large prime factorization attempts
        options->siqsMFBT = parse_double(arg, opt);
    }
    else if (opt == OptionArray[80])
    {
        //argument "siqsBDiv"
        // The divider of large_prime_max as the upper bound for
        // primes to multiply when using batch GCD in TLP factorizations.
        //fobj->qs_obj.gbl_override_bdiv_flag = 1;
        options->siqsBDiv = parse_double(arg, opt);
    }
    else if (opt == OptionArray[81])
    {
        //argument "siqsBT" ("Batch Target")
        // How many relations to batch up before they are processed
        options->siqsBT = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
        if (options->siqsBT == 0)
            invalid_argument(opt);
    }
    else if (opt == OptionArray[82])
    {
        // argument "prefer_gmpecm"
        options->prefer_gmpecm = 1;
    }
    else if (opt == OptionArray[83])
    {
        //argument "save_b1"
        options->saveB1 = 1;
    }
    else if (opt == OptionArray[84])
    {
        //argument "siqsNobat"
        // Whether or not to use batch factoring in 3LP
        options->siqsNobat = 1;
    }
    else if (opt == OptionArray[85])
    {
        // argument "inmem"
        // cutoff for processing in-memory
        options->inmem_cutoff = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[86])
    {
        // argument "prefer_gmpecm_stg2"
        options->prefer_gmpecm_stg2 = 1;
        options->prefer_avxecm_stg2 = 0;
    }
    else if (opt == OptionArray[87])
    {
        // argument "vpp1_work_file"
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->vpp1_work_file, arg);
        else
            printf("*** argument to vpp1_work_file too long, ignoring ***\n");
    }
    else if (opt == OptionArray[88])
    {
        // argument "vpm1_work_file"
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->vpm1_work_file, arg);
        else
            printf("*** argument to vpm1_work_file too long, ignoring ***\n");
    }
    else if (opt == OptionArray[89])
    {
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->resume_file, arg);
        else
            printf("*** argument to resume too long, ignoring ***\n");
    }
    else if (opt == OptionArray[90])
    {
        options->json_pretty = 1;
    }
    else if (opt == OptionArray[91])
    {
        // argument "cadoMsieve"
        options->cadoMsieve = 1;
    }
    else if (opt == OptionArray[92])
    {
        // argument "cado_dir"
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->cado_dir, arg);
        else
            printf("*** argument to cado_dir too long, ignoring ***\n");
    }
    else if (opt == OptionArray[93])
    {
        // argument "convert_poly_path"
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->convert_poly_path, arg);
        else
            printf("*** argument to convert_poly_path too long, ignoring ***\n");
    }
    else if (opt == OptionArray[94])
    {
        // argument "gpucurves"
        options->gpucurves = parse_int(arg, opt);
    }
    else if (opt == OptionArray[95])
    {
        // argument "use_cgbn"
        options->use_cgbn = 1;
    }
    else if (opt == OptionArray[96])
    {
        // argument "use_gpuecm"
        options->use_gpuecm = 1;
    }
    else if (opt == OptionArray[97])
    {
        // argument "use_gpudev"
        options->use_gpudev = parse_int(arg, opt);
    }
    else if (opt == OptionArray[98])
    {
        // argument "prefer_avxecm_stg2"
        options->prefer_avxecm_stg2 = 1;
        options->prefer_gmpecm_stg2 = 0;
    }
    else if (opt == OptionArray[99])
    {
        // argument "stoplt"
        options->stoplt = parse_int(arg, opt);
        options->check_stop_conditions = 1;
    }
    else if (opt == OptionArray[100])
    {
        // argument "stople"
        options->stople = parse_int(arg, opt);
        options->check_stop_conditions = 1;
    }
    else if (opt == OptionArray[101])
    {
        // argument "stopeq"
        options->stopeq = parse_int(arg, opt);
        options->check_stop_conditions = 1;
    }
    else if (opt == OptionArray[102])
    {
        // argument "stopgt"
        options->stopgt = parse_int(arg, opt);
        options->check_stop_conditions = 1;
    }
    else if (opt == OptionArray[103])
    {
        // argument "stopge"
        options->stopge = parse_int(arg, opt);
        options->check_stop_conditions = 1;
    }
    else if (opt == OptionArray[104])
    {
        // argument "stopbase"
        options->stopbase = parse_int(arg, opt);
        if (options->stopbase < 2 || options->stopbase > 62)
            invalid_argument(opt);
    }
    else if (opt == OptionArray[105])
    {
        // argument "stopprime"
        options->stopprime = 1;
    }
    else if (opt == OptionArray[106])
    {
        // argument "siqsSSidx"
        options->siqsSSidx = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[107])
    {
        // argument "siqsSSalloc"
        options->siqsSSalloc = (uint32_t)parse_uint(arg, opt, 15);
    }
    else if (opt == OptionArray[108])
    {
        // argument "skipSNFScheck"
        options->skip_snfscheck = 1;
    }
    else if (opt == OptionArray[109])
    {
        // argument "OBASE"
        int b = parse_int(arg, opt);
        if ((b == 2) || (b == 8) || (b == 10) || (b == 16))
        {
            options->obase = b;
        }
        else
        {
            printf("*** argument obase must be either 2,8,10,or 16 ***\n");
            options->obase = 10;
        }
    }
    else if (opt == OptionArray[110])
    {
        // argument "minrels"
        options->minrels = parse_uint(arg, opt, UINT32_MAX);
    }
    else if (opt == OptionArray[111])
    {
        // argument "stopk".  also sets strict, which includes
        // trial division and rho factors in this number (why shouldn't it?)
        options->stopk = parse_int(arg, opt);
        options->strict = 1;
    }
    else if (opt == OptionArray[112])
    {
        // argument "stop_strict"
        options->strict = 1;
    }
    else if (opt == OptionArray[113])
    {
        // terse
        options->terse_output = 1;
    }
    else if (opt == OptionArray[114])
    {
        // max_siqs
        options->max_siqs = parse_int(arg, opt);
    }
    else if (opt == OptionArray[115])
    {
        // max_nfs
        options->max_nfs = parse_int(arg, opt);
    }
    else if (opt == OptionArray[116])
    {
        // np1
        options->np1 = 1;
    }
    else if (opt == OptionArray[117])
    {
        // nps
        options->nps = 1;
    }
    else if (opt == OptionArray[118])
    {
        // npr
        options->npr = 1;
    }
    else if (opt == OptionArray[119])
    {
        // params_file
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->params_file, arg);
        else
            printf("*** argument to params_file too long, ignoring ***\n");
    }
    else if (opt == OptionArray[120])
    {
        // poly_testsieve
        options->poly_testsieve = parse_int(arg, opt);
    }
    else if (opt == OptionArray[121])
    {
        // poly_percent_max
        options->poly_percent_max = parse_int(arg, opt);
    }
     else if (opt == OptionArray[122])
    {
        // argument "td"
        options->td = parse_int(arg, opt);
    }
    else if (opt == OptionArray[123])
    {
        // jsonlog
        if (strlen(arg) < MAXARGLEN)
            strcpy(options->jsonlog, arg);
        else
            printf("*** argument to jsonlog too long, ignoring ***\n");
    }
    else if (opt == OptionArray[124])
    {
        //argument "forceQLP"
        options->siqsForceQLP = 1;
    }
    else if (opt == OptionArray[125])
    {
        //argument "siqsMFBQ"
        // the exponent of the large prime bound such that residues larger than
        // lpb^siqsMFBQ are subjected to quad large prime factorization attempts
        options->siqsMFBQ = parse_double(arg, opt);
    }
    else if (opt == OptionArray[126])
    {
        //argument "forceQLP"
        options->nfs_batch_3lp = 1;
    }
    else if (opt == OptionArray[127])
    {
        //argument "forceQLP"
        options->soe_analysis  = parse_int(arg, opt);
    }
    else if (opt == OptionArray[128])
    {
        //argument "keep_afb": bare flag enables; an explicit value
        //(e.g. keep_afb=0 in an ini file) is respected
        if (arg == NULL || strlen(arg) == 0)
            options->keep_afb = 1;
        else
            options->keep_afb = (parse_int(arg, opt) != 0);
    }
    else if (opt == OptionArray[129])
    {
        //argument "nfs_stage1_args"
        strcpy(options->nfs_stage1_args, arg);
    }
    else if (opt == OptionArray[130])
    {
        //argument "cuda_sieve": path to the cuda-sieve executable (bench).
        // giving one selects the cuda siever for NFS sieving.
        if (strlen(arg) < MAXARGLEN)
        {
            strcpy(options->cuda_sieve, arg);
        }
        else
        {
            printf("*** argument to cuda_sieve too long, ignoring ***\n");
        }
        }
    else if (opt == OptionArray[131])
    {
        //argument "cuda_dev": comma separated CUDA device indices, e.g. 0,1
        // only digits and single commas between numbers are accepted.
        size_t k, len = strlen(arg);
        int ok = (len > 0 && len < MAXARGLEN);

        for (k = 0; ok && k < len; k++)
        {
            if (arg[k] == ',')
            {
                if (k == 0 || k == len - 1 || arg[k - 1] == ',') ok = 0;
            }
            else if (!isdigit((unsigned char)arg[k])) ok = 0;
        }

        if (ok)
        {
            strcpy(options->cuda_dev, arg);
        }
        else
        {
            printf("*** argument to cuda_dev must look like 0 or 0,1 ; ignoring ***\n");
        }
        }
    else
    {
        int i;

        printf("invalid option %.*s\n", (int)opt.size(), opt.data());
        printf("supported options are: ");
        for (i = 0; i < NUMOPTIONS; i++)
        {
            if (i % 5 == 0) printf("\n");
            printf("%s ", options->OptionArray[i]);
        }
        exit(EXIT_FAILURE);
    }

    return;
}

// ========================================================================
// modify this function to handle assign a default value to each 
// member of the options_t structure, if desired.
// ========================================================================
options_t* initOpt(void)
{
    options_t* options = (options_t*)xmalloc(sizeof(options_t));
    int i;

    memset(options, 0, sizeof(*options));

    for (i = 0; i < NUMOPTIONS; i++)
    {
        strcpy(options->OptionArray[i], OptionArray[i]);
        strcpy(options->OptionHelp[i], OptionHelp[i]);
        strcpy(options->LongOptionAliases[i], LongOptionAliases[i]);
        options->needsArg[i] = needsArg[i];
    }

    // ========================================================================
    // define how many required and optional arguments there are
    options->numArguments = 1;
    options->numRequired = 0;
    // ========================================================================


    // ========================================================================
    // default values assigned to optional arguments here
    options->inputExpr = (char*)xmalloc(MAXARGLEN * sizeof(char));
    strcpy(options->inputExpr, "");
    // ========================================================================


    // ========================================================================
    // default values assigned to switches here:
    
    // general options
    options->yafu_idle = 0;
    strcpy(options->factorlog, "factor.log");
    strcpy(options->batchfile, "");
    strcpy(options->sessionlog, "session.log");
    strcpy(options->scriptfile, "");
    options->num_tune_info = 0;
    options->tune_info = (char**)xmalloc(1 * sizeof(char*));
    options->rand_seed = 0;
    /* 默认用全部核心。命令行 -threads 与 yafu.ini 里的 threads= 会在后面
       覆盖它（启动顺序是 ini 先、命令行后）。 */
    options->threads = (uint32_t)ytools_get_nprocs();
    options->verbosity = 0;
    options->vproc = 0;
    strcpy(options->expr, "");
    options->repeat = 0;
    options->no_clk_test = 1;
    options->json_pretty = 0;
    options->obase = 10;
    options->terse_output = 0;

    // autofact options
    options->no_ecm = 0;
    options->pretest_ratio = 4.0 / 13.0;
    options->xover = 95;
    options->qs_snfs_xover = 95;
    options->one_factor = 0;
    strcpy(options->opfile, "");
    strcpy(options->offile, "");
    strcpy(options->oufile, "");
    strcpy(options->fact_plan, "normal");
    options->pretest = 0;
    options->want_output_expr = 1;
    options->num_tune_info = 0;
    options->stopbase = 10;
    options->stopeq = -1;
    options->stople = -1;
    options->stoplt = -1;
    options->stopge = -1;
    options->stopgt = -1;
    options->stopk = -1;
    options->strict = 0;
    options->stopprime = 0;
    options->check_stop_conditions = 0;
    options->max_siqs = -1;
    options->max_nfs = -1;
    
    // nfs options
    strcpy(options->nfs_outfile, "nfs.dat");
    options->alg_side = 0;
    options->rat_side = 0;
    options->nfs_timeout = 1u << 31;
    strcpy(options->nfs_jobfile, "nfs.job");
    strcpy(options->params_file, "");
    options->sieveQstart = 0;
    options->sieveQstop = 0;
    options->polystart = 0;
    options->polystop = 0;
    options->nc = 0;
    options->nc1 = 0;
    options->nc2 = 0;
    options->nc3 = 0;
    options->ncr = 0;
    options->np1 = 0;
    options->nps = 0;
    options->npr = 0;
    /* 线性代数（msieve 的 Lanczos）本身是多线程的，默认也用全部核心。
       矩阵不够大时线程数会被矩阵切分限制，不会白起线程。 */
    options->lathreads = (uint32_t)ytools_get_nprocs();
    options->filt_bump = 5;
    options->force_gnfs = 0;
    options->snfs_testsieve_threshold = 160;
    strcpy(options->testsieve, "");
    strcpy(options->poly_method, "avg");
    options->nfs_resume = 0;
    options->poly_batch = 5000;
    options->ggnfs_siever = 0;
    // cado_dir used to default to whatever ggnfs_dir was set to.  ggnfs_dir
    // is gone with the siever executables, so it gets its own default: the
    // current directory, same as factor_common.c uses for cado_dir on non-Windows.
    strcpy(options->cado_dir, "./");
    options->skip_snfscheck = 0;
    options->minrels = 0;
    options->td = 0;
    options->poly_percent_max = 8;
    options->poly_testsieve = 390;
    options->nfs_batch_3lp = 0;
    options->keep_afb = 0;
    strcpy(options->nfs_stage1_args, "");
    strcpy(options->cuda_sieve, "");
    strcpy(options->cuda_dev, "");

    // prime finding options
    options->soe_blocksize = 32768;
    options->aprcl_p = 500;
    options->aprcl_d = 200;
    options->num_prp_witnesses = 1;
    options->pfile = 0;
    options->pscreen = 0;
    options->soe_analysis = 1;

    // siqs options
    strcpy(options->qssave, "siqs.dat");
    options->siqsB = 0;
    options->siqsTF = 0;
    options->siqsR = 0;
    options->siqsT = 0;
    options->siqsNB = 0;
    options->siqsM = 0;
    options->siqsForceDLP = 0;
    options->siqsTFSm = 0;
    options->siqsNobat = 0;
    options->siqsSSidx = 0;
    options->siqsSSalloc = 12;
    options->siqsForceTLP = 0;
    options->siqsForceQLP = 0;
    options->siqsLPB = 0;
    options->siqsMFBD = 1.85;
    options->siqsMFBT = 2.7;
    options->siqsMFBQ = 3.3;
    options->siqsBDiv = 3.0;
    // larger batches of relations in TLP are more efficient
    // to run in the batch GCD:
    // 100k: 11314 rels/sec 
    // 500k: 19230 rels/sec
    // 1000k: 25784 rels/sec
    // but there are a couple tradeoffs.  One, the GCD uses
    // more memory and two, it both takes longer to gather
    // the batch and the processing takes longer.  These two
    // issues can significantly extend runtimes toward the
    // end of a factorization when not many more relations are
    // needed and when the cycle formation rate is very high.
    // so we start this out fairly high and gradually decrease it.
    options->siqsBT = 1000000;
    options->no_opt = 0;
    options->inmem_cutoff = 70;

    // ecm options
    strcpy(options->ecm_path, "");
    options->work = 0;
    options->sigma = 0;
    // if we can use AVX-ECM, do so, and save B1 checkpoints.
#ifdef USE_AVX512F
    options->prefer_gmpecm = 0;
    options->prefer_gmpecm_stg2 = 0;
    options->saveB1 = 0;
    options->ext_ecm_xover = 300000000;
#else
    options->prefer_gmpecm = 1;
    options->prefer_gmpecm_stg2 = 1;
    options->saveB1 = 0;
    options->ext_ecm_xover = 48000;
#endif
    options->prefer_avxecm_stg2 = 0;
    options->B1pm1 = 100000;
    options->B1pp1 = 20000;
    options->B1ecm = 11000;
    options->rhomax = 1000;
    options->B2pm1 = 0;
    options->B2pp1 = 0;
    options->B2ecm = 0;
    options->fermat_max = 1000000;
    strcpy(options->vpp1_work_file, "pp1_work.ini");
    strcpy(options->vpm1_work_file, "pm1_work.ini");
    strcpy(options->resume_file, "");
    strcpy(options->jsonlog, "factor.json");
    
    // ========================================================================
    return options;
}


// ========================================================================
// this function should not need to be changed
// ========================================================================
int processOpts(int argc, char** argv, options_t* options)
{
    // this function takes the command line arguments and returns
    // an options_t, which will have any valid options specified
    // on the command line filled in.  Return the number of options
    // processed.
    int i, j, k = 0, valid, longswitch = 0;
    /* 加上 NUL 的一位：strncpy 拷满 MAXOPTIONLEN 时不会补 NUL。 */
    char optbuf[MAXOPTIONLEN + 1];
    int numOpt = 0;

    //argument loop
    i = 1;
    while (i < argc)
    {
        longswitch = 0;

        if (std::string_view(argv[i]) == "-h" || std::string_view(argv[i]) == "--help")
        {
            printUsage(options);
            exit(EXIT_SUCCESS);
        }
        if (std::string_view(argv[i]) == "--")
        {
            for (++i; i < argc; ++i)
            {
                if (numOpt >= options->numArguments)
                    invalid_argument("input expression");
                applyArg(argv[i], numOpt++, options);
            }
            break;
        }

        // read in the option
        if ((strlen(argv[i]) > 1) && (argv[i][0] == '-') && (argv[i][1] == '-'))
        {
            longswitch = 1;
        }
        else if (argv[i][0] == '-')
        {
            longswitch = 0;
        }
        else
        {
            if (numOpt >= (options->numArguments))
            {
                printf("Too many inputs\n\n");
                printUsage(options);
                exit(EXIT_FAILURE);
            }
            else
            {
                // process argument
                applyArg(argv[i], numOpt, options);
                numOpt++;
                i++;
                continue;
            }
        }

        if (numOpt < options->numRequired)
        {
            if (options->numRequired == 1)
            {
                printf("Missing %d required argument \n\n", options->numRequired);
            }
            else
            {
                printf("Missing %d required arguments \n\n", options->numRequired);
            }
            printUsage(options);
            exit(EXIT_FAILURE);
        }

        // check if the options is valid
        valid = 0;
        if (longswitch)
        {
            for (j = 0; j < NUMOPTIONS; j++)
            {
                if (strncmp(options->LongOptionAliases[j], &argv[i][2], MAXOPTIONLEN) == 0)
                {
                    valid = 1;
                    snprintf(optbuf, sizeof(optbuf), "%s",
                        options->OptionArray[j]);
                    break;
                }
            }
        }
        else
        {
            for (j = 0; j < NUMOPTIONS; j++)
            {
                if (strncmp(options->OptionArray[j], &argv[i][1], MAXOPTIONLEN) == 0)
                {
                    valid = 1;
                    snprintf(optbuf, sizeof(optbuf), "%s", &argv[i][1]);
                    break;
                }
            }
        }

        if (valid == 0)
        {
            fprintf(stderr, "invalid option: %s\n", argv[i]);
            printUsage(options);
            exit(EXIT_FAILURE);
        }

        //check to see if this option requires an argument
        if (options->needsArg[j] == 1)
        {
            i++;
            if ((i == argc) || (argv[i][0] == '-' &&
                !isdigit((unsigned char)argv[i][1]) && argv[i][1] != '.'))
            {
                printf("argument expected for %s%s\n\n", longswitch ? "--" : "-", optbuf);
                printUsage(options);
                exit(EXIT_FAILURE);
            }
            if (std::string_view(optbuf) == "e" && numOpt++ >= options->numArguments)
                invalid_argument("input expression");
            applyOpt(optbuf, argv[i], options);
            k++;
        }
        else if (options->needsArg[j] == 2)
        {
            // check to see if an argument was supplied
            if (((i + 1) == argc) || argv[i + 1][0] == '-')
            {
                // no option supplied.  use default option
                applyOpt(optbuf, NULL, options);
                k++;
            }
            else
            {
                i++;
                // an option was supplied, pass it on
                applyOpt(optbuf, argv[i], options);
                k++;
            }
        }
        else
        {
            //apply -option
            //now apply -option argument
            applyOpt(optbuf, NULL, options);
            k++;
        }
        i++;
    }

    if (numOpt < options->numRequired)
    {
        if (options->numRequired == 1)
        {
            printf("Missing %d required argument \n\n", options->numRequired);
        }
        else
        {
            printf("Missing %d required arguments \n\n", options->numRequired);
        }
        printUsage(options);
        exit(EXIT_FAILURE);
    }

    return k;
}

// ========================================================================
// this function should not need to be changed:
// edit OptionHelp and usageHelp strings.
// ========================================================================
void printUsage(options_t* options)
{
    int j;
    printf("Usage: %s\n", usageHelp);
    printf("supported options are: \n");
    for (j = 0; j < NUMOPTIONS; j++)
    {
        int n;
        if (strlen(options->LongOptionAliases[j]) > 0)
        {
            if (options->needsArg[j])
            {
                n = printf("%s <value>",
                    options->OptionArray[j]);
                for (; n < MAXOPTIONLEN + 8; n++)
                    printf(" ");
                printf(": %s (alias --%s)\n",
                    options->OptionHelp[j],
                    options->LongOptionAliases[j]);
            }
            else
            {
                n = printf("%s        ",
                    options->OptionArray[j]);
                for (; n < MAXOPTIONLEN + 8; n++)
                    printf(" ");
                printf(": %s (alias --%s)\n",
                    options->OptionHelp[j],
                    options->LongOptionAliases[j]);
            }
        }
        else
        {
            if (options->needsArg[j])
            {
                n = printf("%s <value>",
                    options->OptionArray[j]);
                for (; n < MAXOPTIONLEN + 8; n++)
                    printf(" ");
                printf(": %s\n", options->OptionHelp[j]);
            }
            else
            {
                n = printf("%s        ",
                    options->OptionArray[j]);
                for (; n < MAXOPTIONLEN + 8; n++)
                    printf(" ");
                printf(": %s\n", options->OptionHelp[j]);
            }
        }
    }
    return;
}

// ========================================================================
// this function should not need to be changed:
// parse options from a .ini (or other) file.
// ========================================================================
/* 这个名字是第几个选项；不认识返回 -1。 */
static int option_index(const char* key)
{
    int i;
    for (i = 0; i < NUMOPTIONS; i++)
    {
        if (strcmp(OptionArray[i], key) == 0)
            return i;
    }
    return -1;
}

int readINI(const char* filename, options_t* options)
{
    FILE* doc;
    char str[1024];
    char* key;
    char* value;
    char* end;

    doc = fopen(filename, "r");

    if (doc == NULL)
    {
        printf("warning: could not open %s, no options parsed\n", filename);
        return 0;
    }

    while (fgets(str, sizeof(str), doc) != NULL)
    {
        int opt_index;

        if (strchr(str, '\n') == NULL && !feof(doc))
        {
            int next = fgetc(doc);
            if (next != EOF && next != '\n')
            {
                fclose(doc);
                invalid_argument("ini line length");
            }
        }
        key = str;
        while (isspace((unsigned char)*key))
            key++;
        if (*key == '%' || *key == '#' || *key == '\0')
            continue;
        end = key + strlen(key);
        while (end > key && isspace((unsigned char)end[-1]))
            *--end = '\0';

        /* 只拆第一个等号，路径或表达式内的等号保留。 */
        value = strchr(key, '=');
        if (value != NULL)
        {
            *value++ = '\0';
            end = value - 1;
            while (end > key && isspace((unsigned char)end[-1]))
                *--end = '\0';
            while (isspace((unsigned char)*value))
                value++;
        }

        /* ini 是手改的配置文件，一个拼错的键、漏了等号、或者一行太长，
           都不该让整个工具起不来。命令行的处理不一样：给错选项就该退出，
           所以在这里先把有问题的行挑出来。 */
        opt_index = option_index(key);
        if (opt_index < 0)
        {
            fprintf(stderr, "warning: ignoring unknown option '%s' in %s\n",
                key, filename);
            continue;
        }
        if (value != NULL && strlen(value) >= MAXARGLEN && opt_index != 68)
        {
            fprintf(stderr, "warning: argument of option '%s' in %s is too "
                "long, ignoring\n", key, filename);
            continue;
        }

        applyOpt(key, value, options);
    }
    if (ferror(doc))
    {
        fclose(doc);
        fprintf(stderr, "error reading options from %s\n", filename);
        return 0;
    }
    fclose(doc);
    return 1;
}
