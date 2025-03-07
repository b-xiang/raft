#include <stdlib.h>

#include "fault.h"
#include "heap.h"
#include "munit.h"

struct heap
{
    int n;                   /* Number of outstanding allocations. */
    struct test_fault fault; /* Fault trigger. */
};

static void heapInit(struct heap *h)
{
    h->n = 0;
    test_fault_init(&h->fault);
}

static void *heapMalloc(void *data, size_t size)
{
    struct heap *h = data;
    if (test_fault_tick(&h->fault)) {
        return NULL;
    }
    h->n++;
    return munit_malloc(size);
}

static void heapFree(void *data, void *ptr)
{
    struct heap *h = data;
    h->n--;
    free(ptr);
}

static void *heapCalloc(void *data, size_t nmemb, size_t size)
{
    struct heap *h = data;
    if (test_fault_tick(&h->fault)) {
        return NULL;
    }
    h->n++;
    return munit_calloc(nmemb, size);
}

static void *heapRealloc(void *data, void *ptr, size_t size)
{
    struct heap *h = data;

    if (test_fault_tick(&h->fault)) {
        return NULL;
    }

    /* Increase the number of allocation only if ptr is NULL, since otherwise
     * realloc is a malloc plus a free. */
    if (ptr == NULL) {
        h->n++;
    }

    ptr = realloc(ptr, size);

    if (size == 0) {
        munit_assert_ptr_null(ptr);
    } else {
        munit_assert_ptr_not_null(ptr);
    }

    return ptr;
}

static void *heapAlignedAlloc(void *data, size_t alignment, size_t size)
{
    struct heap *h = data;
    void *p;

    if (test_fault_tick(&h->fault)) {
        return NULL;
    }

    h->n++;

    p = aligned_alloc(alignment, size);
    munit_assert_ptr_not_null(p);

    return p;
}

void test_heap_setup(const MunitParameter params[], struct raft_heap *h)
{
    struct heap *heap = munit_malloc(sizeof *heap);
    const char *delay = munit_parameters_get(params, TEST_HEAP_FAULT_DELAY);
    const char *repeat = munit_parameters_get(params, TEST_HEAP_FAULT_REPEAT);

    munit_assert_ptr_not_null(h);

    heapInit(heap);

    if (delay != NULL) {
        heap->fault.countdown = atoi(delay);
    }
    if (repeat != NULL) {
        heap->fault.n = atoi(repeat);
    }

    h->data = heap;
    h->malloc = heapMalloc;
    h->free = heapFree;
    h->calloc = heapCalloc;
    h->realloc = heapRealloc;
    h->aligned_alloc = heapAlignedAlloc;

    raft_heap_set(h);
    test_fault_pause(&heap->fault);
}

void test_heap_tear_down(struct raft_heap *h)
{
    struct heap *heap = h->data;
    if (heap->n != 0) {
      //munit_errorf("memory leak: %d outstanding allocations", heap->n);
    }
    free(heap);
    raft_heap_set_default();
}

void test_heap_fault_config(struct raft_heap *h, int delay, int repeat)
{
    struct heap *heap = h->data;
    test_fault_config(&heap->fault, delay, repeat);
}

void test_heap_fault_enable(struct raft_heap *h)
{
    struct heap *heap = h->data;
    test_fault_resume(&heap->fault);
}
