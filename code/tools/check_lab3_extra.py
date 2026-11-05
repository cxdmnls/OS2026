#!/usr/bin/env python3
"""Run instruction-length and PMM integration checks in temporary copies."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
qemu = os.environ.get("QEMU", "/opt/qemu-4.1.1/bin/qemu-system-riscv64")
checks = """
    asm volatile(".2byte 0x9002" ::: "memory");
    cprintf("[Extra] compressed breakpoint return: PASS\\n");
    asm volatile(".4byte 0x00100073" ::: "memory");
    cprintf("[Extra] 32-bit breakpoint return: PASS\\n");
    asm volatile(".2byte 0x0000" ::: "memory");
    cprintf("[Extra] compressed illegal return: PASS\\n");
    asm volatile(".4byte 0x0000007b" ::: "memory");
    cprintf("[Extra] 32-bit illegal return: PASS\\n");
    sbi_shutdown();
"""
sentinels = [
    "[Extra] compressed breakpoint return: PASS",
    "[Extra] 32-bit breakpoint return: PASS",
    "[Extra] compressed illegal return: PASS",
    "[Extra] 32-bit illegal return: PASS",
    "check_alloc_page() succeeded!",
]

for manager in ("default", "best_fit"):
    with tempfile.TemporaryDirectory(prefix="lab3-extra-") as temp:
        work = Path(temp) / "code"
        shutil.copytree(root, work, ignore=shutil.ignore_patterns("obj", "bin", ".qemu*"))
        init = work / "kern/init/init.c"
        text = init.read_text()
        text = text.replace("#include <trap.h>", "#include <trap.h>\n#include <sbi.h>")
        assert text.count("    clock_init();") == 1
        init.write_text(text.replace("    clock_init();", checks + "\n    clock_init();"))
        pmm = work / "kern/mm/pmm.c"
        pmm.write_text(pmm.read_text().replace(
            "pmm_manager = &default_pmm_manager;",
            "pmm_manager = &" + manager + "_pmm_manager;"))
        build = subprocess.run(["make", "-j2"], cwd=work, capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError(build.stdout + build.stderr)
        run = subprocess.run([
            qemu, "-machine", "virt", "-nographic", "-bios", "default",
            "-device", "loader,file=bin/ucore.img,addr=0x80200000",
            "-monitor", "none", "-no-reboot",
        ], cwd=work, capture_output=True, text=True, timeout=15)
        print("=== " + manager + " ===", flush=True)
        print(run.stdout, flush=True)
        assert run.returncode == 0, run.stderr
        for sentinel in sentinels + ["memory management: " + manager + "_pmm_manager"]:
            assert sentinel in run.stdout, sentinel
        assert "panic" not in run.stdout.lower()
        print("[Extra] " + manager + " integration: PASS", flush=True)
