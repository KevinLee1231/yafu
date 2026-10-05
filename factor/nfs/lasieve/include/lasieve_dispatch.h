#ifndef YAFU_LASIEVE_DISPATCH_H
#define YAFU_LASIEVE_DISPATCH_H

#ifdef __cplusplus
extern "C" {  /* yafu-cpp-linkage */
#endif

/* Run the in-process lattice siever for one I value (11..16).
 * Defined in factor/nfs/lasieve/lasieve_dispatch.c, which is still compiled
 * as C; callers that are already C++ must pick this declaration up so the
 * link name matches.  tune.c used to declare it locally, which in C++ meant
 * a mangled call to an unmangled definition.
 */
int lasieve_run(int I, int argc, char **argv);

#ifdef __cplusplus
}  /* yafu-cpp-linkage */
#endif

#endif /* YAFU_LASIEVE_DISPATCH_H */
