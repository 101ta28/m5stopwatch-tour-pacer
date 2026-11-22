#!/usr/bin/env python3
"""Render actual watch sprites from the snapshots written by scripts/test.py."""
from pathlib import Path
import os
import re
import subprocess
import sys
from PIL import Image, ImageDraw
from ui_fonts import noto_font

root = Path(__file__).resolve().parents[1]
build = root / "build"
gfx = Path(os.environ.get("M5GFX_SRC", root / ".arduino/user/libraries/M5GFX/src")).resolve()
if not (gfx / "lgfx/v1/LGFXBase.cpp").exists():
    raise SystemExit("M5GFX が見つかりません。M5GFX_SRC にライブラリの src を指定してください。")
v1 = gfx / "lgfx/v1"
ui = root / "StopWatchTourGuide/src"
sdl_root = os.environ.get("SDL2_ROOT")
use_sdl = bool(sdl_root) or not sys.platform.startswith("linux")
cflags = ["-DLGFX_SDL"] if use_sdl else ["-DLGFX_LINUX_FB"]
libs = ["-lSDL2"] if use_sdl else []
if sdl_root:
    cflags += ["-I", str(Path(sdl_root) / "include")]
    libs = ["-L", str(Path(sdl_root) / "lib"), *libs]
linker = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
cache = build / ("preview-objects-" + sys.platform + "-sdl" if use_sdl else "preview-objects")
cache.mkdir(parents=True, exist_ok=True)
sources = [root / "tests/render.cpp", ui / "WatchUi.cpp",
           *(v1 / f"{name}.cpp" for name in
             ("LGFXBase", "lgfx_fonts", "lgfx_v1", "lgfx_v1_panel", "lgfx_v1_platforms")),
           gfx / "lgfx/Fonts/IPA/lgfx_font_japan.c"]
if use_sdl:
    # PE and Mach-O linkers also resolve references from graphics/font tables
    # that the ELF linker discards when linking the framebuffer preview.
    sources += sorted((gfx / "lgfx/utility").glob("lgfx_*.c"))
    sources += sorted((gfx / "lgfx/Fonts/efont").glob("lgfx_efont_*.c"))
objects = []
cc = os.environ.get("CC", "gcc" if os.name == "nt" else "cc")
cxx = os.environ.get("CXX", "g++" if os.name == "nt" else "c++")
for src in sources:
    obj = cache / (src.stem + ".o")
    # Make-style dependencies include the actual font assets and M5GFX inlines.
    dep = obj.with_suffix(".d")
    deps = []
    if dep.exists():
        rule = dep.read_text(encoding="utf-8", errors="replace").replace("\\\n", " ").partition(":")[2]
        deps = [p.replace("\\ ", " ") for p in re.findall(r"(?:\\ |[^\s])+", rule)]
    if not obj.exists() or not deps or any(not Path(p).exists() or Path(p).stat().st_mtime > obj.stat().st_mtime for p in deps):
        flags = [] if src.suffix == ".c" else ["-std=c++17"]
        subprocess.run([cc if src.suffix == ".c" else cxx, *flags, "-O2", "-ffunction-sections", "-fdata-sections",
                        *cflags, "-I", str(gfx), "-MT", "object", "-MMD", "-MF", str(dep),
                        "-c", str(src), "-o", str(obj)], check=True)
    objects.append(obj)
exe = cache / ("render.exe" if os.name == "nt" else "render")
if not exe.exists() or any(p.stat().st_mtime > exe.stat().st_mtime for p in objects):
    subprocess.run([cxx, linker, *objects, *libs, "-o", exe], check=True)
screens = {"standby":"開始準備", "editor":"終了時刻の設定", "ready":"開始", "course":"コース配分",
           "wifi":"Wi-Fiで設定", "running":"案内中", "overtime":"終了超過", "review":"振り返り", "questions":"質問記録"}
subprocess.run([exe, *screens], cwd=build, check=True)
sheet = Image.new("RGB", (466*3, 506*((len(screens)+2)//3)), "#17181c")
mask = Image.new("L", (466,466), 0)
ImageDraw.Draw(mask).ellipse((0,0,465,465), fill=255)
font = noto_font(20, "Regular")
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
