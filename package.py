"""Package only the mod, its documentation, and redistributable source."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

VERSION = "0.1.4"
root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-dir", type=Path, default=root,
                    help="ZIP/checksum destination (default: source directory)")
output = parser.parse_args().output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
dist = root / "dist"
for name in ("README.md", "LICENSE"):
    (dist / name).write_bytes((root / name).read_bytes())
binary_names = ["gd-display-mod.dll", "gd-display-mod.ini", "gd-mod-test.exe",
                "Start-Gitadora.ps1", "README.md", "LICENSE", "MinHook-LICENSE.txt"]
# Distribute the conservative default profile, not the user's selected mode.
payload = {name: (root / name if name == 'gd-display-mod.ini' else dist / name) for name in binary_names}
manifest = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in payload.items()}
(dist / "SHA256.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
binary_names.append("SHA256.json")
payload['SHA256.json'] = dist / 'SHA256.json'
binary_archive = output / f"gd-display-mod-{VERSION}-binary.zip"
with zipfile.ZipFile(binary_archive, "w", zipfile.ZIP_DEFLATED) as archive:
    for name in binary_names:
        archive.write(payload[name], "gd-display-mod/" + name)

source_files = [root / name for name in ("README.md", "LICENSE", "build.cmd", "package.py",
                "gd-display-mod.ini", "Start-Gitadora.ps1", ".gitignore", ".gitattributes",
                ".github/workflows/build.yml")]
for directory in ("src", "tests", "third_party/minhook/src", "third_party/minhook/include"):
    source_files += [path for path in (root / directory).rglob("*")
                     if path.is_file() and path.suffix in {".c", ".cpp", ".h", ".py", ".ps1"}
                     and not any(part.startswith(".") for part in path.relative_to(root).parts)]
source_files += [root / "third_party/minhook" / name for name in ("LICENSE.txt", "README.md")]
source_archive = output / f"gd-display-mod-{VERSION}-source.zip"
with zipfile.ZipFile(source_archive, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(source_files):
        archive.write(path, "gd-display-mod/" + path.relative_to(root).as_posix())

checksums = []
for path in (binary_archive, source_archive):
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise RuntimeError(f"ZIP integrity check failed: {path.name}")
        if any(Path(name).name.lower() == "gdxg.dll" for name in archive.namelist()):
            raise RuntimeError("Game DLL must not be distributed")
        if path == binary_archive:
            # Verify the manifest against the actual ZIP entries, including the
            # conservative default INI rather than the developer's active mode.
            packaged_manifest = json.loads(archive.read("gd-display-mod/SHA256.json"))
            expected_entries = {"gd-display-mod/" + name for name in packaged_manifest}
            expected_entries.add("gd-display-mod/SHA256.json")
            if set(archive.namelist()) != expected_entries:
                raise RuntimeError("Binary ZIP contains missing or unexpected files")
            for name, expected in packaged_manifest.items():
                if hashlib.sha256(archive.read("gd-display-mod/" + name)).hexdigest() != expected:
                    raise RuntimeError(f"Packaged checksum mismatch: {name}")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    checksums.append(f"{digest}  {path.name}\n")
    print(f"{path.name}: {path.stat().st_size:,} bytes, SHA256 {digest}")
(output / "SHA256SUMS.txt").write_text("".join(checksums), encoding="utf-8")
