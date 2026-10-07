#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "factor/nfs/lasieve/kernels/include/siever-config.h"
#include "factor/nfs/lasieve/include/if.h"

int main(void)
{
    char payload[162];
    char *formatted = NULL;
    int result;

    memset(payload, 'x', sizeof(payload) - 1);
    payload[sizeof(payload) - 1] = '\0';
    result = asprintf(&formatted, "prefix:%s:suffix", payload);
    assert(result == 175);
    assert(strlen(formatted) == 175);
    assert(strncmp(formatted, "prefix:", 7) == 0);
    assert(strcmp(formatted + 168, ":suffix") == 0);
    free(formatted);
    return 0;
}
