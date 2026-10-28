#include <string.h>
#include "swimd_heap.h"

#define SWIMD_HEAP_LEFT_HEAP(ind) (2*((ind) + 1) - 1)
#define SWIMD_HEAP_RIGHT_HEAP(ind) (2*((ind) + 1))
#define SWIMD_HEAP_PARENT_HEAP(ind) (((ind) - 1) / 2)
#define SWIMD_HEAP_SWAP(a, b, type) \
    do {                 \
        type temp = a;   \
        a = b;           \
        b = temp;        \
    } while (0)

void swimd_heap_init(SwimdHeap *scores_heap, int max_size) {
    scores_heap->arr = malloc(max_size * sizeof(SwimdHeapItem));
    scores_heap->size = 0;
    scores_heap->max_size = max_size;
}

void swimd_heap_free(SwimdHeap *scores_heap) {
    free(scores_heap->arr);
}

void swimd_heap_cut_head(SwimdHeap *scores_heap) {
    scores_heap->size--;
    scores_heap->arr[0] = scores_heap->arr[scores_heap->size];

    int ind = 0;
    while (1) {
        int left_ind = SWIMD_HEAP_LEFT_HEAP(ind);
        int right_ind = SWIMD_HEAP_RIGHT_HEAP(ind);
        int smallest = ind;
        if (left_ind < scores_heap->size &&
            scores_heap->arr[smallest].score > scores_heap->arr[left_ind].score) {
            smallest = left_ind;
        }

        if (right_ind < scores_heap->size &&
            scores_heap->arr[smallest].score > scores_heap->arr[right_ind].score) {
            smallest = right_ind;
        }

        if (smallest == ind)
            break;

        SWIMD_HEAP_SWAP(scores_heap->arr[ind],
                scores_heap->arr[smallest],
                SwimdHeapItem);
        ind = smallest;
    }
}

void swimd_heap_insert(SwimdHeap *scores_heap, SwimdHeapItem item) {
    if (scores_heap->size == 0) {
        scores_heap->size++;
        scores_heap->arr[0] = item;
        return;
    }
    if (scores_heap->size == scores_heap->max_size) {
        if (scores_heap->arr[0].score >= item.score)
        {
            return;
        }
        swimd_heap_cut_head(scores_heap);
    }
    scores_heap->arr[scores_heap->size] = item;
    int ind = scores_heap->size;
    scores_heap->size++;
    while (1) {
        if (ind <= 0)
            break;
        int parent_ind = SWIMD_HEAP_PARENT_HEAP(ind);
        if (scores_heap->arr[parent_ind].score > scores_heap-> arr[ind].score) {
            SWIMD_HEAP_SWAP(scores_heap->arr[parent_ind],
                    scores_heap-> arr[ind],
                    SwimdHeapItem);
            ind = parent_ind;
            continue;
        }
        break;
    }
}

void swimd_heap_merge(SwimdHeap *scores_heap1, SwimdHeap *scores_heap2) {
    for (int i = 0; i < scores_heap2->size; i++) {
        SwimdHeapItem item = scores_heap2->arr[i];
        swimd_heap_insert(scores_heap1, item);
    }
}

void swimd_heap_merge_many(SwimdHeap *scores_heaps, int scores_heaps_count) {
    SwimdHeap *heap_acc = &scores_heaps[0];
    for (int i = 1; i < scores_heaps_count; i++) {
        SwimdHeap *heap = &scores_heaps[i];
        swimd_heap_merge(heap_acc, heap);
    }
}
