#!/usr/bin/env python3

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".m",
    ".mm",
}

EXCLUDED_DIRS = {
    Path("external"),
    #Path("include/BNM/Il2CppHeaders"),
}


def is_excluded(path: Path) -> bool:
    return any(path == excluded or excluded in path.parents for excluded in EXCLUDED_DIRS)


def iter_sources(root: Path):
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue

        relative_path = path.relative_to(root)
        if is_excluded(relative_path):
            continue

        if path.suffix.lower() in SOURCE_EXTENSIONS:
            yield path


def run_clang_format(clang_format: str, files, check: bool) -> int:
    if check:
        command = [clang_format, "--dry-run", "--Werror", *map(str, files)]
    else:
        command = [clang_format, "-i", *map(str, files)]

    return subprocess.run(command, check=False).returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Format project C/C++ sources with clang-format."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Check formatting without modifying files.",
    )
    parser.add_argument(
        "--clang-format",
        default="clang-format",
        help="clang-format executable to use.",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    clang_format = shutil.which(args.clang_format)
    if clang_format is None:
        print(f"error: {args.clang_format!r} was not found in PATH", file=sys.stderr)
        return 1

    files = list(iter_sources(root))
    if not files:
        print("No source files found.")
        return 0

    print(f"Found {len(files)} source file(s).")
    return run_clang_format(clang_format, files, args.check)


if __name__ == "__main__":
    raise SystemExit(main())
