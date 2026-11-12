# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Builds dist/qt_themes/ris (RIS, PantACRO4life) from the Lichii Dark theme.
# The stylesheet and icons are Lichii Dark recolored to the RIS palette (near black steel, signal
# red, square corners); the Lichii Modern library rules are appended. The brand images get the
# red rim glow of the RIS boot logos so the black glyph reads on the dark background.
# Needs ImageMagick 7 (magick) with the librsvg delegate. Python standard library only.
#   python tools/ui/make_ris_theme.py <ris logo png> <ris wordmark png> <font dir>
# Defaults: the RIS site logo, the uLaunch RIS wordmark and its Chakra Petch fonts.
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
THEMES = os.path.join(ROOT, "dist", "qt_themes")
LICHII = os.path.join(THEMES, "lichii")
MODERN = os.path.join(THEMES, "lichii_modern")
OUT = os.path.join(THEMES, "ris")

LOGO = sys.argv[1] if len(sys.argv) > 1 else "D:/.devops/Web/React/RIS/public/img/ris-logo.png"
WORDMARK = sys.argv[2] if len(sys.argv) > 2 else "D:/.devops/App/uLaunch-RIS/ris/theme/ris-wordmark.png"
FONTS = sys.argv[3] if len(sys.argv) > 3 else "D:/.devops/App/uLaunch-RIS/ris/theme"

# Lichii Dark token -> RIS token (Web/React/RIS/src/styles.css)
COLORS = {
    "#17181b": "#0f1114",  # window       -> steel-850
    "#131416": "#0a0b0d",  # chrome       -> steel-900
    "#141518": "#0b0c0e",  # sunken
    "#1b1c20": "#121418",  # header
    "#202125": "#14171b",  # surface      -> steel-800
    "#212226": "#171a1f",  # divider
    "#232429": "#1c2026",  # raised       -> steel-700
    "#2a2c31": "#22262d",  # hover
    "#2a2b30": "#252a31",  # border-subtle
    "#303238": "#2a2f37",  # pressed      -> steel-600
    "#34363c": "#3a414b",  # border       -> steel-500
    "#4a4c53": "#4a525d",
    "#55575d": "#5a626d",  # disabled
    "#6f7178": "#6f7782",  # faint
    "#9b9da4": "#8b939f",  # muted        -> smoke
    "#b4b6bc": "#aab1bb",  # menu icons
    "#c4c5ca": "#c0c5cd",  # secondary
    "#d6d7db": "#d8dce2",  #              -> bone
    "#ececef": "#f2f4f7",  # text         -> white
    "#f0386b": "#e01b31",  # accent       -> red
    "#f45a85": "#ff3347",  # accent hover -> red-hot
    "#d42c5a": "#b8162a",  # accent pressed
    "#3a1d28": "#3a1218",  # accent on dark
    "#5cb87a": "#a8d13a",  # success      -> acid
    "#d9a441": "#f0a02a",  # warning      -> amber
}
RGBA = {
    r"rgba\(\s*240\s*,\s*56\s*,\s*107\s*,": "rgba(224, 27, 49,",
    r"rgba\(\s*56\s*,\s*189\s*,\s*225\s*,": "rgba(224, 27, 49,",
    r"rgba\(\s*1\s*,\s*253\s*,\s*201\s*,": "rgba(255, 51, 71,",
}
# Lichii radius 5 / 7 / 10 -> RIS 2 / 3 / 4: the brand uses square corners.
RADII = {10: 4, 8: 4, 7: 3, 6: 3, 5: 2}


def recolor(text):
    def hex_sub(m):
        return COLORS.get(m.group(0).lower(), m.group(0))

    text = re.sub(r"#[0-9a-fA-F]{6}\b", hex_sub, text)
    for pattern, repl in RGBA.items():
        text = re.sub(pattern, repl, text)
    return text


def square(text):
    return re.sub(r"(border-radius:\s*)(\d+)px",
                  lambda m: m.group(1) + str(RADII.get(int(m.group(2)), int(m.group(2)))) + "px", text)


def magick(*args):
    subprocess.run(["magick", *args], check=True)


def render_svg(svg_path, png_base):
    for suffix, density in (("", 96), ("@2x", 192)):
        magick("-background", "none", "-density", str(density), "rsvg:" + svg_path, "-depth", "8",
               png_base + suffix + ".png")


def glow(src, dst, height):
    # Black glyph -> thin red-hot rim and a soft red glow behind it, like the RIS boot logos.
    pad = max(8, height // 10)
    magick(src, "-trim", "+repage", "-resize", "x%d" % (height - 2 * pad),
           "-bordercolor", "none", "-border", str(pad),
           # soft glow
           "(", "-clone", "0", "-channel", "A", "-morphology", "Dilate", "Disk:%d" % max(3, height // 48),
           "-blur", "0x%d" % max(5, height // 20), "-evaluate", "multiply", "0.85", "+channel",
           "-fill", "#e01b31", "-colorize", "100", ")",
           # thin rim
           "(", "-clone", "0", "-channel", "A", "-morphology", "Dilate", "Disk:%d" % max(1, height // 170),
           "+channel", "-fill", "#ff3347", "-colorize", "100", ")",
           # order: glow, rim, glyph
           "(", "-clone", "0", ")", "-delete", "0",
           "-background", "none", "-compose", "over", "-flatten", dst)


def main():
    os.makedirs(os.path.join(OUT, "svg"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "icons"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "brand"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "fonts"), exist_ok=True)

    # Stylesheet: Lichii Dark + Lichii Modern library rules, recolored, plus ris_extra.qss.
    with open(os.path.join(LICHII, "style.qss"), encoding="utf-8") as f:
        base = f.read()
    with open(os.path.join(MODERN, "style.qss"), encoding="utf-8") as f:
        modern = f.read()
    base = base.replace("Lichii Dark. Tokens, from the design's component sheet:",
                        "RIS (PantACRO4life), generated from Lichii Dark by tools/ui/make_ris_theme.py.\n"
                        "   Lichii Dark tokens, remapped to the RIS palette:")
    qss = square(recolor(base + "\n" + modern)).replace(":/lichii/icons/", ":/ris/icons/")
    extra_path = os.path.join(OUT, "ris_extra.qss")
    if os.path.isfile(extra_path):
        with open(extra_path, encoding="utf-8") as f:
            qss += "\n" + f.read()
    with open(os.path.join(OUT, "style.qss"), "w", encoding="utf-8", newline="\n") as f:
        f.write(qss)

    # Icons: same names as Lichii Dark so code can switch the root folder.
    icons = []
    for name in sorted(os.listdir(os.path.join(LICHII, "svg"))):
        if not name.endswith(".svg"):
            continue
        with open(os.path.join(LICHII, "svg", name), encoding="utf-8") as f:
            svg = recolor(f.read())
        if name.startswith(("check_", "radio_")):
            svg = re.sub(r'rx="5"', 'rx="2"', svg)
        dst = os.path.join(OUT, "svg", name)
        with open(dst, "w", encoding="utf-8", newline="\n") as f:
            f.write(svg)
        stem = name[:-4]
        render_svg(dst, os.path.join(OUT, "icons", stem))
        icons += [stem + ".png", stem + "@2x.png"]
        if stem.startswith("menu_"):
            white = os.path.join(OUT, "svg", stem + "_white.tmp.svg")
            with open(white, "w", encoding="utf-8", newline="\n") as f:
                f.write(re.sub(r"#[0-9a-fA-F]{6}", "#ffffff", svg))
            render_svg(white, os.path.join(OUT, "icons", stem + "_white"))
            os.remove(white)
            icons += [stem + "_white.png", stem + "_white@2x.png"]

    # Brand images.
    glow(LOGO, os.path.join(OUT, "brand", "ris_mark.png"), 512)
    glow(WORDMARK, os.path.join(OUT, "brand", "ris_wordmark.png"), 256)
    fonts = []
    for font in ("ChakraPetch-SemiBold.ttf", "ChakraPetch-Bold.ttf"):
        shutil.copyfile(os.path.join(FONTS, font), os.path.join(OUT, "fonts", font))
        fonts.append(font)

    lines = [
        "<!--",
        "SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project",
        "SPDX-License-Identifier: GPL-3.0-or-later",
        "-->",
        "",
        "<!-- Generated by tools/ui/make_ris_theme.py. -->",
        "<RCC>",
        '    <qresource prefix="icons/ris">',
        '        <file alias="index.theme">index.theme</file>',
        "    </qresource>",
        '    <qresource prefix="ris">',
        "        <file>style.qss</file>",
        "        <file>brand/ris_mark.png</file>",
        "        <file>brand/ris_wordmark.png</file>",
    ]
    lines += ["        <file>fonts/%s</file>" % f for f in fonts]
    lines += ["        <file>icons/%s</file>" % i for i in sorted(icons)]
    lines += ["    </qresource>", "</RCC>", ""]
    with open(os.path.join(OUT, "style.qrc"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    with open(os.path.join(OUT, "index.theme"), "w", encoding="utf-8", newline="\n") as f:
        f.write("[Icon Theme]\nName=ris\nComment=RIS (PantACRO4life)\nInherits=lichii\n"
                "Directories=16x16\n\n[16x16]\nSize=16\n")


if __name__ == "__main__":
    main()
