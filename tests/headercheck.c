#include "loxseq/loxseq.h"

/* Back-compat shim header. */
#include "loxseq.h"

int main(void) {
    /* Ensure the types are visible and the headers are self-contained. */
    loxseq_t seq;
    (void)seq;
    return 0;
}

