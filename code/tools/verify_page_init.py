#!/usr/bin/env python3
"""Run page-init checks in temporary builds, leaving the submitted sources intact."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOG_DIR = None


def run_case(name, size, expected, replacement=None):
    with tempfile.TemporaryDirectory(prefix="oslab2-page-init-") as tmp:
        code = pathlib.Path(tmp) / "code"
        shutil.copytree(ROOT, code, ignore=shutil.ignore_patterns("bin", "obj", "__pycache__"))
        pmm = code / "kern/mm/pmm.c"
        text = pmm.read_text()
        if replacement is not None:
            text = text.replace("uint64_t mem_size = get_memory_size();", replacement, 1)
        if expected == "PASS":
            # Reuse the supplied Page Init test without linking unfinished Best-Fit.
            test = (code / "tools/test_lab2.c").read_text()
            test = test[:test.index("void\ntest_best_fit(void)")]
            test = test.replace("#include <test_lab2.h>", "")
            test += r'''
void check_page_init_layout(void) {
    extern char end[];
    uintptr_t limit = get_memory_base() + get_memory_size();
    if (limit > PHYSICAL_MEMORY_END) limit = PHYSICAL_MEMORY_END;
    limit = ROUNDDOWN(limit, PGSIZE);
    assert(va_pa_offset == PHYSICAL_MEMORY_OFFSET);
    assert(npage == PPN(limit));
    assert((uintptr_t)pages == ROUNDUP((uintptr_t)end, PGSIZE));
    uintptr_t free_begin = ROUNDUP(PADDR((uintptr_t)(pages + npage - nbase)), PGSIZE);
    assert(nr_free_pages() == (limit - free_begin) / PGSIZE);
    for (size_t i = 0; i < npage - nbase; i++) {
        struct Page *p = pages + i;
        assert(page2pa(p) == (i + nbase) * PGSIZE);
        assert(pa2page(page2pa(p)) == p);
        assert(PageReserved(p) == (page2pa(p) < free_begin));
        assert(page_ref(p) == 0);
    }
    cprintf("[Layout] every descriptor, reserved range and free count: PASS\n");
}
'''
            (code / "kern/mm/page_init_test.c").write_text(test)
            text = text.replace("    page_init();", "    page_init();\n"
                                "    extern void check_page_init_layout(void);\n"
                                "    check_page_init_layout();", 1)
            if size <= 128:
                text = text.replace("    check_alloc_page();", "    check_alloc_page();\n"
                                    "    extern void test_page_init(void);\n"
                                    "    test_page_init();", 1)
        pmm.write_text(text)
        build = subprocess.run(["make", "-j4"], cwd=code, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
        if build.returncode:
            raise RuntimeError(build.stdout)
        cmd = ["qemu-system-riscv64", "-machine", "virt", "-m", str(size),
               "-nographic", "-bios", "default", "-device",
               "loader,file=bin/ucore.img,addr=0x80200000"]
        proc = subprocess.Popen(cmd, cwd=code, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            output, _ = proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.terminate()
            output, _ = proc.communicate(timeout=4)
        if LOG_DIR is not None:
            (LOG_DIR / (name + ".txt")).write_text(
                "Build command: make -j4\n" + build.stdout +
                "\nQEMU command: " + " ".join(cmd) + "\n" + output,
                encoding="utf-8")
        if expected == "PASS":
            assert "[Layout] every descriptor, reserved range and free count: PASS" in output, output
            assert "check_alloc_page() succeeded!" in output, output
            assert "panic" not in output.lower(), output
            if size <= 128:
                assert "[Test 1] Page Init: PASS" in output, output
        else:
            assert expected in output, output
            assert "check_alloc_page() succeeded!" not in output, output
        print(f"[{name}] PASS", flush=True)
        for line in output.splitlines():
            if any(tag in line for tag in ("memory:", "descriptors:", "[Test 1]", "[Layout]", "panic at")):
                print(line, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-dir", type=pathlib.Path,
                        help="save actual compiler and QEMU output for each case")
    args = parser.parse_args()
    LOG_DIR = args.log_dir
    if LOG_DIR is not None:
        LOG_DIR.mkdir(parents=True, exist_ok=True)
    run_case("default-128MiB", 128, "PASS")
    run_case("smaller-64MiB", 64, "PASS")
    run_case("cap-256MiB-to-128MiB", 256, "PASS")
    run_case("invalid-zero-size", 128, "invalid DTB memory range", "uint64_t mem_size = 0;")
    run_case("invalid-overflow", 128, "invalid DTB memory range", "uint64_t mem_size = (uint64_t)-1;")
    run_case("insufficient-memory", 128, "memory does not cover the kernel",
             "uint64_t mem_size = 0x200000;")
