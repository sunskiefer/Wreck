#!/usr/bin/env python3
"""Wreck (L'Cronx): one source of truth for the parameter list. Writes params.json, layout.conf, presets.json and
src/wparams.h (the engine's enum and tables) so the four can never disagree.   usage: make_data.py   (run from wreck/)"""
import json
import os

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# display formats the engine knows (wparams.h F_*): the engine writes the text for a knob; the value stays a plain number
FMT = ["PCT", "RATE", "SPEEDX", "ST", "MS", "HZ", "TIME", "DB", "BIPCT", "CENTS", "INT", "LOFI", "NAME", "STEPS", "NUM", "CHIP", "WINFO", "WPATH"]

P = []          # (key, name, spec)


def knob(key, name, lo, hi, df, fmt="PCT"):
    P.append(dict(key=key, name=name, min=lo, max=hi, default=df, dynamic_display=True, fmt=fmt))


def ival(key, name, lo, hi, df, fmt="INT", dyn=True):
    P.append(dict(key=key, name=name, min=lo, max=hi, default=df, display="int", dynamic_display=dyn, fmt=fmt))


def opt(key, name, options, df=0):
    P.append(dict(key=key, name=name, options=options, default=df, fmt="OPT"))


def tog(key, name, df=0):
    P.append(dict(key=key, name=name, options=["Off", "On"], default=df, fmt="OPT"))


def trig(key, name):
    P.append(dict(key=key, name=name, min=0, max=1, momentary=True, fmt="TRIG"))


def optstep(key, name, options, df=0):
    """an option with < > arrows (the skin's stepper): the wrapper steps it through <key>_prev / <key>_next"""
    opt(key, name, options, df)
    P.append(dict(key=key + "_prev", name=name + " Prev", min=0, max=1, momentary=True, step_of=key, step_delta=-1, fmt="TRIG"))
    P.append(dict(key=key + "_next", name=name + " Next", min=0, max=1, momentary=True, step_of=key, step_delta=1, fmt="TRIG"))


def slot(key, name, df=0):
    P.append(dict(key=key, name=name, min=0, max=1, type="slot", default=df, display="int", fmt="SLOT"))


def stepper(key, name, lo, hi, df, fmt):
    ival(key, name, lo, hi, df, fmt)
    P.append(dict(key=key + "_prev", name=name + " Prev", min=0, max=1, momentary=True, step_of=key, step_delta=-1, fmt="TRIG"))
    P.append(dict(key=key + "_next", name=name + " Next", min=0, max=1, momentary=True, step_of=key, step_delta=1, fmt="TRIG"))


SRC = ["NOISE", "CLICKS", "WAV", "FM PING", "HARMONOGRAPH", "CLIFFORD", "SUPERFORMULA", "SPIROGRAPH", "MOIRE", "ATTRACTORS"]
BLK = ["DRIVE", "ECHO", "FILTER", "SHIFT", "SMEAR"]
DIV = ["1/32", "1/16", "1/8", "1/4", "1/2", "1/1"]

# ---- INPUT
opt("src", "Source", SRC, 0)
stepper("att", "Attractor", 1, 15, 1, "INT")
stepper("wav", "WAV file", 0, 1023, 0, "NAME")
# the SAMPLES page: a file browser. eight rows (the page of eight that holds the cursor: folders and WAV files), rescan, use as source, count
for i in range(8):
    P.append(dict(key="wl_%d" % (i + 1), name="WAV Row %d" % (i + 1), min=0, max=1, default=0, display="string", fmt="NUM"))
trig("wav_scan", "Rescan WAV folder")
trig("wav_use", "Use WAV as source")
P.append(dict(key="wav_info", name="WAV count", min=0, max=1, default=0, dynamic_display=True, fmt="WINFO"))
knob("src_level", "Source Level", 0, 1, 0.8)
knob("src_rate", "Source Rate", 0, 1, 0.5, "RATE")
knob("fb", "Feedback", 0, 1, 0.0)
tog("src_prev", "Preview")
tog("src_gate", "Gate")
tog("src_tune", "Tune")
# ---- CHAIN
for i in range(5):
    opt("slot%d" % (i + 1), "Position %d" % (i + 1), BLK, i)
for i, d in enumerate([1, 1, 1, 0, 1]):
    tog("b%d_on" % (i + 1), "%s On" % BLK[i].title(), d)
# ---- BLOCKS
knob("b1_drive", "Drv Drive", 0, 1, 0.45)
knob("b1_tone", "Drv Tone", 0, 1, 0.6)
knob("b1_bias", "Drv Bias", -1, 1, 0.0, "BIPCT")
knob("b1_mix", "Drv Mix", 0, 1, 0.8)
knob("b1_level", "Drv Level", 0, 1, 0.75, "DB")
knob("b2_time", "Ech Time", 0, 1, 0.45, "TIME")
knob("b2_fb", "Ech Feedback", 0, 1, 0.45)
knob("b2_tone", "Ech Tone", 0, 1, 0.6)
knob("b2_mix", "Ech Mix", 0, 1, 0.4)
knob("b2_spread", "Ech Spread", 0, 1, 0.3)
knob("b3_cut", "Flt Cut", 0, 1, 0.55, "HZ")
knob("b3_res", "Flt Res", 0, 1, 0.35)
knob("b3_env", "Flt Env", -1, 1, 0.0, "BIPCT")
knob("b3_mix", "Flt Mix", 0, 1, 1.0)
knob("b3_level", "Flt Level", 0, 1, 0.75, "DB")
ival("b4_pitch", "Sft Pitch", -24, 24, 7, "ST")
ival("b4_fine", "Sft Fine", -50, 50, 0, "CENTS")
knob("b4_fb", "Sft Feedback", 0, 1, 0.3)
knob("b4_mix", "Sft Mix", 0, 1, 0.5)
knob("b4_level", "Sft Level", 0, 1, 0.75, "DB")
knob("b5_size", "Smr Size", 0, 1, 0.5)
knob("b5_decay", "Smr Decay", 0, 1, 0.55)
knob("b5_damp", "Smr Damp", 0, 1, 0.5)
knob("b5_mix", "Smr Mix", 0, 1, 0.35)
knob("b5_level", "Smr Level", 0, 1, 0.75, "DB")
# ---- VOICES
knob("v_speed", "Voice Speed", 0, 1, 0.5, "SPEEDX")
ival("v_pitch", "Voice Pitch", -24, 24, 0, "ST")
opt("v_dir", "Voice Direction", ["FWD", "REV", "PING", "RAND"], 0)
knob("v_att", "Voice Attack", 0, 1, 0.2, "MS")
knob("v_rel", "Voice Release", 0, 1, 0.6, "MS")
opt("v_mode", "Pitch Mode", ["FREE", "KEY", "LOCK", "PLAY"], 1)
stepper("v_count", "Voices", 1, 16, 16, "INT")
# ---- OUTPUT
opt("o_ball", "Ball Time", DIV, 2)
knob("o_bounce", "Ball Bounce", 0, 1, 0.4)
knob("o_lofi", "Lo-Fi Rate", 0, 1, 0.884, "LOFI")
opt("o_stut", "Stutter Size", DIV[:4], 1)
knob("o_mix", "Out Mix", 0, 1, 0.5)
tog("o_ball_on", "Ball On", 0)
tog("o_lofi_on", "Lo-Fi On", 0)
tog("o_stut_on", "Stutter On", 0)
tog("o_sync", "Sync", 1)
# ---- ARP
tog("a_on", "Arp On", 0)
tog("a_latch", "Arp Latch", 0)
optstep("a_rate", "Arp Rate", ["1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"], 3)
optstep("a_range", "Arp Range", ["1 OCT", "2 OCT", "3 OCT", "4 OCT"], 1)
optstep("a_order", "Arp Order", ["UP", "DOWN", "UP-DN", "RANDOM", "PLAYED"], 0)
LANES = [("n", "Note", 16, [1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1]),
         ("v", "Vel", 5, [1, 1, 0, 1, 1] + [0] * 11),
         ("g", "Gate", 7, [1, 0, 1, 1, 0, 0, 1] + [0] * 9),
         ("o", "Oct", 3, [1, 0, 1] + [0] * 13)]
for k, nm, ln, pat in LANES:
    stepper("a_%slen" % k, "Arp %s Length" % nm, 1, 16, ln, "STEPS")
for k, nm, ln, pat in LANES:
    for s in range(16):
        slot("a_%s_%d" % (k, s + 1), "Arp %s %d" % (nm, s + 1), pat[s])
# ---- MACROS
knob("m_chaos", "Chaos", 0, 1, 0.0)
knob("m_space", "Space", 0, 1, 0.0)
knob("m_grit", "Grit", 0, 1, 0.0)
knob("m_body", "Body", 0, 1, 0.0)
# ---- MONITOR / MASTER
opt("mon", "Monitor", ["OFF", "SRC", "B1", "B2", "B3", "B4", "B5", "LIM"], 0)
for i, n in enumerate(["SRC", "B1", "B2", "B3", "B4", "B5", "LIM"]):
    tog("mon_t%d" % i, "Mon %s" % n, 0)     # the skin's MON buttons: one of them on = mon is that tap (derived from mon by the engine)
for i, n in enumerate(["SRC", "B1", "B2", "B3", "B4", "B5", "LIM"]):
    tog("solo%d" % i, "Solo %s" % n, 0)
knob("direct", "Direct", 0, 1, 0.0)
knob("level", "Level", 0, 1, 0.75, "DB")
# ---- chain strip readouts (read-only text: the level of each stage, OFF for a block that is off)
for i, n in enumerate(["SRC", "B1", "B2", "B3", "B4", "B5", "LIM"]):
    P.append(dict(key="chip%d" % i, name="Stage %s" % n, min=0, max=1, default=0, dynamic_display=True, fmt="CHIP"))
# ---- PRESETS
opt("p_bank", "Bank", ["FACTORY", "USER"], 0)
stepper("p_sel", "Preset", 0, 999, 0, "NAME")
trig("p_load", "Load")
trig("p_save", "Save")
trig("p_init", "Init")
# ---- SAMPLES browser (appended last so saved projects keep their parameter positions): UP, ROOT, BACK, FWD, OPEN and the folder path
trig("wav_up", "Folder up")
trig("wav_root", "Folder root")
trig("wav_back", "Folder back")
trig("wav_fwd", "Folder forward")
trig("wav_open", "Open or load")
P.append(dict(key="wav_path", name="Folder path", min=0, max=1, default=0, dynamic_display=True, fmt="WPATH"))

KEYS = [p["key"] for p in P]
assert len(KEYS) == len(set(KEYS)), "duplicate key"
IDX = {k: i for i, k in enumerate(KEYS)}
PBY = {p["key"]: p for p in P}


def num(p, v):
    if "options" in p:
        o = p["options"]
        return o.index(v) if isinstance(v, str) else v
    return v


# ======================================================================================================== presets (22)
# every preset is a set of overrides on the defaults; values in natural units, options by name
PRESETS = [
    ("Rust Bucket", dict(src="NOISE", src_rate=0.35, fb=0.45, b1_drive=0.7, b1_mix=1.0, b2_mix=0.3, b2_fb=0.5, b3_cut=0.4, b3_res=0.5, v_dir="FWD", v_att=0.1, v_rel=0.55, v_mode="KEY", m_grit=0.3)),
    ("Tape Ghost", dict(src="FM PING", src_rate=0.55, fb=0.5, b1_drive=0.25, b2_time=0.6, b2_fb=0.65, b2_mix=0.55, b2_tone=0.4, b3_cut=0.62, b5_mix=0.4, v_att=0.35, v_rel=0.75, m_space=0.3)),
    ("Slow Collapse", dict(src="ATTRACTORS", att=1, src_rate=0.3, fb=0.62, b4_on=1, b4_pitch=-12, b4_fb=0.5, b4_mix=0.6, b5_mix=0.5, b5_decay=0.8, v_speed=0.35, v_dir="REV", v_att=0.5, v_rel=0.9)),
    ("Glass Teeth", dict(src="CLICKS", src_rate=0.7, fb=0.35, b1_drive=0.3, b3_cut=0.8, b3_res=0.6, b4_on=1, b4_pitch=12, b4_mix=0.5, b2_time=0.25, b2_fb=0.55, b2_mix=0.5, v_mode="KEY", v_att=0.0, v_rel=0.35)),
    ("Basement Choir", dict(src="HARMONOGRAPH", src_rate=0.42, fb=0.4, b3_cut=0.35, b3_res=0.25, b5_mix=0.55, b5_size=0.7, b5_decay=0.75, v_att=0.6, v_rel=0.85, m_body=0.5, v_mode="KEY")),
    ("Drift Wreck", dict(src="CLIFFORD", src_rate=0.5, fb=0.55, b2_mix=0.45, b2_time=0.5, b4_on=1, b4_pitch=5, b4_mix=0.35, v_dir="PING", v_speed=0.55, m_chaos=0.45)),
    ("Chrome Dust", dict(src="SUPERFORMULA", src_rate=0.6, fb=0.3, b1_drive=0.35, b1_tone=0.8, b3_cut=0.7, b3_res=0.4, b2_mix=0.3, v_att=0.05, v_rel=0.45, m_grit=0.2)),
    ("Tin Cathedral", dict(src="MOIRE", src_rate=0.48, fb=0.35, b5_mix=0.7, b5_size=0.85, b5_decay=0.85, b5_damp=0.4, b3_cut=0.6, v_att=0.55, v_rel=0.9, m_space=0.4)),
    ("Undertow", dict(src="NOISE", src_rate=0.2, fb=0.6, b3_cut=0.25, b3_res=0.55, b3_env=0.35, b1_drive=0.4, v_speed=0.4, v_dir="REV", v_att=0.3, v_rel=0.8, m_body=0.6)),
    ("Static Bloom", dict(src="NOISE", src_rate=0.8, fb=0.5, b5_mix=0.6, b5_decay=0.7, b2_mix=0.35, b2_time=0.35, b3_cut=0.75, v_att=0.45, v_rel=0.7, v_mode="FREE", v_pitch=0)),
    ("Pendulum", dict(src="SPIROGRAPH", src_rate=0.45, fb=0.45, v_dir="PING", v_speed=0.5, v_att=0.15, v_rel=0.6, b2_time=0.4, b2_fb=0.5, b2_mix=0.4, m_chaos=0.2)),
    ("Hollow Engine", dict(src="ATTRACTORS", att=2, src_rate=0.4, fb=0.5, b1_drive=0.6, b1_tone=0.35, b3_cut=0.3, b3_res=0.5, b3_mix=1.0, v_mode="LOCK", v_att=0.1, v_rel=0.4, m_body=0.7)),
    ("Wet Concrete", dict(src="NOISE", src_rate=0.3, fb=0.4, b1_drive=0.5, b5_mix=0.65, b5_size=0.9, b5_decay=0.8, b5_damp=0.65, v_att=0.2, v_rel=0.7, m_grit=0.35, m_space=0.3)),
    ("Frozen Radio", dict(src="WAV", src_rate=0.5, fb=0.55, b3_cut=0.5, b3_res=0.45, b2_mix=0.45, b2_time=0.55, b2_tone=0.35, o_lofi_on=1, o_lofi=0.75, v_att=0.4, v_rel=0.8, v_speed=0.45)),
    ("Spiral Stairs", dict(src="SPIROGRAPH", src_rate=0.62, fb=0.4, b4_on=1, b4_pitch=7, b4_fb=0.6, b4_mix=0.55, a_on=1, a_order="UP", a_range="2 OCT", a_rate="1/16", v_att=0.0, v_rel=0.4)),
    ("Black Ice", dict(src="CLIFFORD", src_rate=0.65, fb=0.5, b1_drive=0.2, b1_tone=0.85, b3_cut=0.85, b3_res=0.3, b5_mix=0.5, b5_decay=0.7, v_att=0.35, v_rel=0.85, m_space=0.5)),
    ("Bent Signal", dict(src="FM PING", src_rate=0.75, fb=0.6, b1_drive=0.55, b4_on=1, b4_pitch=-5, b4_fine=20, b4_mix=0.45, o_stut_on=1, o_stut="1/16", o_mix=0.5, v_att=0.0, v_rel=0.3, m_chaos=0.5)),
    ("Moth Light", dict(src="HARMONOGRAPH", src_rate=0.58, fb=0.3, b3_cut=0.78, b3_res=0.2, b2_mix=0.35, b2_time=0.3, b5_mix=0.45, v_att=0.5, v_rel=0.85, v_mode="PLAY")),
    ("Scrap Orchestra", dict(src="CLICKS", src_rate=0.55, fb=0.65, b1_drive=0.6, b1_mix=0.7, b2_mix=0.5, b2_fb=0.6, b4_on=1, b4_pitch=3, b4_mix=0.4, v_dir="RAND", v_att=0.1, v_rel=0.6, m_chaos=0.6, m_grit=0.4)),
    ("Low Tide", dict(src="ATTRACTORS", att=5, src_rate=0.25, fb=0.55, b3_cut=0.28, b3_res=0.4, b5_mix=0.5, b5_size=0.8, v_speed=0.3, v_dir="PING", v_att=0.6, v_rel=0.95, m_body=0.8)),
    ("Fault Line", dict(src="SUPERFORMULA", src_rate=0.35, fb=0.58, b1_drive=0.75, b1_tone=0.4, b1_bias=0.3, b3_cut=0.45, b3_res=0.6, o_ball_on=1, o_ball="1/8", o_bounce=0.5, o_mix=0.45, m_grit=0.5)),
    ("Last Transmission", dict(src="WAV", src_rate=0.5, fb=0.62, b2_time=0.7, b2_fb=0.7, b2_mix=0.5, b5_mix=0.55, b5_decay=0.9, o_lofi_on=1, o_lofi=0.6, o_ball_on=1, o_ball="1/4", o_mix=0.4, v_dir="REV", v_att=0.5, v_rel=0.95, m_space=0.6)),
]
assert len(PRESETS) == 22


def build_presets():
    out = []
    for name, vals in PRESETS:
        v = {}
        for k, x in vals.items():
            p = PBY[k]
            if "options" in p:
                x = p["options"][x] if isinstance(x, int) else x
            v[k] = x
        # always set the source and chain order so a preset never inherits a stale state
        v.setdefault("slot1", "DRIVE"); v.setdefault("slot2", "ECHO"); v.setdefault("slot3", "FILTER"); v.setdefault("slot4", "SHIFT"); v.setdefault("slot5", "SMEAR")
        out.append({"name": name, "values": v})
    return out


# ======================================================================================================== params.json
def write_params():
    keep = ("key", "name", "min", "max", "default", "options", "momentary", "display", "dynamic_display", "type", "step_of", "step_delta")
    rows = []
    for p in P:
        r = {k: p[k] for k in keep if k in p}
        if p["key"] in ("wav", "p_sel"):
            r["qlink_ticks"] = 3
        if p["key"] == "wav":
            r["nudge_pct"] = 10          # up to 1023 entries: a Q-Link event or wheel click is one step, not eight to ten
        rows.append(r)
    json.dump({"params": rows}, open(os.path.join(HERE, "params.json"), "w"), indent=1)


# ======================================================================================================== wparams.h
def write_header():
    L = ["/* generated by tools/make_data.py from the parameter list: do not edit */", "#pragma once",
         "enum {"]
    for k in KEYS:
        L.append("    P_%s," % k)
    L.append("    NP")
    L.append("};")
    L.append("enum { " + ", ".join("F_%s" % f for f in FMT) + ", F_OPT, F_TRIG, F_SLOT };")
    L.append("typedef struct { const char *key; float lo, hi, def; int fmt; int nopt; } wp_t;")
    rows = []
    for p in P:
        if "options" in p:
            lo, hi = 0, len(p["options"]) - 1
            d = p["options"].index(p["default"]) if isinstance(p.get("default"), str) else p.get("default", 0)
        else:
            lo, hi, d = p["min"], p["max"], p.get("default", p["min"])
        rows.append('    {"%s", %s, %s, %s, F_%s, %d},' % (p["key"], "%.6g" % float(lo) if "." in "%.6g" % float(lo) or "e" in "%.6g" % float(lo) else "%.1f" % float(lo), "%.6g" % float(hi) if "." in "%.6g" % float(hi) else "%.1f" % float(hi), "%.6g" % float(d) if "." in "%.6g" % float(d) else "%.1f" % float(d), p["fmt"], len(p.get("options", []))))
    L.append("static const wp_t WP[NP] = {")
    L += rows
    L.append("};")
    L.append("static const char *const SRC_NAMES[] = {%s};" % ",".join('"%s"' % s for s in SRC))
    L.append("static const char *const DIV_NAMES[] = {%s};" % ",".join('"%s"' % s for s in DIV))
    # factory presets for the engine's own PRESET page: name + (param index, value) pairs
    pres = build_presets()
    L.append("typedef struct { int p; float v; } fv_t;")
    for i, pr in enumerate(pres):
        items = []
        for k, v in pr["values"].items():
            items.append("{P_%s, %s}" % (k, ("%.6g" % float(num(PBY[k], v))) if "." in ("%.6g" % float(num(PBY[k], v))) else "%.1f" % float(num(PBY[k], v))))
        L.append("static const fv_t FACT_%d[] = {%s};" % (i, ", ".join(items)))
    L.append("static const struct { const char *name; int n; const fv_t *v; } FACT[22] = {")
    for i, pr in enumerate(pres):
        L.append('    {"%s", (int)(sizeof FACT_%d / sizeof FACT_%d[0]), FACT_%d},' % (pr["name"], i, i, i))
    L.append("};")
    open(os.path.join(HERE, "src", "wparams.h"), "w").write("\n".join(L) + "\n")
    json.dump({"presets": pres}, open(os.path.join(HERE, "presets.json"), "w"), indent=1)


# ======================================================================================================== layout.conf
LAY = []
TABS = []


def tab(name):
    LAY.append("")
    LAY.append("[tab %s]" % name)
    TABS.append(name)


def line(s):
    LAY.append(s)


def view(label):
    """The buffer strip lives on every page (the overlay draws it over this box); only a caption is drawn by the skin."""
    line('text cx=640 cy=%d label="%s" size=1.0 color=8a8a8a' % (92 + 8, label))


def knobs5(keys, labels, cy=450, r=58):
    xs = [150, 395, 640, 885, 1130]
    for x, k, lb in zip(xs, keys, labels):
        line('knob cx=%d cy=%d r=%d label="%s" key=%s' % (x, cy, r, lb, k))


def ql(page, keys):
    line('qlinks "%s" = %s' % (page, ",".join(keys)))


def write_layout():
    import sys
    sys.path.insert(0, os.path.join(HERE, "tools"))
    import make_pages
    pages = make_pages.build()
    make_pages.render(pages)
    L = ["# Wreck for MPC OS -- page layout (mpc-vst-plugins layout.conf syntax), generated by tools/make_pages.py from the mock-up.",
         "# Coordinates are Force Shadow pixels: the plugin area is x 0..1280, y 86..714. Each page's art (art/bg_*.png) is the mock-up page",
         "# drawn at 1280 x 628; the buffer strip (waveform + playhead) is drawn by the plugin in the box given by data/layout.txt.",
         "scale_names=1", "label_scale=1.7", "art_css=art/wreck.css",
         "knob_strip=art/knob_strip.png", "knob_frames=128", "seg_img=art/seg_off.png", "seg_img_on=art/seg_on.png",
         "theme_bg=000000", "theme_panel=000000", "theme_line=333333", "theme_ink=ffffff", "theme_ink_dim=ffffff", "theme_ink_faint=9a9a9a",
         "theme_accent=e0b227", "theme_accent_hi=f2e160", "theme_knob_face=141414", "theme_knob_ring=262626", "theme_knob_dot=e8c942",
         "theme_btn_on=e0b227", "theme_btn_text=ffffff", "theme_seg_active=e0b227", "theme_seg_active_tx=000000", "theme_seg_inactive=141414", "theme_lcd=000000"]
    for pg in pages:
        L.append("")
        L.append("[tab %s]" % pg.tab)
        TABS.append(pg.tab)
        L.append("art file=art/bg_%s.png x=0 y=86 w=1280 h=628 fit=stretch" % make_pages.slug(pg.tab))
        L += pg.lines
        L.append('qlinks "%s" = %s' % (pg.tab, ",".join(pg.ql)))
    open(os.path.join(HERE, "layout.conf"), "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    write_params()
    write_header()
    write_layout()
    print("%d parameters, %d tabs" % (len(P), len(TABS)))
