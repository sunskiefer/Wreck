#!/usr/bin/env python3
"""Wreck: let the skin builder emit a rotary knob filmstrip with fewer than 128 frames when the knob is large.
MPC garbles filmstrips taller than ~16384 px (docs/NOTES.md); the mock-up's 176 px knob at 128 frames would be 22528 px,
so a big knob gets as many frames as fit in 12288 px (176 px -> 69; Wreck's art has 64). Small knobs are unchanged (still 128).
Idempotent. usage: patch_builder.py <mpc-vst-plugins checkout>"""
import sys, os
p = os.path.join(sys.argv[1], "tools", "shadow_skin.py")
s = open(p).read()
if "def knob_frames(" in s:
    print("builder already patched"); sys.exit(0)
reps = [
 ("def strip_frames(h, want=FRAMES):",
  "KNOB_AREA_MAX = 2040000     # px: 126 x 16128 (128 frames, r=58) is device-proven; larger strips scrolled on a Force\n\n\ndef knob_frames(r):\n    \"\"\"Wreck patch: frames of a rotary knob strip: 128, fewer when 128 frames would pass 12288 px.\"\"\"\n    sz = 2 * r + 10\n    return max(2, min(FRAMES, 16384 // sz, KNOB_AREA_MAX // (sz * sz)))\n\n\ndef strip_frames(h, want=FRAMES):"),
 ('"numFrames": ROT_FRAMES, "invert": False, "dragOrientation": "Vertical",\n                                  "handleName": "Data"}, _bounds(0, (bh_ - s_) // 2, s_, s_), "Knob"),',
  '"numFrames": knob_frames(r) - 1, "invert": False, "dragOrientation": "Vertical",\n                                  "handleName": "Data"}, _bounds(0, (bh_ - s_) // 2, s_, s_), "Knob"),'),
 ('"numFrames": ROT_FRAMES, "invert": False, "dragOrientation": "Vertical",\n                                  "handleName": "Data"}, _bounds((cw - s) // 2, 0, s, s), "Knob"),',
  '"numFrames": knob_frames(r) - 1, "invert": False, "dragOrientation": "Vertical",\n                                  "handleName": "Data"}, _bounds((cw - s) // 2, 0, s, s), "Knob"),'),
 ("if s * FRAMES > 16384:", "if s * knob_frames(r) > 16384:"),
 ('script.append("lstrip|%s|%d|%d|%s" % (art("sh_knob_r%d_%s" % (r, lid)), r, FRAMES, skin_assets.encode(looks[lid])))',
  'script.append("lstrip|%s|%d|%d|%s" % (art("sh_knob_r%d_%s" % (r, lid)), r, knob_frames(r), skin_assets.encode(looks[lid])))'),
 ('script.append("strip|%s|%d|%d|%s" % (art("sh_knob_r%d" % r), r, FRAMES, under()))',
  'script.append("strip|%s|%d|%d|%s" % (art("sh_knob_r%d" % r), r, knob_frames(r), under()))'),
]
for a, b in reps:
    assert s.count(a) == 1, "patch anchor not found exactly once: " + a[:60]
    s = s.replace(a, b)
open(p, "w").write(s)
print("builder patched")
