#include <mmu_probe.h>
#include <pmm.h>
#include <stdio.h>

/* A dedicated data page makes the first load's TLB miss reproducible. */
volatile uint64_t mmu_probe_cell __attribute__((aligned(4096))) = 0x123456789abcdef0ULL;

void __attribute__((noinline)) mmu_probe(void) {
    uint64_t first, second;
    __asm__ volatile(
        "sfence.vma\n"
        ".global mmu_probe_load\n"
        "mmu_probe_load:\n"
        "ld %0, 0(%2)\n"
        "ld %1, 0(%2)\n"
        ".global mmu_probe_done\n"
        "mmu_probe_done:\n"
        : "=&r"(first), "=&r"(second)
        : "r"(&mmu_probe_cell) : "memory");
    assert(first == 0x123456789abcdef0ULL && second == first);
    cprintf("[MMU] two loads from 0x%016lx: PASS\n", &mmu_probe_cell);
}
