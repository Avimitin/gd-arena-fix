"""Package the two-file user distribution."""
from pathlib import Path
import argparse
import hashlib
import zipfile

NAME = "gd-arena-fix"
VERSION = "0.1.7"
root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-dir", type=Path, default=root,
                    help="ZIP/checksum destination (default: source directory)")
parser.add_argument("--dist-dir", type=Path, default=root / "dist",
                    help="directory containing the built DLL (default: dist)")
args = parser.parse_args()
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
dist = args.dist_dir.resolve()

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

with zipfile.ZipFile(binary_archive) as archive:
    if archive.testzip() is not None:
        raise RuntimeError(f"ZIP integrity check failed: {binary_archive.name}")
    if len(archive.namelist()) != 2 or set(archive.namelist()) != set(payload):
        raise RuntimeError("Binary ZIP must contain only the DLL and INI")
    for name, expected in expected_hashes.items():
        if hashlib.sha256(archive.read(name)).hexdigest() != expected:
            raise RuntimeError(f"Packaged checksum mismatch: {name}")
digest = hashlib.sha256(binary_archive.read_bytes()).hexdigest()
print(f"{binary_archive.name}: {binary_archive.stat().st_size:,} bytes, SHA256 {digest}")
(output / "SHA256SUMS.txt").write_text(
    f"{digest}  {binary_archive.name}\n", encoding="utf-8")
