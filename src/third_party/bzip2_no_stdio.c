#include <stdlib.h>

// With BZ_NO_STDIO, upstream delegates impossible internal invariant failures
// to the embedding application. There is no recoverable decoder result at this
// boundary, so fail closed without formatting, logging, or filesystem access.
void bz_internal_error(int error_code)
{
    (void)error_code;
    abort();
}
