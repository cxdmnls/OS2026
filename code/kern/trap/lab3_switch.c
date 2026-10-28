#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>

extern void lab3_switch_enter_user(void);
extern void lab3_switch_resume(void);

extern volatile uintptr_t lab3_switch_test_phase;

#define LAB3_SWITCH_USER_BASE       ((uintptr_t)0x0000000040000000ULL)
#define LAB3_SWITCH_USER_SECOND     (LAB3_SWITCH_USER_BASE + 0x10)

volatile int lab3_switch_test_result;

/*
 * Handle the two user-mode environment calls used by Challenge 1.
 *
 * phase == 1: the first U-mode ecall returns to the second user stage.
 * phase == 2: the second U-mode ecall returns to the kernel resume label.
 *
 * Timer interrupts are left to the normal interrupt handler. Other user-mode
 * exceptions terminate the test and return to the kernel resume label so a
 * failure produces a diagnostic instead of an exception loop.
 */
bool
lab3_switch_test_handle(struct trapframe *tf) {
    if (lab3_switch_test_phase == 0) {
        return 0;
    }

    if ((intptr_t)tf->cause < 0) {
        return 0;
    }

    if ((tf->status & SSTATUS_SPP) == 0 && tf->cause == CAUSE_USER_ECALL) {
        if (lab3_switch_test_phase == 1) {
            lab3_switch_test_phase = 2;
            tf->epc = LAB3_SWITCH_USER_SECOND;
            return 1;
        }

        if (lab3_switch_test_phase == 2) {
            lab3_switch_test_phase = 0;
            lab3_switch_test_result = 0;
            tf->status |= SSTATUS_SPP | SSTATUS_SPIE;
            tf->epc = (uintptr_t)lab3_switch_resume;
            return 1;
        }
    }

    lab3_switch_test_phase = 0;
    lab3_switch_test_result = -(int)tf->cause - 1;
    tf->status |= SSTATUS_SPP | SSTATUS_SPIE;
    tf->epc = (uintptr_t)lab3_switch_resume;
    cprintf("[Challenge 1] user trap failed: cause=0x%lx status=0x%lx epc=0x%lx\n",
            tf->cause, tf->status, tf->epc);
    return 1;
}

void
lab3_switch_test(void) {
    lab3_switch_test_result = -1;
    cprintf("[Challenge 1] user/kernel mode switch test\n");

    lab3_switch_enter_user();

    if (lab3_switch_test_result == 0) {
        cprintf("[Challenge 1] S -> U -> S -> U -> S: PASS\n");
    } else {
        cprintf("[Challenge 1] S -> U -> S -> U -> S: FAIL (%d)\n",
                lab3_switch_test_result);
    }
}
