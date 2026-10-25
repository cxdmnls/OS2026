"""Executed by native x86 GDB, not by the RISC-V guest GDB."""
import gdb
import os
import shlex

target = int(os.environ["MMU_TARGET"], 16)
qemu = os.environ["MMU_QEMU"]
serial = os.environ["MMU_SERIAL"]
port = os.environ["MMU_PORT"]
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("set print thread-events off")
gdb.execute("set debuginfod enabled off")
gdb.execute("handle SIGPIPE nostop noprint pass")
gdb.execute("file " + shlex.quote(qemu))
gdb.execute("set args -machine virt -nographic -bios default -monitor none "
            "-serial file:" + serial + " -device loader,file=bin/ucore.img,addr=0x80200000 "
            "-S -gdb tcp:127.0.0.1:" + port)

bare = gdb.Breakpoint("get_physical_address", temporary=True)
bare.condition = "addr == 0x1000 && mmu_idx == 3 && access_type == 2"
gdb.execute("run")
print("=== BARE RESET TRANSLATION ===", flush=True)
gdb.execute("info args")
gdb.execute("bt 6")
physical = gdb.parse_and_eval("physical")
gdb.execute("finish")
print("BARE_PHYSICAL=" + hex(int(physical.dereference())), flush=True)
bare_fill = gdb.Breakpoint("tlb_set_page_with_attrs", temporary=True)
bare_fill.condition = "vaddr == 0x1000 && mmu_idx == 3"
gdb.execute("continue")
print("=== BARE SOFTWARE TLB FILL (NO PAGE WALK) ===", flush=True)
gdb.execute("info args")
gdb.execute("bt 6")

class LoadObserver(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval("addr")) != target:
            return False
        print("=== DATA LOAD SLOW HELPER ===", flush=True)
        gdb.execute("info args")
        gdb.execute("bt 4")
        return False

load_observer = LoadObserver("helper_le_ldq_mmu")
load_observer.condition = "addr == %#x" % target

walk = gdb.Breakpoint("get_physical_address")
walk.condition = "addr == %#x && access_type == 0" % target
gdb.execute("continue")
print("=== PAGED DATA LOAD ===", flush=True)
gdb.execute("info args")
gdb.execute("p/x env->satp")
gdb.execute("bt 10")
physical = gdb.parse_and_eval("physical")
# Step source lines in the actual RISC-V walk, logging locals when available.
for step in range(75):
    frame = gdb.newest_frame()
    if frame.name() != "get_physical_address":
        break
    values = []
    for name in ("base", "levels", "ptidxbits", "ptesize", "ptshift", "i", "idx", "pte_addr", "pte", "ppn"):
        try:
            value = gdb.parse_and_eval(name)
            if not value.is_optimized_out:
                values.append(name + "=" + hex(int(value)))
        except (gdb.error, ValueError):
            pass
    print(("WALK_STEP %d %s" % (step, " ".join(values))).rstrip(), flush=True)
    gdb.execute("next")
print("PAGED_PHYSICAL=" + hex(int(physical.dereference())), flush=True)
walk.delete()

class ExtraWalkObserver(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval("addr")) != target:
            return False
        caller = gdb.newest_frame().older()
        if caller is None or caller.name() != "riscv_cpu_tlb_fill":
            return False
        print("UNEXPECTED_EXTRA_DATA_WALK", flush=True)
        return False

extra = ExtraWalkObserver("get_physical_address")
extra.condition = "addr == %#x && access_type == 0" % target

fill = gdb.Breakpoint("tlb_set_page_with_attrs", temporary=True)
fill.condition = "vaddr == %#x" % (target & ~4095)
gdb.execute("continue")
print("=== SOFTWARE TLB FILL ===", flush=True)
gdb.execute("info args")
gdb.execute("bt 7")
gdb.execute("set $mmucpu = (RISCVCPU *)cpu")
gdb.execute("p/x $mmucpu->neg.tlb.f[1]")
gdb.execute("finish")
mask = int(gdb.parse_and_eval("$mmucpu->neg.tlb.f[1].mask"))
entry_size = int(gdb.parse_and_eval("sizeof(CPUTLBEntry)"))
index = (target >> 12) & (mask // entry_size)
entry = gdb.parse_and_eval("$mmucpu->neg.tlb.f[1].table[%d]" % index)
print("TLB_INDEX=%#x READ_TAG=%#x ADDEND=%#x" %
      (index, int(entry["addr_read"]), int(entry["addend"])), flush=True)
print("HOST_DATA_VALUE=" + hex(int(gdb.parse_and_eval(
    "*(unsigned long long *)(%#x + %#x)" % (target, int(entry["addend"]))))), flush=True)
gdb.execute("continue")
