#include <pmm.h>
#include <list.h>
#include <best_fit_pmm.h>
#include <stdio.h>

//lab2 function2-2 2413404
static free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void best_fit_check(void);
static void best_fit_run_checks(void);

static void
best_fit_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

/* Keep blocks in address order so only the two neighbours can coalesce. */
static void
best_fit_insert(struct Page *base) {
    list_entry_t *next = list_next(&free_list);
    while (next != &free_list && le2page(next, page_link) < base) {
        next = list_next(next);
    }
    list_add_before(next, &base->page_link);

    list_entry_t *prev = list_prev(&base->page_link);
    if (prev != &free_list) {
        struct Page *p = le2page(prev, page_link);
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            base->property = 0;
            list_del(&base->page_link);
            base = p;
        }
    }
    next = list_next(&base->page_link);
    if (next != &free_list) {
        struct Page *p = le2page(next, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            p->property = 0;
            list_del(&p->page_link);
        }
    }
}

static void
best_fit_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    for (struct Page *p = base; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    best_fit_insert(base);
}

static struct Page *
best_fit_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *best = NULL;
    for (list_entry_t *le = list_next(&free_list); le != &free_list;
         le = list_next(le)) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n &&
            (best == NULL || p->property < best->property)) {
            best = p;
            if (p->property == n) {
                break;
            }
        }
    }
    /* Enough total free pages does not imply a large enough contiguous block. */
    if (best == NULL) {
        return NULL;
    }
    size_t remaining = best->property - n;
    list_entry_t *prev = list_prev(&best->page_link);
    list_del(&best->page_link);
    if (remaining != 0) {
        struct Page *tail = best + n;
        tail->property = remaining;
        SetPageProperty(tail);
        list_add(prev, &tail->page_link);
    }
    ClearPageProperty(best);
    best->property = 0;
    nr_free -= n;
    return best;
}

static void
best_fit_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    for (struct Page *p = base; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    best_fit_insert(base);
}

static size_t
best_fit_nr_free_pages(void) {
    return nr_free;
}

const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_run_checks,
};

// Framework tests: students must not modify basic_check or best_fit_check.
static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

static void
best_fit_check(void) {
    int score = 0, sumscore = 6;
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // * - - * -
    free_pages(p0 + 1, 2);
    free_pages(p0 + 4, 1);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 1) && p0[1].property == 2);
    // * - - * *
    assert((p1 = alloc_pages(1)) != NULL);
    assert(alloc_pages(2) != NULL);
    assert(p0 + 4 == p1);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    p2 = p0 + 1;
    free_pages(p0, 5);
    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        count--, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
}

/* Extra behavioural coverage; the framework's two tests above stay intact. */
static void
best_fit_run_checks(void) {
    best_fit_check();
    cprintf("[Best-Fit] framework checks: PASS\n");

    size_t before = nr_free_pages();
    struct Page *region = alloc_pages(24);
    assert(region != NULL);
    list_entry_t saved_list = free_list;
    unsigned int saved_count = nr_free;
    list_init(&free_list);
    nr_free = 0;

    /* Allocated gaps separate blocks of 8, 4 and 4 pages. */
    free_pages(region, 8);
    free_pages(region + 10, 4);
    free_pages(region + 16, 4);
    assert(nr_free == 16);
    assert(alloc_pages(9) == NULL && nr_free == 16);
    cprintf("[Best-Fit] fragmented allocation failure: PASS\n");

    struct Page *small = alloc_pages(3);
    assert(small == region + 10);  /* minimum size, then lowest address */
    assert(PageProperty(region + 13) && region[13].property == 1);
    assert(nr_free == 13);
    free_pages(small, 3);
    assert(region[10].property == 4 && nr_free == 16);
    cprintf("[Best-Fit] smallest block, tie and split: PASS\n");

    assert(alloc_pages(4) == region + 10);
    assert(alloc_pages(4) == region + 16);
    assert(alloc_pages(8) == region);
    assert(nr_free == 0 && list_empty(&free_list));
    assert(alloc_page() == NULL);
    cprintf("[Best-Fit] exact fit and exhaustion: PASS\n");

    free_pages(region, 8);
    free_pages(region + 10, 4);
    free_pages(region + 8, 2);  /* bridges both neighbours */
    assert(PageProperty(region) && region->property == 14);
    assert(!PageProperty(region + 8) && !PageProperty(region + 10));
    free_pages(region + 16, 4);
    free_pages(region + 14, 2);
    free_pages(region + 20, 4);
    assert(nr_free == 24 && region->property == 24);
    assert(alloc_pages(24) == region);
    assert(nr_free == 0 && list_empty(&free_list));
    cprintf("[Best-Fit] two-sided coalescing: PASS\n");

    free_list = saved_list;
    nr_free = saved_count;
    free_pages(region, 24);
    assert(nr_free_pages() == before);
    cprintf("[Best-Fit] allocator state restored: PASS\n");
}
