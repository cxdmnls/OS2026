#include <slub.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>

/* Separate tests exercise public object allocation, actual contents and PMM
 * recovery, rather than peeking at the allocator's private bitmap. */
void slub_check(void) {
    size_t before = nr_free_pages();
    assert(kmalloc(0) == NULL);
    kfree(NULL);
    assert(kmalloc((size_t)-1) == NULL);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] zero/null/oversized: PASS\n");

    const size_t sizes[] = {1, 15, 16, 17, 31, 32, 33, 63, 64, 65,
        127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024,
        1025, 2047, 2048, 2049, 4096, 6000, 8192};
    void *objects[sizeof(sizes) / sizeof(sizes[0])];
    size_t count = sizeof(sizes) / sizeof(sizes[0]);
    for (size_t i = 0; i < count; i++) {
        objects[i] = kmalloc(sizes[i]);
        assert(objects[i] != NULL && (uintptr_t)objects[i] % 16 == 0);
        for (size_t j = 0; j < i; j++) {
            uintptr_t a = (uintptr_t)objects[i], b = (uintptr_t)objects[j];
            assert(a + sizes[i] <= b || b + sizes[j] <= a);
        }
        memset(objects[i], (int)(i + 1), sizes[i]);
    }
    for (size_t i = 0; i < count; i++) {
        unsigned char *p = objects[i];
        for (size_t j = 0; j < sizes[i]; j++) assert(p[j] == i + 1);
    }
    for (size_t i = count; i > 0; i--) kfree(objects[i - 1]);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] size boundaries/alignment/nonoverlap/data: PASS\n");

    void *small[300];
    for (size_t i = 0; i < 300; i++) {
        small[i] = kmalloc(16);
        assert(small[i] != NULL);
        memset(small[i], (int)(i & 255), 16);
    }
    /* 300 objects must cross a slab boundary. */
    assert(before - nr_free_pages() >= 2);
    for (size_t i = 0; i < 300; i++)
        for (size_t j = 0; j < 16; j++)
            assert(((unsigned char *)small[i])[j] == (i & 255));
    kfree(small[0]);
    small[0] = kmalloc(16);
    assert(small[0] != NULL);
    for (size_t i = 0; i < 300; i += 2) kfree(small[i]);
    for (size_t i = 1; i < 300; i += 2) kfree(small[i]);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] multi-slab/reuse/page-reclaim: PASS\n");

    /* Valid object request larger than all remaining pages: true OOM. */
    size_t impossible = nr_free_pages() * PGSIZE;
    assert(kmalloc(impossible) == NULL);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] out-of-memory/state-recovery: PASS\n");
    cprintf("[SLUB] all checks: PASS\n");
}
