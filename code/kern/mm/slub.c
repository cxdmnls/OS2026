#include <slub.h>
#include <pmm.h>
#include <list.h>
#include <string.h>

/* Educational SLUB-style allocator: one-page slabs, size-class caches,
 * embedded metadata and a bitmap. No per-CPU caches or concurrency support. */
#define SLAB_MAGIC 0x534c55424c414232ULL
#define NCACHE 8
#define LARGE_CACHE ((size_t)-1)
#define HEADER_SIZE ROUNDUP(sizeof(struct slab), 16)

struct slab {
    uint64_t magic;
    list_entry_t link;
    size_t cache, npages, capacity, inuse;
    uint64_t used[4];
};

static list_entry_t caches[NCACHE];
static bool initialized;

void slub_init(void) {
    assert(!initialized);
    for (size_t i = 0; i < NCACHE; i++) list_init(&caches[i]);
    initialized = 1;
}

static struct slab *new_slab(size_t cache, size_t npages) {
    struct Page *p = alloc_pages(npages);
    if (p == NULL) return NULL;
    struct slab *s = (struct slab *)(page2pa(p) + va_pa_offset);
    memset(s, 0, HEADER_SIZE);
    s->magic = SLAB_MAGIC;
    s->cache = cache;
    s->npages = npages;  /* Keep the requested count for the Buddy interface. */
    list_init(&s->link);
    return s;
}

void *kmalloc(size_t size) {
    assert(initialized);
    if (size == 0) return NULL;
    if (size > 2048) {
        /* Bound arithmetic before rounding; failure must preserve PMM state. */
        if (size > PHYSICAL_MEMORY_END - DRAM_BASE - HEADER_SIZE) return NULL;
        size_t n = ROUNDUP(size + HEADER_SIZE, PGSIZE) / PGSIZE;
        struct slab *s = new_slab(LARGE_CACHE, n);
        if (s == NULL) return NULL;
        s->inuse = 1;
        return (char *)s + HEADER_SIZE;
    }
    size_t cache = 0, slot_size = 16;
    while (slot_size < size) { cache++; slot_size <<= 1; }
    struct slab *s = NULL;
    for (list_entry_t *le = list_next(&caches[cache]); le != &caches[cache];
         le = list_next(le)) {
        struct slab *candidate = to_struct(le, struct slab, link);
        if (candidate->inuse < candidate->capacity) { s = candidate; break; }
    }
    if (s == NULL) {
        s = new_slab(cache, 1);
        if (s == NULL) return NULL;
        s->capacity = (PGSIZE - HEADER_SIZE) / slot_size;
        assert(s->capacity > 0 && s->capacity <= 256);
        list_add(&caches[cache], &s->link);
    }
    for (size_t i = 0; i < s->capacity; i++) {
        uint64_t bit = 1ULL << (i % 64);
        if (!(s->used[i / 64] & bit)) {
            s->used[i / 64] |= bit;
            s->inuse++;
            return (char *)s + HEADER_SIZE + i * slot_size;
        }
    }
    panic("slub: inconsistent bitmap");
}

void kfree(void *ptr) {
    if (ptr == NULL) return;
    assert(initialized);
    uintptr_t base = ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    struct slab *s = (struct slab *)base;
    assert(s->magic == SLAB_MAGIC && s->inuse > 0);
    if (s->cache == LARGE_CACHE) {
        assert((char *)ptr == (char *)s + HEADER_SIZE);
    } else {
        assert(s->cache < NCACHE);
        size_t slot_size = (size_t)16 << s->cache;
        uintptr_t offset = (uintptr_t)ptr - base - HEADER_SIZE;
        size_t index = offset / slot_size;
        assert(offset % slot_size == 0 && index < s->capacity);
        uint64_t bit = 1ULL << (index % 64);
        assert(s->used[index / 64] & bit);
        s->used[index / 64] &= ~bit;
    }
    if (--s->inuse == 0) {
        size_t n = s->npages;
        if (s->cache != LARGE_CACHE) list_del(&s->link);
        s->magic = 0;
        free_pages(pa2page(PADDR(base)), n);
    }
}
