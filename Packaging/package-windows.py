import os
import shutil
import zipfile
from pathlib import Path

version = os.environ["VERSION"]
dist = Path("dist")
dist.mkdir(exist_ok=True)
files = {
    "slipstream5000.exe": Path("build/slipstream5000.exe"),
    "SDL3.dll": Path("build/SDL3.dll"),
    "LICENSE": Path("LICENSE"),
    "readme.md": Path("readme.md"),
    "Opal-LICENSE": Path("src/opal/LICENSE"),
}
with zipfile.ZipFile(dist / f"slipstream5000-{version}-windows-x86_64.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for name, path in files.items():
        archive.write(path, name)
# Keep symbols available without cluttering the playable archive.
shutil.copyfile("build/slipstream5000.pdb", dist / f"slipstream5000-{version}-windows-x86_64.pdb")
