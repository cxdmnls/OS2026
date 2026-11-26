#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <error.h>
#include <vmm.h>
#include <kmalloc.h>

//lab5 function1 2413603
static int sys_exit(uint64_t arg[]) { return do_exit((int)arg[0]); }
static int sys_fork(uint64_t arg[]) {
    return do_fork(0, current->tf->gpr.sp, current->tf);
}
static int sys_wait(uint64_t arg[]) {
    return do_wait((int)arg[0], (int *)(uintptr_t)arg[1]);
}
static int sys_exec(uint64_t arg[]) {
    size_t len = (size_t)arg[1];
    size_t size = (size_t)arg[3];
    uintptr_t user_name = (uintptr_t)arg[0];
    uintptr_t user_binary = (uintptr_t)arg[2];
    if (len == 0 || len > PROC_NAME_LEN || size == 0 || current->mm == NULL ||
        !user_mem_check(current->mm, user_name, len, 0) ||
        !user_mem_check(current->mm, user_binary, size, 0)) {
        return -E_INVAL;
    }

    char *name = kmalloc(len + 1);
    unsigned char *binary = kmalloc(size);
    if (name == NULL || binary == NULL) {
        if (name != NULL) kfree(name);
        if (binary != NULL) kfree(binary);
        return -E_NO_MEM;
    }
    if (!copy_from_user(current->mm, name, (const void *)user_name, len, 0) ||
        !copy_from_user(current->mm, binary, (const void *)user_binary, size, 0)) {
        kfree(binary);
        kfree(name);
        return -E_INVAL;
    }
    name[len] = '\0';

    // Preserve user buffers before exec replaces the current address space.
    int ret = do_execve(name, len, binary, size);
    kfree(binary);
    kfree(name);
    return ret;
}
static int sys_yield(uint64_t arg[]) { return do_yield(); }
static int sys_kill(uint64_t arg[]) { return do_kill((int)arg[0]); }
static int sys_getpid(uint64_t arg[]) { return current->pid; }
static int sys_putc(uint64_t arg[]) {
    cputchar((int)arg[0]);
    return 0;
}
static int sys_pgdir(uint64_t arg[]) {
    print_pgdir();
    return 0;
}

static int (*syscalls[])(uint64_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_wait]              sys_wait,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_getpid]            sys_getpid,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

void syscall(void) {
    assert(current != NULL && current->tf != NULL);
    struct trapframe *tf = current->tf;
    uint64_t num = tf->gpr.a0;
    uint64_t arg[5] = {tf->gpr.a1, tf->gpr.a2, tf->gpr.a3,
                       tf->gpr.a4, tf->gpr.a5};
    int ret = -E_INVAL;
    if (num < NUM_SYSCALLS && syscalls[num] != NULL) {
        ret = syscalls[num](arg);
    }
    tf->gpr.a0 = (intptr_t)ret;
}
