#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_system_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// Absolute page number of the managed physical memory's exclusive upper bound.
size_t npage = 0;
// Kernel virtual address minus physical address (fixed boot mapping).
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
#ifdef PMM_BUDDY
    pmm_manager = &buddy_system_pmm_manager;
#elif defined(PMM_BEST_FIT)
    pmm_manager = &best_fit_pmm_manager;
#else
    pmm_manager = &default_pmm_manager;
#endif
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2413603
static void page_init(void) {
    extern char end[];
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();

    // nbase and the boot page table assume that RAM starts at DRAM_BASE.
    // Reject invalid DTB data before doing address arithmetic or touching RAM.
    if (mem_base != DRAM_BASE || mem_size == 0 ||
        mem_size > (uint64_t)-1 - mem_base) {
        panic("page_init: invalid DTB memory range");
    }
    uintptr_t detected_end = mem_base + mem_size;
    uintptr_t managed_end = detected_end;
    if (managed_end > PHYSICAL_MEMORY_END) {
        managed_end = PHYSICAL_MEMORY_END;
    }
    managed_end = ROUNDDOWN(managed_end, PGSIZE);
    if (managed_end <= KERNEL_BEGIN_PADDR) {
        panic("page_init: memory does not cover the kernel");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    npage = PPN(managed_end);
    // pages[0] describes DRAM_BASE, not physical address zero.
    uintptr_t descriptors_pa = PADDR(ROUNDUP((uintptr_t)end, PGSIZE));
    size_t descriptor_bytes = (npage - nbase) * sizeof(struct Page);
    if (descriptors_pa < KERNEL_BEGIN_PADDR ||
        descriptors_pa >= managed_end ||
        descriptor_bytes > managed_end - descriptors_pa) {
        panic("page_init: no room for page descriptors");
    }
    pages = (struct Page *)(descriptors_pa + va_pa_offset);

    // Reserve everything first; only the explicitly registered free range
    // below may be released by the selected allocator's init_memmap().
    for (size_t i = 0; i < npage - nbase; i++) {
        pages[i].flags = 0;
        SetPageReserved(&pages[i]);
        pages[i].property = 0;
        set_page_ref(&pages[i], 0);
        list_init(&pages[i].page_link);
    }

    uintptr_t free_begin = ROUNDUP(descriptors_pa + descriptor_bytes, PGSIZE);
    cprintf("physical memory: [0x%016lx, 0x%016lx)\n", mem_base, detected_end);
    cprintf("managed memory:  [0x%016lx, 0x%016lx)\n", mem_base, managed_end);
    cprintf("page descriptors: [0x%016lx, 0x%016lx)\n",
            descriptors_pa, descriptors_pa + descriptor_bytes);
    cprintf("free memory:     [0x%016lx, 0x%016lx), %lu pages\n",
            free_begin, managed_end, (managed_end - free_begin) / PGSIZE);
    if (free_begin < managed_end) {
        init_memmap(pa2page(free_begin), (managed_end - free_begin) / PGSIZE);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
