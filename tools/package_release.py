"""Package a built EBOOT without games, settings, saves or credentials."""
import argparse
import hashlib
from pathlib import Path
import struct
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--eboot", type=Path, default=Path("EBOOT.PBP"))
parser.add_argument("--output", type=Path, default=Path("dist/Native32PSP-PSP.zip"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
data = args.eboot.read_bytes()
if len(data) < 40 or data[:4] != b"\0PBP":
    raise SystemExit("Not a PBP file")
offsets = struct.unpack_from("<8I", data, 8)
if offsets[0] != 40 or list(offsets) != sorted(offsets) or offsets[-1] > len(data):
    raise SystemExit("Invalid PBP offsets")
if data[offsets[6]:offsets[6]+4] != b"~PSP":
    raise SystemExit("Expected encrypted PSP module")
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as z:
    base = "PSP/GAME/Native32PSP/"
    z.writestr(base + "EBOOT.PBP", data)
    for f in sorted((root / "languages").glob("*.ini")):
        z.write(f, base + "languages/" + f.name)
    z.writestr(base + "games/", "")
    for name in ["LICENSE", "THIRD_PARTY.md", "README.md", "CHANGELOG.md"]:
        z.write(root / name, name)
    for folder in ["licenses", "docs"]:
        for f in sorted((root / folder).rglob("*")):
            if f.is_file(): z.write(f, f.relative_to(root).as_posix())
    z.writestr("SHA256.txt", hashlib.sha256(data).hexdigest()+"  "+base+"EBOOT.PBP\n")
with zipfile.ZipFile(args.output) as z:
    if z.testzip() is not None: raise SystemExit("ZIP verification failed")
print(args.output.resolve())
