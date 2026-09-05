"""Compile/run production UI on the host; export actual U8g2 framebuffers.
Requires: python -m pip install ziglang pillow; python -m platformio run
"""
import os
from pathlib import Path
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
BUILD = ROOT / "build" / "host"
BUILD.mkdir(parents=True, exist_ok=True)
(ROOT / "build/preview").mkdir(parents=True, exist_ok=True)
LIB = ROOT / ".pio/libdeps/agentdeck/U8g2/src"
if not LIB.exists():
    raise SystemExit("Run python -m platformio run first to fetch pinned U8g2.")
ZIG = [sys.executable, "-m", "ziglang"]

def compile_command(command):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        sys.stdout.buffer.write(result.stdout[-12000:])
        raise SystemExit(result.returncode)
    if result.stdout:
        # Keep upstream compiler diagnostics available without flooding the terminal.
        with (BUILD / "compiler.log").open("ab") as log:
            log.write(result.stdout)

# Reuse unmodified upstream C objects. No Arduino or OLED transport stubs in
# production code: the harness supplies U8g2's ordinary transport callbacks.
sources = sorted((LIB / "clib").glob("*.c"))
stale = [p for p in sources if not (BUILD / (p.stem + ".o")).exists()
         or (BUILD / (p.stem + ".o")).stat().st_mtime < p.stat().st_mtime]
if stale:
    def compile_c(path):
        compile_command(ZIG + ["cc", "-O1", "-c", "-ffunction-sections", "-fdata-sections",
                              str(path), "-o", str(BUILD / (path.stem + ".o"))])
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(compile_c, stale))
cpp = [ROOT / "tests/host.cpp", ROOT / "src/ui/ScreenManager.cpp",
       ROOT / "src/ui/Renderer.cpp", ROOT / "tests/fixtures/MockService.cpp"]
cpp += sorted((ROOT / "src/screens").glob("*.cpp"))
exe = BUILD / ("check.exe" if os.name == "nt" else "check")
compile_command(ZIG + ["c++", "-std=c++17", "-O1", "-Wall", "-Wextra",
                     "-I" + str(ROOT / "src"), "-I" + str(LIB)]
               + [str(p) for p in cpp] + [str(BUILD / (p.stem + ".o")) for p in sources]
               + ["-o", str(exe)])
subprocess.run([str(exe)], check=True)

from PIL import Image, ImageDraw, ImageOps
frames = sorted((ROOT / "build/preview").glob("[0-9]*.pbm"))
sheet = Image.new("RGB", (4*416, ((len(frames)+3)//4)*244), "#16181b")
draw = ImageDraw.Draw(sheet)
for i, path in enumerate(frames):
    im = ImageOps.invert(Image.open(path).convert("L"))
    im.save(path.with_suffix(".png"))
    x, y = (i%4)*416+16, (i//4)*244+30
    sheet.paste(im.resize((384,192), Image.Resampling.NEAREST),(x,y))
    draw.text((x,y-20),path.stem,fill="#bfc8cc")
sheet.save(ROOT / "build/preview/contact-sheet.png")
print("Preview: build/preview/contact-sheet.png")
movie = [ImageOps.invert(Image.open(path).convert("L")).resize((512,256), Image.Resampling.NEAREST)
         for path in sorted((ROOT / "build/preview").glob("movie-*.pbm"))]
if movie:
    movie[0].save(ROOT / "build/preview/buddy-motion.gif", save_all=True,
                  append_images=movie[1:], duration=33, loop=0, disposal=2)
    print("Motion preview: build/preview/buddy-motion.gif")
