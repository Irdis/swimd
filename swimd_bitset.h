#ifndef SWIMD_BITSET_H
#define SWIMD_BITSET_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    uint64_t *bits;
    int size;
    int bits_length;
} SwimdBitset;

void swimd_bitset_init(SwimdBitset *set, int size);
bool swimd_bitset_get(SwimdBitset *set, int i);
void swimd_bitset_set(SwimdBitset *set, int i, bool val);
void swimd_bitset_free(SwimdBitset *set);

#endif // SWIMD_BITSET_H
