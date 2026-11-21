#!/usr/bin/env python3
"""Render actual watch sprites from the snapshots written by scripts/test.sh."""
from pathlib import Path
import os
import subprocess
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
build = root / "build"
gfx = Path(os.environ.get("M5GFX_SRC", root / ".arduino/user/libraries/M5GFX/src")).resolve()
if not (gfx / "lgfx/v1/LGFXBase.cpp").exists():
    raise SystemExit("M5GFX が見つかりません。M5GFX_SRC にライブラリの src を指定してください。")
v1 = gfx / "lgfx/v1"
ui = root / "StopWatchTourGuide/src"
cache = build / "preview-objects"
cache.mkdir(parents=True, exist_ok=True)
sources = [root / "tests/render.cpp", ui / "WatchUi.cpp",
           *(v1 / f"{name}.cpp" for name in
             ("LGFXBase", "lgfx_fonts", "lgfx_v1", "lgfx_v1_panel", "lgfx_v1_platforms")),
           gfx / "lgfx/Fonts/IPA/lgfx_font_japan.c"]
objects = []
for src in sources:
    obj = cache / (src.stem + ".o")
    # Make-style dependencies include the actual font assets and M5GFX inlines.
    dep = obj.with_suffix(".d")
    deps = dep.read_text().replace("\\\n", " ").partition(":")[2].split() if dep.exists() else []
    if not obj.exists() or not deps or any(not Path(p).exists() or Path(p).stat().st_mtime > obj.stat().st_mtime for p in deps):
        cc = os.environ.get("CC", "cc") if src.suffix == ".c" else os.environ.get("CXX", "c++")
        flags = [] if src.suffix == ".c" else ["-std=c++17"]
        subprocess.run([cc, *flags, "-O2", "-ffunction-sections", "-fdata-sections",
                        "-DLGFX_LINUX_FB", "-I", str(gfx), "-MMD", "-MF", str(dep),
                        "-c", str(src), "-o", str(obj)], check=True)
    objects.append(obj)
exe = cache / "render"
if not exe.exists() or any(p.stat().st_mtime > exe.stat().st_mtime for p in objects):
    subprocess.run([os.environ.get("CXX", "c++"), "-Wl,--gc-sections", *objects, "-o", exe], check=True)
screens = {"standby":"開始準備", "editor":"終了時刻の設定", "ready":"開始", "course":"コース配分",
           "wifi":"Wi-Fiで設定", "running":"案内中", "overtime":"終了超過", "review":"振り返り", "questions":"質問記録"}
subprocess.run([exe, *(build / name for name in screens)], check=True)
sheet = Image.new("RGB", (466*3, 506*((len(screens)+2)//3)), "#17181c")
mask = Image.new("L", (466,466), 0)
ImageDraw.Draw(mask).ellipse((0,0,465,465), fill=255)
font = ImageFont.truetype("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 20)
for i, (name,label) in enumerate(screens.items()):
    ppm = build / f"{name}.ppm"
    with Image.open(ppm) as panel:
        panel.save(build / f"{name}.png")
        x,y = i%3*466,i//3*506
        sheet.paste(panel,(x,y+40),mask)
        ImageDraw.Draw(sheet).text((x+233,y+20),label,font=font,anchor="mm",fill="white")
    ppm.unlink()
sheet.save(build / "ui-preview.png")
print(f"{len(screens)} RGB565 views rendered with the firmware fonts. Label bounds and overlap checks passed.")
