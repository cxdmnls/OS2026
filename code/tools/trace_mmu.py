#!/usr/bin/env python3
"""Coordinate host GDB and guest GDB; save real QEMU 4.1.1 MMU traces."""
import os
import pathlib
import re
import signal
import socket
import subprocess
import time

root = pathlib.Path(__file__).resolve().parents[1]
logs = root.parent / "report/logs"
logs.mkdir(exist_ok=True)
qemu = os.environ.get("MMU_QEMU", "/home/lenovo/oslab/build/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64")
version = subprocess.check_output([qemu, "--version"], text=True).splitlines()[0]
if not version.startswith("QEMU emulator version 4.1.1"):
    raise RuntimeError("This lab requires QEMU 4.1.1: " + version)
symbols = subprocess.check_output(["riscv64-unknown-elf-nm", "bin/kernel"], cwd=root, text=True)
target = next(line.split()[0] for line in symbols.splitlines() if line.endswith(" mmu_probe_cell"))
done = next(line.split()[0] for line in symbols.splitlines() if line.endswith(" mmu_probe_done"))
load = next(line.split()[0] for line in symbols.splitlines() if line.endswith(" mmu_probe_load"))
disassembly = subprocess.check_output(["riscv64-unknown-elf-objdump", "-d", "bin/kernel"], cwd=root, text=True)
load_lines = disassembly[re.search(r"\b" + load.lstrip("0") + r":", disassembly).start():]
regs = re.findall(r"\bld\s+(\w+),0\(\w+\)", load_lines)[:2]
if len(regs) != 2:
    raise RuntimeError("cannot locate the probe's two load destinations")
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
env = dict(os.environ, MMU_TARGET=target, MMU_QEMU=qemu,
           MMU_SERIAL=str(logs / "mmu-serial.txt"), MMU_PORT=str(port))
guest_script = f'''set pagination off
set confirm off
set remotetimeout 120
file bin/kernel
set architecture riscv:rv64
target remote 127.0.0.1:{port}
info registers pc
x/5i 0x1000
break mmu_probe_load
continue
info registers pc satp
x/3i $pc
si
info registers pc
si
info registers pc
printf "LOAD_RESULTS first=0x%lx second=0x%lx\\n", ${regs[0]}, ${regs[1]}
printf "TWO_LOADS_COMPLETED pc=0x%lx\\n", $pc
detach
quit
'''
# Command file is generated from the selected ephemeral port, never hand-edit
# a debugger port or hard-code the kernel data address.
import tempfile
with tempfile.TemporaryDirectory(prefix="oslab2-mmu-") as tmp:
    script = pathlib.Path(tmp) / "guest.gdb"
    script.write_text(guest_script)
    with (logs / "mmu-host-gdb.txt").open("w") as host_log:
        host = subprocess.Popen(["gdb", "-q", "-nx", "-batch", "-x", "tools/mmu_host_gdb.py"],
            cwd=root, env=env, stdout=host_log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            # Poll the listening socket via /proc without opening a debugger connection.
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if host.poll() is not None:
                    raise RuntimeError("host GDB exited; see mmu-host-gdb.txt")
                tcp = pathlib.Path("/proc/net/tcp").read_text()
                if any(line.split()[1].endswith(":%04X" % port) and
                       line.split()[3] == "0A" for line in tcp.splitlines()[1:]):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("QEMU GDB stub did not start")
            guest = subprocess.run(["riscv64-unknown-elf-gdb", "-q", "-nx", "-batch", "-x", str(script)],
                cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
            (logs / "mmu-guest-gdb.txt").write_text(guest.stdout)
            if guest.returncode:
                raise RuntimeError(guest.stdout)
            time.sleep(1)
        finally:
            if host.poll() is None:
                os.killpg(host.pid, signal.SIGTERM)
                host.wait(timeout=10)
# Normalize serial CRLF and trailing spaces; preserve all observed text.
for name in ("mmu-host-gdb.txt", "mmu-guest-gdb.txt", "mmu-serial.txt"):
    path = logs / name
    path.write_text("\n".join(line.rstrip() for line in path.read_text().splitlines()) + "\n")
host_text = (logs / "mmu-host-gdb.txt").read_text()
assert "BARE_PHYSICAL=0x1000" in host_text, host_text
expected_pa = int(target, 16) - 0xffffffff40000000
assert "PAGED_PHYSICAL=" + hex(expected_pa) in host_text, "unexpected physical address"
assert "SOFTWARE TLB FILL" in host_text, host_text
assert "HOST_DATA_VALUE=0x123456789abcdef0" in host_text, "missing host RAM verification"
assert host_text.count("=== DATA LOAD SLOW HELPER ===") == 1, "expected one slow helper for two loads"
assert "TWO_LOADS_COMPLETED pc=0x" + done.lstrip("0") in guest.stdout, guest.stdout
assert "UNEXPECTED_EXTRA_DATA_WALK" not in host_text, "CPU repeated a walk; inspect mmu-host-gdb.txt"
assert "LOAD_RESULTS first=0x123456789abcdef0 second=0x123456789abcdef0" in guest.stdout, guest.stdout
print("QEMU 4.1.1 host/guest GDB translation trace: PASS")
