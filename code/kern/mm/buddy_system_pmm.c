#include <buddy_system_pmm.h>
#include <list.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>

//lab2 function2-3 2411834
/*
 * Each entry in free_area holds blocks with exactly 2^order pages.  Every
 * block has one head Page whose PG_property bit is set and whose property is
 * the block size in pages.
 *
 * Requests need not be powers of two.  alloc_pages(n) rounds n up to the next
 * power of two; free_pages(page, n) performs the same rounding, so callers
 * must release an allocation with the same n that they requested.
 */
#define BUDDY_MAX_ORDER 32

static free_area_t free_area[BUDDY_MAX_ORDER];
static unsigned int nr_free;

static list_entry_t *free_list(size_t order) {
    return &free_area[order].free_list;
}

static size_t block_size(size_t order) {
    return (size_t)1 << order;
}

static size_t request_order(size_t n) {
    size_t order = 0;
    size_t pages = 1;

    assert(n > 0);
    while (pages < n) {
        if (order == BUDDY_MAX_ORDER - 1) {
            return BUDDY_MAX_ORDER;
        }
        pages <<= 1;
        order++;
    }
    return order;
}

static bool block_in_bounds(struct Page *base, size_t count) {
    return base >= pages && base + count <= pages + (npage - nbase);
}

static bool block_aligned(struct Page *base, size_t count) {
    return (page2ppn(base) & (count - 1)) == 0;
}

static void add_free_block(struct Page *base, size_t order) {
    size_t count = block_size(order);

    assert(block_in_bounds(base, count));
    assert(block_aligned(base, count));
    assert(!PageReserved(base));

    base->property = count;
    SetPageProperty(base);
    free_area[order].nr_free += count;
    nr_free += count;
    list_add(free_list(order), &(base->page_link));
}

static void remove_free_block(struct Page *base, size_t order) {
    size_t count = block_size(order);

    assert(PageProperty(base));
    assert(base->property == count);
    assert(free_area[order].nr_free >= count);

    list_del(&(base->page_link));
    ClearPageProperty(base);
    base->property = 0;
    free_area[order].nr_free -= count;
    nr_free -= count;
}

static void buddy_init(void) {
    for (size_t order = 0; order < BUDDY_MAX_ORDER; order++) {
        list_init(free_list(order));
        free_area[order].nr_free = 0;
    }
    nr_free = 0;
}

/*
 * A free range may start at any page boundary and may contain an arbitrary
 * number of pages.  Carve it into naturally aligned power-of-two blocks and
 * register each block at its own order.
 */
static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    assert(block_in_bounds(base, n));

    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&(p->page_link));
    }

    struct Page *cursor = base;
    size_t remaining = n;
    while (remaining > 0) {
        size_t max_order = 0;
        while (max_order + 1 < BUDDY_MAX_ORDER &&
               block_size(max_order + 1) <= remaining &&
               block_aligned(cursor, block_size(max_order + 1))) {
            max_order++;
        }

        add_free_block(cursor, max_order);
        cursor += block_size(max_order);
        remaining -= block_size(max_order);
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    size_t order = request_order(n);
    if (order >= BUDDY_MAX_ORDER) {
        return NULL;
    }

    size_t higher = order;
    while (higher < BUDDY_MAX_ORDER &&
           list_empty(free_list(higher))) {
        higher++;
    }
    if (higher == BUDDY_MAX_ORDER) {
        return NULL;
    }

    struct Page *block =
        le2page(list_next(free_list(higher)), page_link);
    remove_free_block(block, higher);

    while (higher > order) {
        higher--;
        struct Page *buddy = block + block_size(higher);
        add_free_block(buddy, higher);
    }

    block->flags = 0;
    block->property = 0;
    set_page_ref(block, 0);
    return block;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    size_t order = request_order(n);
    assert(order < BUDDY_MAX_ORDER);
    size_t count = block_size(order);
    assert(block_in_bounds(base, count));
    assert(block_aligned(base, count));

    /*
     * The interface only passes the requested page count.  Therefore the
     * rounded block is reclaimed here, exactly matching alloc_pages(n).
     */
    for (struct Page *p = base; p != base + count; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&(p->page_link));
    }

    base->property = count;
    SetPageProperty(base);

    while (order + 1 < BUDDY_MAX_ORDER) {
        size_t current_pages = block_size(order);
        ppn_t buddy_ppn = page2ppn(base) ^ current_pages;
        if (buddy_ppn < nbase || buddy_ppn >= npage) {
            break;
        }

        struct Page *buddy = &pages[buddy_ppn - nbase];
        if (!PageProperty(buddy) || PageReserved(buddy) ||
            buddy->property != current_pages ||
            free_area[order].nr_free < current_pages) {
            break;
        }

        list_del(&(buddy->page_link));
        free_area[order].nr_free -= current_pages;
        nr_free -= current_pages;
        ClearPageProperty(buddy);
        buddy->property = 0;

        if (buddy < base) {
            ClearPageProperty(base);
            base = buddy;
        }
        order++;
        base->property = block_size(order);
        SetPageProperty(base);
    }

    free_area[order].nr_free += base->property;
    nr_free += base->property;
    list_add(free_list(order), &(base->page_link));
}

static size_t buddy_nr_free_pages(void) {
    return nr_free;
}

static void buddy_validate(void) {
    size_t total = 0;
    for (size_t order = 0; order < BUDDY_MAX_ORDER; order++) {
        size_t pages = block_size(order);
        size_t order_total = 0;
        list_entry_t *le = free_list(order);

        while ((le = list_next(le)) != free_list(order)) {
            struct Page *block = le2page(le, page_link);
            assert(PageProperty(block));
            assert(!PageReserved(block));
            assert(block->property == pages);
            assert(block_aligned(block, pages));
            assert(block_in_bounds(block, pages));
            order_total += pages;
        }
        assert(order_total == free_area[order].nr_free);
        total += order_total;
    }
    assert(total == nr_free);
}

static void buddy_capture_state(free_area_t saved[BUDDY_MAX_ORDER]) {
    for (size_t order = 0; order < BUDDY_MAX_ORDER; order++) {
        saved[order] = free_area[order];
    }
}

static void buddy_restore_state(free_area_t saved[BUDDY_MAX_ORDER]) {
    for (size_t order = 0; order < BUDDY_MAX_ORDER; order++) {
        free_area[order] = saved[order];
    }
}

/*
 * The checks use one 32-page region owned by this allocator.  A dedicated
 * local state makes split, merge, OOM, and non-power-of-two behavior
 * reproducible, independent of fragmentation in the rest of physical memory.
 */
//lab2 function2-3 tests 2411834
static void buddy_check(void) {
    buddy_validate();

    struct Page *region = alloc_pages(32);
    assert(region != NULL);

    free_area_t saved[BUDDY_MAX_ORDER];
    buddy_capture_state(saved);
    unsigned int saved_nr_free = nr_free;

    buddy_init();
    for (size_t i = 0; i < 32; i++) {
        SetPageReserved(&region[i]);
    }
    buddy_init_memmap(region, 32);
    assert(nr_free == 32);
    assert(PageProperty(region) && region->property == 32);
    buddy_validate();
    cprintf("[Buddy] init_memmap: PASS\n");

    struct Page *left = alloc_pages(8);
    struct Page *right = alloc_pages(8);
    assert(left == region);
    assert(right == region + 8);
    assert(block_aligned(left, 8));
    assert(block_aligned(right, 8));
    assert(nr_free == 16);
    cprintf("[Buddy] split: PASS\n");

    free_pages(left, 8);
    free_pages(right, 8);
    assert(nr_free == 32);
    assert(PageProperty(region) && region->property == 32);
    buddy_validate();
    cprintf("[Buddy] coalesce: PASS\n");

    struct Page *odd = alloc_pages(3);
    assert(odd == region);
    assert(block_aligned(odd, 4));
    assert(nr_free == 28);
    free_pages(odd, 3);
    assert(nr_free == 32);
    assert(PageProperty(region) && region->property == 32);
    buddy_validate();
    cprintf("[Buddy] non-power-of-two request: PASS\n");

    unsigned int before_oom = nr_free;
    assert(alloc_pages(before_oom + 1) == NULL);
    assert(nr_free == before_oom);
    cprintf("[Buddy] insufficient memory: PASS\n");

    buddy_validate();

    /*
     * Remove the local test metadata before restoring the real allocator
     * state.  The 32-page region was still allocated from the real allocator,
     * so returning it afterwards merges it back normally.
     */
    for (size_t i = 0; i < 32; i++) {
        region[i].flags = 0;
        region[i].property = 0;
        set_page_ref(&region[i], 0);
    }
    buddy_restore_state(saved);
    nr_free = saved_nr_free;
    free_pages(region, 32);
    buddy_validate();
}

const struct pmm_manager buddy_system_pmm_manager = {
    .name = "buddy_system_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
