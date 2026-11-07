#!/usr/bin/env python3
"""Wreck page art + layout lines, from the user's mock-up (mock/*.dc.html): same type, same gold, same page furniture
(header, chain strip, buffer box), drawn at the Force's plugin-area size 1280 x 628.

make_data.py imports this: pages() -> [(tab name, art png, [layout lines], qlinks keys)], and render() draws the art with
headless Chromium (Playwright). Coordinates in the HTML are page pixels (y 0 = top of the plugin area); layout lines use
Force Shadow pixels (page y + 86).

Text sizes: baked text is CSS px as in the mock-up (names 35.7, readouts 26, header 34). MPC's own live text is sized by
its font *height*, which is 1.52 x the CSS size for Titillium Web, so live text is given CSS size x 1.52."""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "art")
FONTS = os.environ.get("TITILLIUM", "/home/claude/sunskiefer/mpc-vst-plugins/tools/html_art/fonts")
SHADOW = 86
K = 1.52

GOLD = "linear-gradient(180deg,#f7f185 0%,#f2e160 25%,#e8c942 45%,#e0b227 62%,#dda816 82%,#e7b21e 100%)"
CHIPS = ["SRC", "B1", "B2", "B3", "B4", "B5", "LIM"]
KNOB_R = 58                      # 2r + 10 = 126: the largest knob strip proven on a Force (128 frames of 126 px)
KNOB_TOP = 296                   # page y of the knob's frame; name label above (214..262), value label below
VAL_CSS = 37.4
NAME_CSS = 35.7

# the strip drawn by the plugin's overlay (waveform + playhead): page coordinates
VIEW = (64, 152, 1152, 40)

PAGES = []     # (tab, html body, lines, qlinks)


def Y(y):
    return int(round(y + SHADOW))


def slug(s):
    return "".join(c.lower() if c.isalnum() else "_" for c in s).strip("_")


class Page:
    def __init__(self, tab, title, sub, active=None):
        self.tab, self.title, self.sub, self.active = tab, title, sub, active
        self.h = []          # html (absolute-positioned elements)
        self.lines = []      # layout lines
        self.ql = []
        self.imgs = {}       # name -> (w, h, html) transparent control images to render

    # ---- baked furniture
    def furniture(self):
        chips = ""
        cw = 165
        for i, n in enumerate(CHIPS):
            x = 32 + i * (cw + 10)
            on = self.active == i
            bg = GOLD if on else "#111111"
            lab = "#000000" if on else "#9a9a9a"
            chips += ('<div style="position:absolute;left:%dpx;top:58px;width:%dpx;height:48px;background:%s"></div>'
                      '<div style="position:absolute;left:%dpx;top:58px;height:48px;line-height:48px;font-size:26px;font-weight:700;color:%s">%s</div>'
                      % (x, cw, bg, x + 14, lab, n))
        head = ('<div style="position:absolute;left:32px;top:0;height:56px;display:flex;align-items:center;gap:16px;white-space:nowrap">'
                '<div style="font-size:34px;font-weight:700;letter-spacing:6px;color:#fff">WRECK</div>'
                '<div style="width:10px;height:10px;background:%s;margin-left:4px"></div>'
                '<div style="font-size:34px;font-weight:700;letter-spacing:3px;background:%s;-webkit-background-clip:text;background-clip:text;color:transparent;margin-left:16px">%s</div>'
                '<div style="font-size:26px;font-weight:600;color:#9a9a9a;letter-spacing:2px">%s</div></div>'
                % (GOLD, GOLD, self.title, self.sub))
        buf = ('<div style="position:absolute;left:32px;top:114px;width:1216px;height:84px;background:#0b0b0b"></div>'
               '<div style="position:absolute;left:64px;top:114px;height:36px;line-height:36px;font-size:26px;font-weight:700;letter-spacing:2px;color:#9a9a9a">BUFFER 20 S</div>')
        return head + chips + buf

    def html(self, *parts):
        self.h.extend(parts)

    def text(self, x, y, w, h, s, css=26, weight=700, color="#fff", align="left", ls=0):
        self.h.append('<div style="position:absolute;left:%dpx;top:%dpx;width:%dpx;height:%dpx;line-height:%dpx;font-size:%gpx;font-weight:%d;'
                      'color:%s;text-align:%s;letter-spacing:%gpx;white-space:nowrap">%s</div>' % (x, y, w, h, h, css, weight, color, align, ls, s))

    def card(self, x, y, w, h, color="#141414", cut=28):
        self.h.append('<div style="position:absolute;left:%dpx;top:%dpx;width:%dpx;height:%dpx;background:%s;'
                      'clip-path:polygon(0 0,calc(100%% - %dpx) 0,100%% %dpx,100%% 100%%,0 100%%)"></div>' % (x, y, w, h, color, cut, cut))

    # ---- controls
    def chip_values(self):
        cw = 165
        for i in range(7):
            x = 32 + i * (cw + 10)
            self.lines.append('readout cx=%d cy=%d w=%d h=48 key=chip%d tsize=%d tcolor=%s tweight=700 talign=right tpad=14' % (
                x + cw - 52, Y(82), 104, i, int(round(26 * K)), "000000" if self.active == i else "ffffff"))

    def knob(self, cx, key, name, dy=0, bw=None):
        self.text(cx - 110, KNOB_TOP - 52 + dy, 221, 48, name, css=NAME_CSS, weight=600, align="center")
        self.lines.append('knob cx=%d cy=%d r=%d key=%s ns=0 vs=%d ink_dim=ffffff%s' % (
            cx, Y(KNOB_TOP + dy + KNOB_R + 5), KNOB_R, key, int(round(VAL_CSS * K)), (" bw=%d" % bw) if bw else ""))

    def toggle(self, cx, cy, w, h, label, key):
        nm = "tog_%s_%dx%d" % (slug(label), w, h)
        for st, bg, fg, bd in (("off", "#000", "#fff", "2px solid #333"), ("on", GOLD, "#000", "0")):
            self.imgs["%s_%s" % (nm, st)] = (w, h, '<div style="width:%dpx;height:%dpx;box-sizing:border-box;background:%s;color:%s;border:%s;'
                                             'display:flex;align-items:center;justify-content:center;font-size:26px;font-weight:700;letter-spacing:2px">%s</div>'
                                             % (w, h, bg, fg, bd, label))
        self.lines.append('toggle cx=%d cy=%d w=%d h=%d key=%s ns=0 img=art/%s_off.png img_on=art/%s_on.png' % (cx, Y(cy), w, h, key, nm, nm))

    def button(self, cx, cy, w, h, label, key, gold=False):
        nm = "btn_%s_%dx%d" % (slug(label), w, h)
        for st, bg, fg, bd in (("off", GOLD if gold else "#000", "#000" if gold else "#fff", "0" if gold else "2px solid #333"),
                               ("on", "#fff", "#000", "0")):
            self.imgs["%s_%s" % (nm, st)] = (w, h, '<div style="width:%dpx;height:%dpx;box-sizing:border-box;background:%s;color:%s;border:%s;'
                                             'display:flex;align-items:center;justify-content:center;font-size:26px;font-weight:700;letter-spacing:2px">%s</div>'
                                             % (w, h, bg, fg, bd, label))
        self.lines.append('button cx=%d cy=%d key=%s label="" w=%d h=%d img=art/%s_off.png img_on=art/%s_on.png' % (cx, Y(cy), key, w, h, nm, nm))

    def seg(self, kind, cx, cy, key, options, sw, sh, rows=1):
        self.lines.append('%s cx=%d cy=%d label="" key=%s options="%s" sw=%d sh=%d%s' % (
            kind, cx, Y(cy), key, ",".join(options), sw, sh, (" rows=%d" % rows) if rows > 1 else ""))

    def stepper(self, cx, cy, w, h, key, label="", get=None):
        self.lines.append('stepper cx=%d cy=%d w=%d h=%d label="" key=%s tsize=%d tcolor=ffffff tweight=700%s' % (
            cx, Y(cy), w, h, key, int(round(26 * K)), (" get=%s" % get) if get else ""))

    def qlinks(self, keys):
        self.ql = keys

    def done(self):
        self.chip_values()
        PAGES.append(self)


def knob_x(n, left=32, right=1248):
    wcol = 221
    if n == 1:
        return [(left + right) // 2]
    step = (right - left - wcol) / (n - 1)
    return [int(round(left + wcol / 2 + step * i)) for i in range(n)]


def build():
    PAGES.clear()
    # ------------------------------------------------------------------------------------------ INPUT
    p = Page("INPUT", "INPUT", "SOURCE", active=0)
    src = ["NOISE", "CLICKS", "WAV", "FM PING", "HARMONOGRAPH", "CLIFFORD", "SUPERFORMULA", "SPIROGRAPH", "MOIRE", "ATTRACTORS"]
    p.seg("enum_h", 295, 229, "src", src, 262, 46, rows=5)
    p.text(32, 450, 150, 52, "ATTRACTOR", css=22, weight=600, color="#9a9a9a", ls=2)
    p.stepper(375, 476, 370, 52, "att", "ATTRACTOR")
    p.text(32, 518, 150, 52, "WAV FILE", css=22, weight=600, color="#9a9a9a", ls=2)
    p.stepper(375, 544, 370, 52, "wav", "WAV FILE")
    xs = [695, 915, 1135]
    for x, k, n in zip(xs, ["src_level", "src_rate", "fb"], ["LEVEL", "RATE", "FEEDBACK"]):
        p.knob(x, k, n, bw=200)
    for x, k, n in zip(xs, ["src_prev", "src_gate", "src_tune"], ["PREVIEW", "GATE", "TUNE"]):
        p.toggle(x, 568, 200, 64, n, k)
    p.qlinks(["src_level", "src_rate", "fb", "att", "src_prev", "src_gate", "src_tune", "wav"])
    p.done()
    # ------------------------------------------------------------------------------------------ SAMPLES (WAV files)
    p = Page("SAMPLES", "SAMPLES", "TAP A FILE TO LOAD IT", active=0)
    p.text(32, 202, 600, 30, "YOUR WAV FILES", css=26, weight=600, color="#9a9a9a", ls=2)
    p.lines.append('list x=32 y=%d w=1216 h=256 cols=2 rows=4 order=cols th=56 gap=8 key=wl tsize=%d tcolor=ffffff tweight=700 talign=left tpad=18' % (Y(236), int(round(26 * K))))
    p.stepper(372, 530, 680, 64, "wav")
    p.button(808, 530, 160, 64, "RESCAN", "wav_scan")
    p.button(1071, 530, 350, 64, "PLAY AS SOURCE", "wav_use", gold=True)
    p.lines.append('readout cx=%d cy=%d w=%d h=40 key=wav_info tsize=%d tcolor=ffffff tweight=700 talign=left tpad=0' % (32 + 110, Y(596), 220, int(round(26 * K))))
    p.text(270, 576, 980, 40, "PUT .WAV FILES IN  /sdcard/Synths/L&#39;Cronx - VST - Wreck/data/wav", css=22, weight=600, color="#9a9a9a")
    p.qlinks(["wav", "wav_scan", "wav_use"])
    p.done()
    # ------------------------------------------------------------------------------------------ CHAIN
    p = Page("CHAIN", "CHAIN", "ORDER OF THE BLOCKS")
    blocks = ["DRIVE", "ECHO", "FILTER", "SHIFT", "SMEAR"]
    cwid, gap = 233, 12
    for i in range(5):
        x0 = 32 + i * (cwid + gap)
        p.card(x0, 206, cwid, 322)
        p.text(x0 + 14, 212, cwid - 28, 34, "POSITION %d" % (i + 1), css=26, weight=600, color="#9a9a9a", ls=2)
        p.seg("enum_v", x0 + cwid // 2, 252 + (5 * 54) // 2, "slot%d" % (i + 1), blocks, cwid - 20, 52)
        p.toggle(x0 + cwid // 2, 568, cwid, 64, "%s ON" % blocks[i], "b%d_on" % (i + 1))
    p.qlinks(["slot1", "slot2", "slot3", "slot4", "slot5", "b1_on", "b2_on", "b3_on", "b4_on", "b5_on"])
    p.done()
    # ------------------------------------------------------------------------------------------ BLOCKS
    spec = [("DRIVE", ["b1_drive", "b1_tone", "b1_bias", "b1_mix", "b1_level"], ["DRIVE", "TONE", "BIAS", "MIX", "LEVEL"]),
            ("ECHO", ["b2_time", "b2_fb", "b2_tone", "b2_mix", "b2_spread"], ["TIME", "FEEDBACK", "TONE", "MIX", "SPREAD"]),
            ("FILTER", ["b3_cut", "b3_res", "b3_env", "b3_mix", "b3_level"], ["CUT", "RES", "ENV", "MIX", "LEVEL"]),
            ("SHIFT", ["b4_pitch", "b4_fine", "b4_fb", "b4_mix", "b4_level"], ["PITCH", "FINE", "FDBACK", "MIX", "LEVEL"]),
            ("SMEAR", ["b5_size", "b5_decay", "b5_damp", "b5_mix", "b5_level"], ["SIZE", "DECAY", "DAMP", "MIX", "LEVEL"])]
    for i, (nm, ks, lbs) in enumerate(spec):
        p = Page(nm, nm, "BLOCK %d OF 5" % (i + 1), active=i + 1)
        for x, k, n in zip(knob_x(5), ks, lbs):
            p.knob(x, k, n)
        p.toggle(157, 568, 250, 64, "BLOCK ON", "b%d_on" % (i + 1))
        p.toggle(419, 568, 250, 64, "SOLO", "solo%d" % (i + 1))
        p.qlinks(ks + ["b%d_on" % (i + 1), "solo%d" % (i + 1)])
        p.done()
    # ------------------------------------------------------------------------------------------ VOICES
    p = Page("VOICES", "VOICES", "READ-BACK FROM THE BUFFER")
    for x, k, n in zip(knob_x(5), ["v_speed", "v_pitch", "v_dir", "v_att", "v_rel"], ["SPEED", "PITCH", "DIR", "ATTACK", "RELEASE"]):
        p.knob(x, k, n)
    p.text(32, 536, 240, 64, "PITCH MODE", css=26, weight=600, color="#9a9a9a", ls=2)
    p.seg("enum_h", 508, 568, "v_mode", ["FREE", "KEY", "LOCK", "PLAY"], 130, 64)
    p.text(800, 536, 150, 64, "VOICES", css=26, weight=600, color="#9a9a9a", ls=2)
    p.stepper(1106, 568, 280, 64, "v_count", "VOICES")
    p.qlinks(["v_speed", "v_pitch", "v_dir", "v_att", "v_rel", "v_mode", "v_count"])
    p.done()
    # ------------------------------------------------------------------------------------------ OUTPUT
    p = Page("OUTPUT", "OUTPUT", "OUTPUT BUS")
    for x, k, n in zip(knob_x(5), ["o_ball", "o_bounce", "o_lofi", "o_stut", "o_mix"], ["BALL", "BOUNCE", "LO-FI", "STUTTER", "MIX"]):
        p.knob(x, k, n)
    tw = (1216 - 3 * 12) // 4
    for i, (k, n) in enumerate([("o_ball_on", "BALL ON"), ("o_lofi_on", "LO-FI ON"), ("o_stut_on", "STUTTER ON"), ("o_sync", "SYNC")]):
        p.toggle(32 + tw // 2 + i * (tw + 12), 568, tw, 64, n, k)
    p.qlinks(["o_ball", "o_bounce", "o_lofi", "o_stut", "o_mix", "o_ball_on", "o_lofi_on", "o_stut_on", "o_sync"])
    p.done()
    # ------------------------------------------------------------------------------------------ ARP
    p = Page("ARP", "ARP", "POLYMETRIC LANES")
    p.toggle(107, 262, 150, 64, "ARP ON", "a_on")
    p.toggle(269, 262, 150, 64, "LATCH", "a_latch")
    for x, k, n in [(504, "a_rate", "RATE"), (804, "a_range", "RANGE"), (1104, "a_order", "ORDER")]:
        p.text(x - 144, 206, 288, 22, n, css=22, weight=600, color="#9a9a9a", ls=2)
        p.stepper(x, 262, 288, 64, k, n)
    for j, (k, nm) in enumerate([("n", "NOTE"), ("v", "VEL"), ("g", "GATE"), ("o", "OCT")]):
        y = 316 + j * 62
        p.text(32, y, 100, 48, nm, css=26, weight=700, color="#fff", ls=2)
        p.lines.append('list x=140 y=%d w=900 h=48 key=a_%s cols=16 rows=1 th=48 gap=4 mark=1 tcolor=none' % (Y(y), k))
        p.stepper(1152, y + 24, 192, 48, "a_%slen" % k, "LEN")
    p.qlinks(["a_on", "a_latch", "a_rate", "a_range", "a_order", "a_nlen", "a_vlen", "a_glen", "a_olen"])
    p.done()
    # ------------------------------------------------------------------------------------------ MACROS
    p = Page("MACROS", "MACROS", "FOUR MACRO KNOBS")
    for x, k, n in zip(knob_x(4), ["m_chaos", "m_space", "m_grit", "m_body"], ["CHAOS", "SPACE", "GRIT", "BODY"]):
        p.knob(x, k, n, dy=40)
    p.qlinks(["m_chaos", "m_space", "m_grit", "m_body"])
    p.done()
    # ------------------------------------------------------------------------------------------ MONITOR
    p = Page("MONITOR", "MONITOR", "HEAR ANY STAGE")
    tap = ["SRC", "B1", "B2", "B3", "B4", "B5", "LIM"]
    for i, n in enumerate(tap):
        x0 = 32 + i * 108
        p.card(x0, 206, 100, 232, cut=20)
        p.text(x0 + 12, 212, 80, 34, n, css=26, weight=700, color="#fff")
        p.toggle(x0 + 50, 290, 84, 56, "MON", "mon_t%d" % i)
        p.toggle(x0 + 50, 362, 84, 56, "SOLO", "solo%d" % i)
    p.knob(912, "direct", "DIRECT")
    p.knob(1136, "level", "LEVEL")
    p.qlinks(["mon_t0", "mon_t1", "mon_t2", "mon_t3", "mon_t4", "mon_t5", "mon_t6", "direct", "level"])
    p.done()
    # ------------------------------------------------------------------------------------------ PRESETS
    p = Page("PRESETS", "PRESETS", "DATA WHEEL SCROLLS THE LIST")
    p.text(32, 206, 300, 34, "PRESET", css=26, weight=600, color="#9a9a9a", ls=2)
    p.stepper(412, 280, 760, 88, "p_sel", "PRESET")
    p.text(32, 352, 300, 34, "BANK", css=26, weight=600, color="#9a9a9a", ls=2)
    p.seg("enum_h", 232, 428, "p_bank", ["FACTORY", "USER"], 200, 64)
    p.button(1044, 262, 408, 88, "LOAD", "p_load", gold=True)
    p.button(1044, 360, 408, 88, "SAVE", "p_save")
    p.button(1044, 458, 408, 88, "INIT", "p_init")
    p.qlinks(["p_bank", "p_sel", "p_load", "p_save", "p_init"])
    p.done()
    return PAGES


# ------------------------------------------------------------------------------------------------------ rendering
def shell(inner, w=1280, h=628, transparent=False):
    ff = ""
    for wgt, fn in ((400, "Regular"), (600, "SemiBold"), (700, "Bold")):
        ff += "@font-face{font-family:'Titillium Web';font-weight:%d;src:url('file://%s/TitilliumWeb-%s.ttf')}" % (wgt, FONTS, fn)
    return ("<!doctype html><meta charset=utf-8><style>%s html,body{margin:0;background:%s}body{width:%dpx;height:%dpx;position:relative;"
            "overflow:hidden;font-family:'Titillium Web',sans-serif;color:#fff}</style>%s" % (ff, "transparent" if transparent else "#000", w, h, inner))


def render(pages=None):
    from playwright.sync_api import sync_playwright
    pages = pages or build()
    os.makedirs(ART, exist_ok=True)
    tmp = os.path.join(ART, "_t.html")
    with sync_playwright() as pw:
        br = pw.chromium.launch(args=["--allow-file-access-from-files"])
        pg = br.new_page(viewport={"width": 1280, "height": 628}, device_scale_factor=1)
        for p in pages:
            open(tmp, "w", encoding="utf-8").write(shell(p.furniture() + "".join(p.h)))
            pg.goto("file://" + tmp)
            pg.evaluate("document.fonts.ready")
            pg.wait_for_timeout(120)
            pg.screenshot(path=os.path.join(ART, "bg_%s.png" % slug(p.tab)), clip={"x": 0, "y": 0, "width": 1280, "height": 628})
            for name, (w, h, html) in p.imgs.items():
                open(tmp, "w", encoding="utf-8").write(shell(html, w, h, transparent=True))
                pg.set_viewport_size({"width": w, "height": h})
                pg.goto("file://" + tmp)
                pg.evaluate("document.fonts.ready")
                pg.wait_for_timeout(60)
                pg.screenshot(path=os.path.join(ART, name + ".png"), clip={"x": 0, "y": 0, "width": w, "height": h}, omit_background=True)
                pg.set_viewport_size({"width": 1280, "height": 628})
        br.close()
    os.remove(tmp)
    # seg tiles (option segments): dark off tile, gold on tile
    from PIL import Image, ImageDraw
    import numpy as np
    off = Image.new("RGBA", (64, 64), (0x14, 0x14, 0x14, 255))
    stops = [(0.00, (0xf7, 0xf1, 0x85)), (0.25, (0xf2, 0xe1, 0x60)), (0.45, (0xe8, 0xc9, 0x42)), (0.62, (0xe0, 0xb2, 0x27)),
             (0.82, (0xdd, 0xa8, 0x16)), (1.00, (0xe7, 0xb2, 0x1e))]
    t = np.linspace(0, 1, 64)
    col = np.stack([np.interp(t, [s[0] for s in stops], [s[1][c] for s in stops]) for c in range(3)], axis=1)
    arr = np.zeros((64, 64, 4), np.uint8)
    arr[..., :3] = col[:, None, :]
    arr[..., 3] = 255
    Image.fromarray(arr).save(os.path.join(ART, "seg_on.png"))
    off.save(os.path.join(ART, "seg_off.png"))


if __name__ == "__main__":
    render()
    print("rendered %d pages" % len(PAGES))
