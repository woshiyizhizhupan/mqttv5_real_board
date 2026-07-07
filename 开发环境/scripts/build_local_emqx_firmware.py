from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Build HC32F460 local EMQX firmware with GNU Arm GCC.")
    parser.add_argument(
        "--gcc",
        default=r"D:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe",
    )
    parser.add_argument("--project-root", default=None, help="Firmware project folder. Defaults to <repo>/mqttv5(1).")
    parser.add_argument("--output-name", default="mqttv5_local_emqx", help="Output artifact base name.")
    parser.add_argument("--define", action="append", default=[], help="Additional C preprocessor define.")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    project_root = Path(args.project_root) if args.project_root else repo_root / "mqttv5(1)"
    project_root = project_root.resolve()
    eide_root = project_root / "eide"
    params_path = eide_root / "build" / "Debug" / "builder.params"
    output_dir = eide_root / "build" / "LocalEMQX"
    object_root = output_dir / ".obj" / "__"
    elf_path = output_dir / f"{args.output_name}.elf"
    hex_path = output_dir / f"{args.output_name}.hex"
    bin_path = output_dir / f"{args.output_name}.bin"
    map_path = output_dir / f"{args.output_name}.map"
    linker_script = "../ldscripts/hc32f460_app_0x4000.lds"

    gcc = Path(args.gcc)
    if not gcc.exists():
        raise SystemExit(f"arm-none-eabi-gcc not found: {gcc}")
    tool_dir = gcc.parent
    objcopy = tool_dir / "arm-none-eabi-objcopy.exe"
    size = tool_dir / "arm-none-eabi-size.exe"

    if args.clean and output_dir.exists():
        remove_tree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    params = json.loads(params_path.read_text(encoding="utf-8"))
    include_args = [f"-I{include_dir}" for include_dir in params["incDirs"]]
    app_defines = [*params["defines"], "VECT_TAB_OFFSET=0x4000", *args.define]
    define_args = [f"-D{define}" for define in app_defines]

    c_common = [
        "-c",
        "-xc",
        "-std=c11",
        *include_args,
        *define_args,
        "-mcpu=cortex-m4",
        "-mfloat-abi=soft",
        "-mthumb",
        "-O1",
        "-Wall",
        "-Wno-error=incompatible-pointer-types",
        "-g",
        "-ffunction-sections",
        "-fdata-sections",
        "-funsigned-char",
        "--specs=nano.specs",
        "--specs=nosys.specs",
    ]
    asm_common = [
        "-c",
        "-x",
        "assembler-with-cpp",
        *include_args,
        "-mcpu=cortex-m4",
        "-mfloat-abi=soft",
        "-mthumb",
        "-g",
        "--specs=nano.specs",
        "--specs=nosys.specs",
    ]

    objects: list[Path] = []
    for source in params["sourceList"]:
        source_path = (eide_root / source).resolve()
        object_path = object_path_for(object_root, source)
        object_path.parent.mkdir(parents=True, exist_ok=True)
        objects.append(object_path)
        relative_object = path_arg(object_path.relative_to(eide_root))
        if source_path.suffix.lower() == ".s":
            command = [str(gcc), *asm_common, "-o", relative_object, source]
        else:
            command = [str(gcc), *c_common, "-o", relative_object, source]
        run(command, eide_root)

    response_path = output_dir / "link_local_emqx.rsp"
    response_args = [
        "-mcpu=cortex-m4",
        "-mfloat-abi=soft",
        "-mthumb",
        "-T",
        linker_script,
        "-Wl,--gc-sections",
        "-Wl,--print-memory-usage",
        "--specs=nano.specs",
        "--specs=nosys.specs",
        f"-Wl,-Map={path_arg(map_path.relative_to(eide_root))}",
        "-o",
        path_arg(elf_path.relative_to(eide_root)),
        *[path_arg(obj.relative_to(eide_root)) for obj in objects],
    ]
    response_path.write_text("\n".join(quote_rsp(arg) for arg in response_args), encoding="utf-8")
    run([str(gcc), f"@{path_arg(response_path.relative_to(eide_root))}"], eide_root)
    run([str(objcopy), "-O", "ihex", path_arg(elf_path.relative_to(eide_root)), path_arg(hex_path.relative_to(eide_root))], eide_root)
    run([str(objcopy), "-O", "binary", path_arg(elf_path.relative_to(eide_root)), path_arg(bin_path.relative_to(eide_root))], eide_root)
    run([str(size), path_arg(elf_path.relative_to(eide_root))], eide_root)

    minimum, maximum = hex_address_range(hex_path)
    if minimum < 0x4000:
        raise SystemExit(f"unsafe HEX start address: 0x{minimum:08X}")
    print(f"HEX_RANGE=0x{minimum:08X}-0x{maximum:08X}")
    print(f"ELF={elf_path}")
    print(f"HEX={hex_path}")
    print(f"BIN={bin_path}")
    return 0


def object_path_for(object_root: Path, source: str) -> Path:
    relative = source.replace("\\", "/")
    if relative.startswith("../"):
        relative = relative[3:]
    relative_path = Path(relative)
    return object_root / relative_path.with_suffix(".o")


def path_arg(path: Path | str) -> str:
    return str(path).replace("\\", "/")


def quote_rsp(arg: str) -> str:
    if any(char.isspace() for char in arg):
        return '"' + arg.replace('"', '\\"') + '"'
    return arg


def run(command: list[str], cwd: Path) -> None:
    print(" ".join(command))
    completed = subprocess.run(command, cwd=cwd)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def hex_address_range(path: Path) -> tuple[int, int]:
    upper = 0
    minimum: int | None = None
    maximum = 0
    for raw_line in path.read_text(encoding="ascii").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        length = int(line[1:3], 16)
        address = int(line[3:7], 16)
        record_type = int(line[7:9], 16)
        if record_type == 0:
            absolute = upper + address
            if length:
                minimum = absolute if minimum is None else min(minimum, absolute)
                maximum = max(maximum, absolute + length - 1)
        elif record_type == 2:
            upper = int(line[9:13], 16) << 4
        elif record_type == 4:
            upper = int(line[9:13], 16) << 16
    if minimum is None:
        raise ValueError(f"no data records in {path}")
    return minimum, maximum


def remove_tree(path: Path) -> None:
    import shutil

    shutil.rmtree(path)


if __name__ == "__main__":
    raise SystemExit(main())
