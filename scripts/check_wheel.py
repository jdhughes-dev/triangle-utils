"""
Check that the library in a wheel needs nothing beside it.

Reads the libraries it links with the platform's own tool (readelf on
Linux, otool on macOS, dumpbin on Windows) and fails if one is a compiler
runtime, which a wheel would need installed beside it; on macOS it also
fails if the oldest macOS the library runs on is later than the deployment
target. Run by .github/workflows/wheels.yml on each wheel it builds.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

# libraries a wheel cannot assume: the C++ and gcc runtimes, and the MSVC
# runtime, which -static-libstdc++, -static-libgcc, and b_vscrt=mt link in
RUNTIMES = re.compile(
    r"libstdc\+\+|libgcc_s|msvcp\d|vcruntime\d|api-ms-win-crt|ucrtbase",
    re.IGNORECASE,
)


def dumpbin():
    """The path of dumpbin from the newest Visual Studio."""
    vswhere = (
        Path(os.environ["ProgramFiles(x86)"])
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    found = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-find",
            r"VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.split()
    if not found:
        sys.exit("dumpbin was not found")
    return found[0]


def linked(library):
    """The libraries a library links."""
    if sys.platform.startswith("linux"):
        output = subprocess.run(
            ["readelf", "-d", str(library)], check=True, capture_output=True, text=True
        ).stdout
        return re.findall(r"\(NEEDED\).*\[(.+)\]", output)
    if sys.platform == "darwin":
        output = subprocess.run(
            ["otool", "-L", str(library)], check=True, capture_output=True, text=True
        ).stdout
        return [line.split()[0] for line in output.splitlines()[1:] if line.strip()]
    output = subprocess.run(
        [dumpbin(), "/dependents", str(library)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return re.findall(r"^\s+(\S+\.dll)\s*$", output, re.MULTILINE | re.IGNORECASE)


def minimum_macos(library):
    """The oldest macOS a library runs on."""
    output = subprocess.run(
        ["otool", "-l", str(library)], check=True, capture_output=True, text=True
    ).stdout
    found = re.search(r"\bminos (\S+)", output) or re.search(
        r"LC_VERSION_MIN_MACOSX.*?\n\s*version (\S+)", output, re.DOTALL
    )
    if not found:
        sys.exit(f"{library.name} gives no minimum macOS")
    return found.group(1)


def version(text):
    return tuple(int(part) for part in text.split("."))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("wheels", nargs="+", type=Path)
    wheels = parser.parse_args().wheels
    failures = []
    for wheel in wheels:
        with tempfile.TemporaryDirectory() as directory:
            with zipfile.ZipFile(wheel) as archive:
                archive.extractall(directory)
            libraries = [
                path
                for path in Path(directory).rglob("*triangle_utils*")
                if path.suffix in (".so", ".dylib", ".dll")
            ]
            if not libraries:
                failures.append(f"{wheel.name} has no library")
            for library in libraries:
                links = linked(library)
                print(f"{wheel.name}: {library.name} links {', '.join(links)}")
                failures += [
                    f"{library.name} links the runtime {name}"
                    for name in links
                    if RUNTIMES.search(name)
                ]
                if sys.platform == "darwin":
                    target = os.environ.get("MACOSX_DEPLOYMENT_TARGET", "11.0")
                    oldest = minimum_macos(library)
                    print(
                        f"{wheel.name}: {library.name} runs on macOS {oldest} and later"
                    )
                    if version(oldest) > version(target):
                        failures.append(
                            f"{library.name} needs macOS {oldest}, later than {target}"
                        )
    if failures:
        sys.exit("\n".join(failures))


if __name__ == "__main__":
    main()
