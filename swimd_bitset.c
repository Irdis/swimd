#include <string.h>
#include "swimd_bitset.h"

#define SWIMD_BITSET_WORD_BITS 64
#define SWIMD_BITSET_CEIL_DIV(a,b) ((a) % (b) == 0 ? ((a)/(b)) : ((a)/(b)) + 1)

void swimd_bitset_init(SwimdBitset *set, int size) {
    set->size = size;
    set->bits_length = SWIMD_BITSET_CEIL_DIV(size, SWIMD_BITSET_WORD_BITS);
    set->bits = malloc(set->bits_length * sizeof(uint64_t));
    memset(set->bits, 0, set->bits_length * sizeof(uint64_t));
}

bool swimd_bitset_get(SwimdBitset *set, int i) {
    return (
            set->bits[i / SWIMD_BITSET_WORD_BITS] >>
            (i % SWIMD_BITSET_WORD_BITS)
        ) & 1;
}

void swimd_bitset_set(SwimdBitset *set, int i, bool val) {
    uint64_t mask = UINT64_C(1) << (i % SWIMD_BITSET_WORD_BITS);

    if (val)
        set->bits[i / SWIMD_BITSET_WORD_BITS] |= mask;
    else
        set->bits[i / SWIMD_BITSET_WORD_BITS] &= ~mask;

}

void swimd_bitset_free(SwimdBitset *set) {
    free(set->bits);
}

