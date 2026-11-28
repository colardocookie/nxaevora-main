#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Makes the icon subset font of the engine menu (docs/dev/ui/engine-menu.md).

    tools/subset_icon_font.py <output font> --cache <folder>

Downloads MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf of google/material-design-icons at the pinned commit, checks
its SHA-256, fixes the axes to FILL 0, GRAD 0, opsz 24 and wght 400, and keeps only the code points of the icons that
the menu draws. The output is an Apache 2.0 font; the source and the license are named in assets/fonts/README.md.
The tool writes nothing outside the cache folder and the output file.
"""

import argparse
import hashlib
import shutil
import sys
import urllib.request
from pathlib import Path

# google/material-design-icons, the newest commit that changed variablefont/ (2026-10-23, "Update Symbols").
SOURCE_COMMIT = "10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7"
SOURCE_NAME = "MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf"
SOURCE_URL = (
    "https://raw.githubusercontent.com/google/material-design-icons/"
    f"{SOURCE_COMMIT}/variablefont/MaterialSymbolsRounded%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf"
)

# The SHA-256 of SOURCE_NAME at SOURCE_COMMIT. Fill it from the first download; the tool refuses a file that differs.
PINNED_SHA256 = "7e74e36533ccee12c2b81e75bfcf3ccf123d3bb7e7e46d65d94445f23db0c97c"

# The instance of the variable font: the filled, ungraded 24 px optical size at the Regular weight.
AXIS_VALUES = {"FILL": 0.0, "GRAD": 0.0, "opsz": 24.0, "wght": 400.0}

# The code points of the icons of the menu, one row for each constant of md3::icon
# (engine/src/nx/gpu/vk/md3/md3_widgets.h). The comment gives the Material Symbols name of the icon.
ICONS = {
    "kGame": 0xEA28,              # sports_esports
    "kDisplay": 0xE30C,           # desktop_windows
    "kGraphics": 0xE40A,          # palette
    "kPerformance": 0xE9E4,       # speed
    "kMouse": 0xE323,             # mouse
    "kKeyboard": 0xE312,          # keyboard
    "kControllers": 0xE30F,       # gamepad
    "kOnScreenControls": 0xE913,  # touch_app
    "kDebug": 0xE868,             # bug_report
    "kAbout": 0xE88E,             # info
    "kClose": 0xE5CD,             # close
    "kBuild": 0xF8CD,             # build (wrench)
    "kRestart": 0xE5D5,           # refresh
    "kArrowDropDown": 0xE5C5,     # arrow_drop_down
    "kArrowDropUp": 0xE5C7,       # arrow_drop_up
    "kExpandMore": 0xE5CF,        # expand_more
}


def fetch_source(cache_dir):
    """Returns the cached source font, and downloads it when the cache has no copy."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    source = cache_dir / SOURCE_NAME
    if source.is_file():
        return source
    request = urllib.request.Request(SOURCE_URL, headers={"User-Agent": "nxaevora-subset-icon-font"})
    print(f"download {SOURCE_URL}")
    with urllib.request.urlopen(request) as response, source.open("wb") as out:
        shutil.copyfileobj(response, out)
    return source


def check_hash(source):
    """Stops when the source font is not the pinned file."""
    if PINNED_SHA256 == "TODO-SHA256":
        sys.exit("subset_icon_font: PINNED_SHA256 is not filled; the source is not checked")
    digest = hashlib.sha256()
    with source.open("rb") as font:
        for block in iter(lambda: font.read(1 << 20), b""):
            digest.update(block)
    if digest.hexdigest() != PINNED_SHA256:
        sys.exit(f"subset_icon_font: {source} has SHA-256 {digest.hexdigest()}, expected {PINNED_SHA256}")


def write_subset(source, output):
    """Fixes the axes and writes the font with the code points of ICONS."""
    try:
        from fontTools import subset
        from fontTools.ttLib import TTFont
        from fontTools.varLib.instancer import instantiateVariableFont
    except ImportError as error:
        sys.exit(f"subset_icon_font: fontTools is missing ({error}); install it with pip")

    # Keep head.modified of the source, so the output is the same on each run.
    font = TTFont(source, recalcTimestamp=False)
    instantiateVariableFont(font, AXIS_VALUES, inplace=True)
    options = subset.Options()
    options.layout_features = []  # the icons need no OpenType feature
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=list(ICONS.values()))
    subsetter.subset(font)
    font.save(str(output))


def main():
    parser = argparse.ArgumentParser(description="Make the icon subset font of the engine menu.")
    parser.add_argument("output", type=Path, help="the output font, assets/fonts/MaterialSymbolsRounded-Subset.ttf")
    parser.add_argument("--cache", type=Path, required=True, help="the folder of the downloaded source font")
    args = parser.parse_args()

    source = fetch_source(args.cache)
    check_hash(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_subset(source, args.output)
    print(f"wrote {args.output} ({args.output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
