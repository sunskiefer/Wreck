#!/usr/bin/env python3
"""Pseudo X/Y pad (L'Cronx): after gen_vst.py has built the skin, paint the page's two marker pixel blocks into the background picture.

The plugin finds XYZ's page on the display by them (src/xyz_pad.c, the same method as VADER's live screen): 2 x 2 opaque pixels at
the page's top-left and bottom-right corners, in colours no other page uses. The pad is shown only while both are visible.
Usage: skin_post.py <skin dir>  (the "Plugin Skins" folder's parent or the folder itself)."""
import json
import os
import sys

from PIL import Image

MARK_TL = [(23, 7, 91), (91, 7, 23), (7, 91, 23), (23, 91, 7)]
MARK_BR = [(91, 23, 7), (7, 23, 91), (61, 7, 61), (7, 61, 61)]


def find_skins(d):
    p = os.path.join(d, "Plugin Skins")
    return p if os.path.isdir(p) else d


# drag mode per knob (component name -> dragOrientation), read from drag.json in the plugin folder if there is one.
# MPC's own values: Vertical, Horizontal, Circular (see README, "Drag modes"). No drag.json: every knob keeps the generator's Vertical.
DRAG = json.load(open("drag.json")) if os.path.exists("drag.json") else {}


def set_drag(tui):
    """Give every knob its own copy of the knob definition with its drag mode (the generator makes one shared, always Vertical)."""
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    byk = {d["key"]: d for d in defs}
    n = 0
    for d in list(defs):
        for c in d["value"].get("componentsData", []):
            cd = c["componentData"]
            mode = DRAG.get(cd.get("name"))
            if mode is None or cd.get("type") not in byk or not cd["type"].startswith("shKnob"):
                continue
            key = cd["type"].split("|")[0] + "_" + mode
            if key not in byk:
                nd = json.loads(json.dumps(byk[cd["type"]]))
                nd["key"] = key
                for cc in nd["value"]["componentsData"]:
                    if cc["componentData"].get("type") == "Knob":
                        cc["componentData"]["data"]["dragOrientation"] = mode
                defs.append(nd)
                byk[key] = nd
            cd["type"] = key
            n += 1
    if n != len(DRAG):
        sys.exit("skin_post: expected %d knobs, found %d" % (len(DRAG), n))


def main():
    sk = find_skins(sys.argv[1])
    tui = json.load(open(os.path.join(sk, "TUI.json")))
    if DRAG:
        set_drag(tui)
        json.dump(tui, open(os.path.join(sk, "TUI.json"), "w"), indent=2)
    done = 0
    marked = set()
    for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]:
        for c in d["value"].get("componentsData", []):
            cd = c["componentData"]
            if cd.get("name") == "Background" and cd.get("type") == "Image":
                png = os.path.join(sk, cd["data"]["image"])
                if png in marked:
                    continue
                marked.add(png)
                im = Image.open(png).convert("RGB")
                w, h = im.size
                assert (w, h) == (1280, 628), (w, h)
                for (x0, y0, cols) in ((0, 0, MARK_TL), (w - 2, h - 2, MARK_BR)):
                    for k, col in enumerate(cols):
                        im.putpixel((x0 + k % 2, y0 + k // 2), col)
                im.save(png)
                done += 1
    if done < 1:
        sys.exit("skin_post: no page background found")
    print("skin_post: markers written into %d page backgrounds" % done)


main()
