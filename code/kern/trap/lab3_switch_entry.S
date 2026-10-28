#include <riscv.h>
#include <mmu.h>
#include <memlayout.h>

    .section .text
    .globl lab3_switch_enter_user
lab3_switch_enter_user:
    addi sp, sp, -16
    sd ra, 0(sp)
    la t0, lab3_switch_saved_kernel_sp
    sd sp, 0(t0)

    # Build a dedicated Sv39 subtree for:
    #   VA 0x0000000040000000 -> user code page
    #   VA 0x0000000040001000 -> user stack page
    la t0, lab3_switch_l1
    li t1, SV39_NENTRY
.Lclear_l1:
    sd zero, 0(t0)
    addi t0, t0, 8
    addi t1, t1, -1
    bnez t1, .Lclear_l1

    la t0, lab3_switch_l0
    li t1, SV39_NENTRY
.Lclear_l0:
    sd zero, 0(t0)
    addi t0, t0, 8
    addi t1, t1, -1
    bnez t1, .Lclear_l0

    # root[1] -> level-1 page table
    la t0, lab3_switch_l1
    li t1, PHYSICAL_MEMORY_OFFSET
    sub t0, t0, t1
    srli t0, t0, PGSHIFT
    slli t0, t0, 10
    ori t0, t0, PTE_V | PTE_U
    la t1, boot_page_table_sv39
    ld t2, 8(t1)
    la t3, lab3_switch_saved_root_pte
    sd t2, 0(t3)
    sd t0, 8(t1)

    # level-1[0] -> level-0 page table
    la t0, lab3_switch_l0
    li t1, PHYSICAL_MEMORY_OFFSET
    sub t0, t0, t1
    srli t0, t0, PGSHIFT
    slli t0, t0, 10
    ori t0, t0, PTE_V | PTE_U
    la t1, lab3_switch_l1
    sd t0, 0(t1)

    # Leaf mappings for the code and stack pages.
    li t2, PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D
    la t0, lab3_switch_user_code_page
    li t1, PHYSICAL_MEMORY_OFFSET
    sub t0, t0, t1
    srli t0, t0, PGSHIFT
    slli t0, t0, 10
    or t0, t0, t2
    la t1, lab3_switch_l0
    sd t0, 0(t1)

    la t0, lab3_switch_user_stack
    li t1, PHYSICAL_MEMORY_OFFSET
    sub t0, t0, t1
    srli t0, t0, PGSHIFT
    slli t0, t0, 10
    or t0, t0, t2
    la t1, lab3_switch_l0
    sd t0, 8(t1)
    sfence.vma zero, zero

    la t0, lab3_switch_test_phase
    li t1, 1
    sd t1, 0(t0)

    li t0, 0x40000000
    csrw sepc, t0

    # Clear SPP so sret enters U mode. SPIE is restored into SIE by sret.
    csrr t0, sstatus
    li t1, ~SSTATUS_SPP
    and t0, t0, t1
    ori t0, t0, SSTATUS_SPIE
    csrw sstatus, t0

    li sp, 0x40002000
    la t0, lab3_switch_kernel_stack_top
    csrw sscratch, t0
    sret

    .globl lab3_switch_resume
lab3_switch_resume:
    # Remove the temporary user mapping from the boot page table.
    la t0, boot_page_table_sv39
    la t1, lab3_switch_saved_root_pte
    ld t2, 0(t1)
    sd t2, 8(t0)
    sfence.vma zero, zero

    la t0, lab3_switch_test_phase
    sd zero, 0(t0)

    # Restore the C caller's stack and return address.
    la t0, lab3_switch_saved_kernel_sp
    ld sp, 0(t0)
    ld ra, 0(sp)
    addi sp, sp, 16
    ret

    .section .data
    .align 3
lab3_switch_saved_kernel_sp:
    .quad 0
lab3_switch_saved_root_pte:
    .quad 0

    .align 12
lab3_switch_user_code_page:
    ecall
    ebreak
    .balign 16
lab3_switch_user_second_stage:
    ecall
lab3_switch_user_fail:
    ebreak
1:
    j 1b

    .section .bss
    .align 12
lab3_switch_l1:
    .space PGSIZE

    .align 12
lab3_switch_l0:
    .space PGSIZE

    .align 4
    .globl lab3_switch_test_phase
lab3_switch_test_phase:
    .quad 0

    .align 12
lab3_switch_kernel_stack:
    .space PGSIZE
    .globl lab3_switch_kernel_stack_top
lab3_switch_kernel_stack_top:

    .align 12
lab3_switch_user_stack:
    .space PGSIZE
