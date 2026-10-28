#ifndef SWIMD_HEAP_H
#define SWIMD_HEAP_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    int score;
    int index;
} SwimdHeapItem;

typedef struct  {
    SwimdHeapItem *arr;
    int size;
    int max_size;
} SwimdHeap;

void swimd_heap_init(SwimdHeap *scores_heap, int max_size);
void swimd_heap_free(SwimdHeap *scores_heap);
void swimd_heap_cut_head(SwimdHeap *scores_heap);
void swimd_heap_insert(SwimdHeap *scores_heap, SwimdHeapItem item);
void swimd_heap_merge(SwimdHeap *scores_heap1, SwimdHeap *scores_heap2);
void swimd_heap_merge_many(SwimdHeap *scores_heaps, int scores_heaps_count);

#endif // SWIMD_HEAP_H
