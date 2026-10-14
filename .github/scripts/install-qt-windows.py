"""Install a prebuilt Qt for Windows straight from download.qt.io.

Since Qt 6.11 the Windows online repository is split per toolchain
(e.g. qt6_6120/qt6_6120_msvc2022_64/), which aqtinstall 3.3.0 cannot read.
This fetches the package's Updates.xml, downloads the requested archives,
checks each against the .sha256 published on download.qt.io, and unpacks
them with 7-Zip.

usage: install-qt-windows.py VERSION TOOLCHAIN ARCH OUTPUT_DIR [MODULE ...]
   e.g. install-qt-windows.py 6.12.0 msvc2022_64 win64_msvc2022_64 C:/Qt qtbase
Prints the Qt prefix (the directory holding lib/cmake/Qt6).
"""

import hashlib
import shutil
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "PunchTray-CI"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def main() -> int:
    if len(sys.argv) < 5:
        print(__doc__, file=sys.stderr)
        return 2
    version, toolchain, arch, out = sys.argv[1:5]
    modules = sys.argv[5:] or ["qtbase"]
    tag = "qt6_" + version.replace(".", "")
    repo = f"{REPO}/{tag}/{tag}_{toolchain}"
    package = f"qt.qt6.{version.replace('.', '')}.{arch}"

    updates = ET.fromstring(fetch(f"{repo}/Updates.xml"))
    entry = next((p for p in updates.iter("PackageUpdate") if p.findtext("Name") == package), None)
    if entry is None:
        print(f"{package} not found in {repo}/Updates.xml", file=sys.stderr)
        return 1
    full_version = entry.findtext("Version")
    archives = [a.strip() for a in (entry.findtext("DownloadableArchives") or "").split(",") if a.strip()]
    wanted = [a for a in archives if any(a.startswith(m + "-") for m in modules)]
    missing = [m for m in modules if not any(a.startswith(m + "-") for a in archives)]
    if missing:
        print(f"modules not in {package}: {', '.join(missing)}", file=sys.stderr)
        return 1

    out_dir = Path(out)
    out_dir.mkdir(parents=True, exist_ok=True)
    seven_zip = shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe"
    for archive in wanted:
        name = f"{full_version}{archive}"
        url = f"{repo}/{package}/{name}"
        print(f"Downloading {name}", file=sys.stderr)
        data = fetch(url)
        expected = fetch(url + ".sha256").decode().split()[0].lower()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            print(f"checksum mismatch for {name}: {actual} != {expected}", file=sys.stderr)
            return 1
        path = out_dir / name
        path.write_bytes(data)
        subprocess.run([seven_zip, "x", "-y", "-bso0", "-bsp0", f"-o{out_dir}", str(path)], check=True)
        path.unlink()

    config = next(out_dir.rglob("lib/cmake/Qt6/Qt6Config.cmake"), None)
    if config is None:
        print("Qt6Config.cmake not found after extraction", file=sys.stderr)
        return 1
    print(config.parents[3])
    return 0


if __name__ == "__main__":
    sys.exit(main())
