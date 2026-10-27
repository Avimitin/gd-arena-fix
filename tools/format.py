"""Format project C++ with the pinned clang-format; leave vendored code alone."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION = "21.1.8"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report changes without writing files")
    args = parser.parse_args()
    # Prefer the formatter installed alongside this Python (including a venv).
    adjacent = Path(sys.executable).parent / ("clang-format.exe" if sys.platform == "win32" else "clang-format")
    executable = str(adjacent) if adjacent.is_file() else shutil.which("clang-format")
    if not executable:
        parser.error("install developer tools with: python -m pip install -r requirements-dev.txt")
    version = subprocess.check_output([executable, "--version"], text=True)
    if f"version {VERSION}" not in version:
        parser.error(f"clang-format {VERSION} is required; install requirements-dev.txt")
    paths = sorted(path for folder in ("src", "tests") for path in (ROOT / folder).rglob("*")
                   if path.suffix in {".cpp", ".h"})
    flags = ["--dry-run", "--Werror"] if args.check else ["-i"]
    # Batch to stay below Windows' command-line limit as the project grows.
    failed = False
    for offset in range(0, len(paths), 20):
        result = subprocess.run([executable, "--style=file", *flags,
                                 *(str(path) for path in paths[offset:offset + 20])], cwd=ROOT)
        failed |= result.returncode != 0
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
