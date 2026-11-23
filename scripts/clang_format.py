"""
Check or apply clang-format on the tracked C and C++ source.

Lists the files with git, so a new source or header file is checked without
changing the pixi tasks.
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = ("*.c", "*.cpp", "*.h", "*.hpp")


def source_files():
    out = subprocess.run(
        ["git", "ls-files", "-z", *PATTERNS],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return [f for f in out.split("\0") if f]


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--fix", action="store_true", help="format the files in place")
    args = parser.parse_args()

    files = source_files()
    # clang-format reads stdin when given no files
    if not files:
        sys.exit("no C or C++ files found")
    if args.fix:
        cmd = ["clang-format", "-i", *files]
    else:
        cmd = ["clang-format", "--dry-run", "--Werror", *files]
    sys.exit(subprocess.run(cmd, cwd=ROOT).returncode)


if __name__ == "__main__":
    main()
