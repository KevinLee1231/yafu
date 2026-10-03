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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>  // isdigit, isspace
#include <errno.h>
#include <limits.h>
#include <math.h>

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
    "qssave", "siqsB", "siqsTF", "siqsR", "siqsT", 
    "siqsNB", "siqsM", "logfile", "batchfile", "seed", 
    "session", "threads", "v", "silent", "forceDLP",
    "noopt", "vproc", "nprp", "aprcl_p", "aprcl_d",
    "repeat", "siqsTFSm", "script", "forceTLP", "siqsLPB", 
    "siqsMFBD", "siqsMFBT", "siqsBDiv", "siqsBT", "siqsNobat", 
    "inmem" };

// help strings displayed with -h
// needs to be the same length as the above arrays, even if 
// some are left blank
char OptionHelp[NUMOPTIONS][MAXHELPLEN] = {
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
    "(String)          : Name of session logfile",
    "(Integer < 32-bit): Number of threads",
    "                  : Increase verbosity",
    "                  : Set verbosity to -1 (no output)", 
    "                  : Force use of DLP variation in SIQS",
    "                  : Do not optimize small prime variation cutoff in SIQS", 
    "                  : Verbose output of processor info",
    "(Integer < 32-bit): Number of witnesses to use in Miller-Rabin PRP tests",
    "(Integer < 32-bit): setting the threshold below which numbers are proved prime using APR-CL",
    "(Integer < 32-bit): setting the threshold above which numbers that are proved prime using APR-CL have additional verbosity",
    "(Integer < 32-bit): Repeat input command line specified number of times",
    "(Integer < 32-bit): SIQS small prime variation trial factoring bound",
    "(String)          : Filename of script to execute", 
    "                  : Force use of TLP variation in SIQS",
    "(Integer < 32-bit): SIQS large prime bound, in bits (2^bits)",
    "(Floating point)  : Exponent of SIQS DLP: attempt to split residues up to LPB^exponent",
    "(Floating point)  : Exponent of SIQS TLP: attempt to split residues up to LPB^exponent",
    "(Floating point)  : SIQS Batch Divisor: specifies max prime to include in batch GCD as divisor of max factorbase prime",
    "(Integer < 32-bit): SIQS Batch Target: how many residues to batch before doing batch GCD", 
    "                  : Do not use SIQS Batch GCD",
    "(Integer < 32-bit): Digit level below which SIQS in-memory is used (no savefile)" };

// indication of whether or not an option needs a corresponding argument.
// needs to be the same length as the above two arrays.
// 0 = no argument
// 1 = argument required
// 2 = argument optional
int needsArg[NUMOPTIONS] = {
    1,1,1,1,1,
    1,1,1,1,1,
    1,1,0,0,0,
    0,0,1,1,1,
    1,1,1,0,1,
    1,1,1,1,0,
    1};

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
    "" };

// ========================================================================
static void invalid_argument(const char *opt)
{
    fprintf(stderr, "invalid or out-of-range argument for option %s\n", opt);
    exit(EXIT_FAILURE);
}

/* 转换前后都检查，避免空值、部分解析和窄整数截断。 */
static uint64_t parse_uint(const char *arg, const char *opt, uint64_t limit)
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

static double parse_double(const char *arg, const char *opt)
{
    char *end;
    double value;
    if (arg == NULL || *arg == '\0' || isspace((unsigned char)*arg))
        invalid_argument(opt);
    errno = 0;
    value = strtod(arg, &end);
    if (errno == ERANGE || end == arg || *end != '\0' ||
        !isfinite(value) || value < 0)
        invalid_argument(opt);
    return value;
}

static void copy_argument(char *dst, size_t dst_size, const char *arg,
    const char *opt)
{
    if (arg == NULL || strlen(arg) >= dst_size)
        invalid_argument(opt);
    memcpy(dst, arg, strlen(arg) + 1);
}


// ========================================================================
// modify this function to handle input arguments
// ========================================================================
void applyArg(char* arg, int argNum, options_t* options)
{
    if (argNum == 0)
    {
        char *new_expr = (char*)realloc(options->inputExpr,
            (strlen(arg) + 1) * sizeof(char));
        if (new_expr == NULL)
        {
            fprintf(stderr, "out of memory while copying input expression\n");
            exit(EXIT_FAILURE);
        }
        options->inputExpr = new_expr;
        strcpy(options->inputExpr, arg);
    }

    return;
}

// ========================================================================
// modify this function to handle each option case, assigning values
// or arguments to the members of the options_t structure.
// ========================================================================
void applyOpt(char* opt, char* arg, options_t* options)
{
    int i;

    if (strcmp(opt, OptionArray[0]) == 0)
    {
        // qssave
        copy_argument(options->qssave, sizeof(options->qssave), arg, opt);
    }
    else if (strcmp(opt, OptionArray[1]) == 0)
    {
        // siqsB
        options->siqsB = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[2]) == 0)
    {
        // siqsTF
        options->siqsTF = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[3]) == 0)
    {
        // siqsR
        options->siqsR = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[4]) == 0)
    {
        // siqsT
        options->siqsT = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[5]) == 0)
    {
        // siqsNB
        options->siqsNB = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[6]) == 0)
    {
        // siqsM
        options->siqsM = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[7]) == 0)
    {
        // logfile
        // argument is a string
        if ((strlen(arg) == 0) || (strcmp(arg, "NUL") == 0) ||
            (strcmp(arg, "NULL") == 0) || (strcmp(arg, "nul") == 0) ||
            (strcmp(arg, "null") == 0))
        {
            strcpy(options->factorlog, "");
        }
        else
        {
            copy_argument(options->factorlog, sizeof(options->factorlog), arg, opt);
        }
    }
    else if (strcmp(opt, OptionArray[8]) == 0)
    {
        // batchfile
        // argument is a string
        copy_argument(options->batchfile, sizeof(options->batchfile), arg, opt);
    }
    else if (strcmp(opt, OptionArray[9]) == 0)
    {
        printf("attempting to parse user rng seed from %s\n", arg);
        options->rand_seed = parse_uint(arg, opt, UINT64_MAX);
        printf("read: % "PRIu64"\n", options->rand_seed);
    }
    else if (strcmp(opt, OptionArray[10]) == 0)
    {
        // sessionlog
        // argument is a string
        copy_argument(options->sessionlog, sizeof(options->sessionlog), arg, opt);
    }
    else if (strcmp(opt, OptionArray[11]) == 0)
    {
        options->threads = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
        if (options->threads == 0)
            invalid_argument(opt);
    }
    else if (strcmp(opt, OptionArray[12]) == 0)
    {
        options->verbosity++;
    }
    else if (strcmp(opt, OptionArray[13]) == 0)
    {
        options->verbosity = -1;
    }
        else if (strcmp(opt, OptionArray[14]) == 0)
    {
        options->siqsForceDLP = 1;
    }
    else if (strcmp(opt, OptionArray[15]) == 0)
    {
        options->no_opt = 1;
    }
    else if (strcmp(opt, OptionArray[16]) == 0)
    {
        options->vproc++;
    }
    else if (strcmp(opt, OptionArray[17]) == 0)
    {
        //argument "nprp"
        options->num_prp_witnesses = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[18]) == 0)
    {
        // argument "aprcl_p", setting the threshold below which numbers
        // are proved prime using APR-CL
        options->aprcl_p = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
        if (options->aprcl_p > 6021)
        {
            printf("APR-CL primality proving is possible only for numbers less"
                " than 6021 digits... setting limit to 6021 digits\n");
            options->aprcl_p = 6021;
        }
    }
    else if (strcmp(opt, OptionArray[19]) == 0)
    {
        // argument "aprcl_d", setting the threshold above which numbers
        // that are proved prime using APR-CL have additional verbosity enabled
        options->aprcl_d = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[20]) == 0)			// NOTE: we skip -e because it is handled 
    {													// specially by process_flags
        //argument "repeat"
        options->repeat = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[21]) == 0)
    {
        //argument "siqsTFSm"
        options->siqsTFSm = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else if (strcmp(opt, OptionArray[22]) == 0)
    {
        //argument "script"
        copy_argument(options->scriptfile, sizeof(options->scriptfile), arg, opt);
    }
    else if (strcmp(opt, OptionArray[23]) == 0)
    {
        //argument "forceTLP"
        options->siqsForceTLP = 1;
    }
    else if (strcmp(opt, OptionArray[24]) == 0)
    {
        //argument "siqsLPB"
        // the maximum allowed large prime, in bits, in SIQS
        options->siqsLPB = (uint32_t)parse_uint(arg, opt, 31);
    }
    else if (strcmp(opt, OptionArray[25]) == 0)
    {
        //argument "siqsMFBD"
        // the exponent of the large prime bound such that residues larger than
        // lpb^siqsMFBD are subjected to double large prime factorization attempts
        options->siqsMFBD = parse_double(arg, opt);
    }
    else if (strcmp(opt, OptionArray[26]) == 0)
    {
        //argument "siqsMFBT"
        // the exponent of the large prime bound such that residues larger than
        // lpb^siqsMFBT are subjected to triple large prime factorization attempts
        options->siqsMFBT = parse_double(arg, opt);
    }
    else if (strcmp(opt, OptionArray[27]) == 0)
    {
        //argument "siqsBDiv"
        // The divider of large_prime_max as the upper bound for
        // primes to multiply when using batch GCD in TLP factorizations.
        //fobj->qs_obj.gbl_override_bdiv_flag = 1;
        options->siqsBDiv = parse_double(arg, opt);
    }
    else if (strcmp(opt, OptionArray[28]) == 0)
    {
        //argument "siqsBT" ("Batch Target")
        // How many relations to batch up before they are processed
        options->siqsBT = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
        if (options->siqsBT == 0)
            invalid_argument(opt);
    }
    else if (strcmp(opt, OptionArray[29]) == 0)
    {
        //argument "siqsNobat"
        // Whether or not to use batch factoring in 3LP
        options->siqsNobat = 1;
    }
    else if (strcmp(opt, OptionArray[30]) == 0)
    {
        // argument "inmem"
        // cutoff for processing in-memory
        options->inmem_cutoff = (uint32_t)parse_uint(arg, opt, UINT32_MAX);
    }
    else
    {
        int i;

        printf("invalid option %s\n", opt);
        printf("supported options are: ");
        for (i = 0; i < NUMOPTIONS; i++)
        {
            if (i % 5 == 0) printf("\n");
            printf("%s ", options->OptionArray[i]);
        }
        exit(0);
    }

    return;
}

// ========================================================================
// modify this function to handle assign a default value to each 
// member of the options_t structure, if desired.
// ========================================================================
options_t* initOpt(void)
{
    options_t* options = (options_t*)calloc(1, sizeof(options_t));
    int i;

    if (options == NULL)
    {
        fprintf(stderr, "out of memory while initializing options\n");
        exit(EXIT_FAILURE);
    }

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
    options->numRequired = 1;
    // ========================================================================


    // ========================================================================
    // default values assigned to optional arguments here
    options->inputExpr = (char*)malloc(MAXARGLEN * sizeof(char));
    if (options->inputExpr == NULL)
    {
        free(options);
        fprintf(stderr, "out of memory while initializing options\n");
        exit(EXIT_FAILURE);
    }
    strcpy(options->inputExpr, "");
    // ========================================================================


    // ========================================================================
    // default values assigned to switches here:
    
    // general options
    strcpy(options->factorlog, "factor.log");
    strcpy(options->batchfile, "");
    strcpy(options->sessionlog, "session.log");
    strcpy(options->scriptfile, "");
    options->rand_seed = 0;
    options->threads = 1;
    options->verbosity = 0;
    options->vproc = 0;
    strcpy(options->expr, "");
    options->repeat = 0;
  
    // prime finding options
    options->aprcl_p = 500;
    options->aprcl_d = 200;
    options->num_prp_witnesses = 1;

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
    options->siqsForceTLP = 0;
    options->siqsLPB = 0;
    options->siqsMFBD = 1.85;
    options->siqsMFBT = 2.9;
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
    char optbuf[MAXOPTIONLEN];
    char argbuf[MAXARGLEN];
    int numOpt = 0;

    //argument loop
    i = 1;
    while (i < argc)
    {
        longswitch = 0;

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
                exit(0);
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
            exit(0);
        }

        // check if the options is valid
        valid = 0;
        if (longswitch)
        {
            for (j = 0; j < NUMOPTIONS; j++)
            {
                if (options->LongOptionAliases[j][0] != '\0' &&
                    strcmp(options->LongOptionAliases[j], &argv[i][2]) == 0)
                {
                    valid = 1;
                    strcpy(optbuf, options->OptionArray[j]);
                    break;
                }
            }
        }
        else
        {
            for (j = 0; j < NUMOPTIONS; j++)
            {
                if (strcmp(options->OptionArray[j], &argv[i][1]) == 0)
                {
                    valid = 1;
                    strcpy(optbuf, &argv[i][1]);
                    break;
                }
            }
        }

        if (valid == 0)
        {
            printUsage(options);
            exit(0);
        }

        //check to see if this option requires an argument
        if (options->needsArg[j] == 1)
        {
            i++;
            if ((i == argc) || argv[i][0] == '-')
            {
                printf("argument expected for %s%s\n\n", longswitch ? "--" : "-", optbuf);
                printUsage(options);
                exit(0);
            }
            copy_argument(argbuf, sizeof(argbuf), argv[i], optbuf);

            //now apply -option argument
            applyOpt(optbuf, argbuf, options);
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
                copy_argument(argbuf, sizeof(argbuf), argv[i], optbuf);

                //now apply -option argument
                applyOpt(optbuf, argbuf, options);
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
        exit(0);
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
void readINI(const char* filename, options_t* options)
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
        return;
    }

    while (fgets(str, sizeof(str), doc) != NULL)
    {
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

        //apply the option... same routine command line options use
        applyOpt(key, value, options);
    }

    if (ferror(doc))
    {
        fclose(doc);
        fprintf(stderr, "error reading options from %s\n", filename);
        return;
    }
    fclose(doc);

    return;
}
