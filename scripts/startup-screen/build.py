"""Fully recompile CPU0 into an isolated directory using e2 studio response files.

No experimental patches or stale objects are used. Run after an e2 studio build
has generated Debug/*.in. New UI sources are added to that baseline source list.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "e2studio_CPU0"
LLVM = Path("C:/Renesas/RA/e2studio_v2025-12_fsp_v6.3.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to((BASE / "Debug/startup-screen").resolve())
    out.mkdir(parents=True, exist_ok=False)
    link = (BASE / "Debug/mimamori_sense_CPU0.elf.in").read_text(encoding="utf-8-sig")
    objects = re.findall(r'\./([^\s"]+\.o)\b', link)
    assert objects and len(objects) == len(set(objects))
    additions = ["ui_startup_screen", "ui_startup_image", "puff/puff",
                 "ui_time_setting_screen"]
    template = (BASE / "Debug/src/ui/ui_main_screen.o.in").read_text(encoding="utf-8-sig")
    for name in additions:
        obj = f"src/ui/{name}.o"
        if obj not in objects:
            objects.append(obj)
            link += f" ./{obj}"
    for obj in objects:
        original = BASE / "Debug" / (obj + ".in")
        if obj in [f"src/ui/{n}.o" for n in additions]:
            name = obj[len("src/ui/"):-2]
            rsp = template.replace("ui_main_screen", name)
        else:
            rsp = original.read_text(encoding="utf-8-sig")
        # Input paths in these response files are relative to the old Debug.
        rsp = rsp.replace('"../', '"' + BASE.as_posix() + '/')
        path = out / (obj + ".in")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(rsp + " -fstack-usage\n", encoding="utf-8")
    for name in ["fsp_gen.lld", "memory_regions.lld", "bsp_linker_info.h"]:
        (out / name).write_bytes((BASE / "Debug" / name).read_bytes())
    (out / "mimamori_sense_CPU0.elf.in").write_text(link, encoding="utf-8")
    sources = [p for folder in ["src", "ra_cfg", "ra_gen"] for p in (BASE / folder).rglob("*") if p.is_file()]
    (out / "source-sha256.json").write_text(json.dumps({
        p.relative_to(BASE).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sources}, indent=2), encoding="utf-8")

    def compile_one(obj):
        result = subprocess.run([str(LLVM / "clang.exe"), "--target=arm-none-eabi", "@" + obj + ".in"],
                                cwd=out, capture_output=True)
        (out / (obj + ".log")).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f"Failed: {obj} (see its .log)")

    print(f"Recompiling all {len(objects)} objects into {out}", flush=True)
    failures = []
    with ThreadPoolExecutor(max_workers=6) as pool:
        for i, future in enumerate(as_completed([pool.submit(compile_one, o) for o in objects]), 1):
            try:
                future.result()
            except Exception as exc:
                failures.append(str(exc))
            if i % 100 == 0:
                print(f"Compiled {i}/{len(objects)}", flush=True)
    if failures:
        raise RuntimeError("\n".join(failures))
    with (out / "link.log").open("wb") as log:
        subprocess.run([str(LLVM / "clang.exe"), "--target=arm-none-eabi", "@mimamori_sense_CPU0.elf.in"],
                       cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run([str(LLVM / "llvm-objcopy.exe"), "-O", "srec", "mimamori_sense_CPU0.elf", "mimamori_sense_CPU0.srec"],
                   cwd=out, check=True)
    print("CPU0 ELF / S-record complete", flush=True)


if __name__ == "__main__":
    main()
