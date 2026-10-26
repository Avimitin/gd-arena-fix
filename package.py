"""Package the two-file user distribution and the redistributable source."""
from pathlib import Path
import argparse
import hashlib
import zipfile

NAME = "gd-arena-fix"
VERSION = "0.1.4"
root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-dir", type=Path, default=root,
                    help="ZIP/checksum destination (default: source directory)")
output = parser.parse_args().output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
dist = root / "dist"

# The user archive contains exactly these two files, with no wrapper folder.
# Always ship the public default INI rather than a developer's active profile.
payload = {f"{NAME}.dll": dist / f"{NAME}.dll",
           f"{NAME}.ini": root / f"{NAME}.ini"}
dll_bytes = payload[f"{NAME}.dll"].read_bytes()
for notice in (root / "LICENSE", root / "third_party/minhook/LICENSE.txt"):
    if notice.read_bytes() not in dll_bytes:
        raise RuntimeError(f"Rebuild the DLL: embedded notice missing: {notice.relative_to(root)}")
expected_hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                   for name, path in payload.items()}
binary_archive = output / f"{NAME}-{VERSION}-binary.zip"
with zipfile.ZipFile(binary_archive, "w", zipfile.ZIP_DEFLATED) as archive:
    for name, path in payload.items():
        archive.write(path, name)

source_files = [root / name for name in (
    "README.md", "LICENSE", "build.cmd", "package.py", f"{NAME}.ini",
    ".gitignore", ".gitattributes", ".github/workflows/build.yml")]
for directory in ("src", "tests", "third_party/minhook/src", "third_party/minhook/include"):
    source_files += [path for path in (root / directory).rglob("*")
                    if path.is_file() and path.suffix in {".c", ".cpp", ".h", ".py", ".rc"}
                    and not any(part.startswith(".") for part in path.relative_to(root).parts)]
source_files += [root / "third_party/minhook" / name for name in ("LICENSE.txt", "README.md")]
source_files += sorted((root / "docs").rglob("*.md"))
source_archive = output / f"{NAME}-{VERSION}-source.zip"
with zipfile.ZipFile(source_archive, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(source_files):
        archive.write(path, NAME + "/" + path.relative_to(root).as_posix())

checksums = []
for path in (binary_archive, source_archive):
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise RuntimeError(f"ZIP integrity check failed: {path.name}")
        if any(Path(name).name.lower() == "gdxg.dll" for name in archive.namelist()):
            raise RuntimeError("Game DLL must not be distributed")
        if path == binary_archive:
            if len(archive.namelist()) != 2 or set(archive.namelist()) != set(payload):
                raise RuntimeError("Binary ZIP must contain only the DLL and INI")
            for name, expected in expected_hashes.items():
                if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                    raise RuntimeError(f"Packaged checksum mismatch: {name}")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    checksums.append(f"{digest}  {path.name}\n")
    print(f"{path.name}: {path.stat().st_size:,} bytes, SHA256 {digest}")
(output / "SHA256SUMS.txt").write_text("".join(checksums), encoding="utf-8")
