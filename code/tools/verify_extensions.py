#!/usr/bin/env python3
"""Build and boot real kernels for each PMM; check SLUB and MMU probes."""
import os
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
logs = root.parent / "report/logs"
logs.mkdir(exist_ok=True)
qemu = os.environ.get("QEMU", "qemu-system-riscv64")
version = subprocess.check_output([qemu, "--version"], text=True).splitlines()[0]
if not version.startswith("QEMU emulator version 4.1.1"):
    raise RuntimeError("This lab requires QEMU 4.1.1: " + version)
for pmm in ("first_fit", "buddy", "best_fit"):
    subprocess.run(["make", "clean"], cwd=root, check=True, stdout=subprocess.PIPE)
    build = subprocess.run(["make", "-j4", "PMM=" + pmm], cwd=root,
                           check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    proc = subprocess.Popen([qemu, "-machine", "virt", "-nographic", "-bios", "default",
        "-device", "loader,file=bin/ucore.img,addr=0x80200000"], cwd=root,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        output, _ = proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        proc.terminate()
        output, _ = proc.communicate(timeout=5)
    (logs / (pmm + "-extensions.txt")).write_text(build.stdout + output, encoding="utf-8")
    assert "check_alloc_page() succeeded!" in output, output
    assert "[SLUB] all checks: PASS" in output, output
    assert "[MMU] two loads from" in output, output
    assert "panic" not in output.lower(), output
    print(pmm + ": page allocator + SLUB + MMU probe PASS", flush=True)
