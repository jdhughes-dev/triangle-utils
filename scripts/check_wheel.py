"""
Check that a wheel holds only the package and needs nothing beside it.

Fails if the wheel holds a file other than the module files, the library,
the licenses, the notice, and the metadata; if its library links anything
but the system libraries every machine has, read with the platform's own
tool (readelf on Linux, otool on macOS, dumpbin on Windows); or, on macOS,
if the library needs a macOS later than MACOSX_DEPLOYMENT_TARGET. Run by
.github/workflows/wheels.yml on each wheel it builds.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
LICENSES = ("LICENSE", "LICENSE-Triangle", "NOTICE")
# the libraries a library in a wheel may link: the C library and the
# loader on Linux, the system and C++ libraries and its own name on macOS,
# and the kernel on Windows
ALLOWED = {
    "linux": re.compile(
        r"(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1"
        r"|ld-linux[\w.-]*\.so\.\d)"
    ),
    "darwin": re.compile(
        r"(/usr/lib/libSystem\.B\.dylib|/usr/lib/libc\+\+\.1\.dylib"
        r"|@rpath/libtriangle_utils\.dylib)"
    ),
    "win32": re.compile(r"kernel32\.dll", re.IGNORECASE),
}


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
    ).stdout.splitlines()
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
    """A version as three numbers, so 11 and 11.0 compare equal."""
    parts = [int(part) for part in text.split(".")]
    return tuple([*parts, 0, 0, 0][:3])


def expected_files(dist_info, library):
    """The files a wheel should hold."""
    modules = sorted((REPOSITORY / "python" / "triangle_utils").glob("*.py"))
    package = [f"triangle_utils/{path.name}" for path in modules]
    package += [f"triangle_utils/{name}" for name in (*LICENSES, library)]
    metadata = [f"{dist_info}/{name}" for name in ("METADATA", "WHEEL", "RECORD")]
    metadata += [f"{dist_info}/licenses/{name}" for name in LICENSES]
    return set(package + metadata)


def check(wheel):
    """The failures of a wheel."""
    failures = []
    platform = "linux" if sys.platform.startswith("linux") else sys.platform
    with tempfile.TemporaryDirectory() as directory:
        with zipfile.ZipFile(wheel) as archive:
            # files only; a repaired wheel also lists its directories
            names = {name for name in archive.namelist() if not name.endswith("/")}
            archive.extractall(directory)
        dist_info = next(name.split("/")[0] for name in names if ".dist-info/" in name)
        libraries = [
            Path(directory) / name
            for name in names
            if name.startswith("triangle_utils/libtriangle_utils.")
        ]
        if len(libraries) != 1:
            return [f"{wheel.name} holds {len(libraries)} libraries, not 1"]
        library = libraries[0]
        expected = expected_files(dist_info, library.name)
        failures += [f"{wheel.name} holds {name}" for name in sorted(names - expected)]
        failures += [f"{wheel.name} lacks {name}" for name in sorted(expected - names)]
        metadata = (Path(directory) / dist_info / "METADATA").read_text()
        if "License-Expression: CC0-1.0 AND LicenseRef-Triangle" not in metadata:
            failures.append(f"{wheel.name} has no license expression in its metadata")

        links = linked(library)
        print(f"{wheel.name}: {library.name} links {', '.join(links)}")
        if not links:
            failures.append(f"{library.name} links nothing, so its links were not read")
        failures += [
            f"{library.name} links {name}, which a machine may not have"
            for name in links
            if not ALLOWED[platform].fullmatch(name)
        ]
        if platform == "darwin":
            target = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
            if not target:
                return [*failures, "MACOSX_DEPLOYMENT_TARGET is not set"]
            oldest = minimum_macos(library)
            print(f"{wheel.name}: {library.name} runs on macOS {oldest} and later")
            if version(oldest) > version(target):
                failures.append(
                    f"{library.name} needs macOS {oldest}, later than {target}"
                )
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("wheels", nargs="+", type=Path)
    failures = []
    for wheel in parser.parse_args().wheels:
        failures += check(wheel)
    if failures:
        sys.exit("\n".join(failures))


if __name__ == "__main__":
    main()
