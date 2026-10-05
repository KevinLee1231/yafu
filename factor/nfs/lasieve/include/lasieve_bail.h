
#ifdef __cplusplus
extern "C" {  /* yafu-cpp-linkage */
#endif
/* lasieve_bail.h -- leaving the siever without leaving the process.
 *
 * The siever used to be a program, so its error paths and its main() could
 * exit().  It is now called from inside yafu, where exit() would take the
 * factoring run down with it, so those call sites call lasieve_bail() instead
 * and the status comes back from lasieve_run() as an ordinary return value.
 *
 * Declared here rather than in a generated header: the per-I -D renames every
 * name in ../I_SYMBOLS, and this one must stay as it is so all six I values
 * reach the same definition.
 */
#ifndef LASIEVE_BAIL_H

#define LASIEVE_BAIL_H

void lasieve_bail(int status);


#endif


#ifdef __cplusplus
}  /* yafu-cpp-linkage */
#endif