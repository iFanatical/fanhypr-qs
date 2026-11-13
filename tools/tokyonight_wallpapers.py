#!/usr/bin/env python3
"""
tokyonight_wallpapers.py — procedural, seamlessly looping Tokyo Night video wallpapers.

Styles:  set1: city, bokeh, aurora, synthwave, coderain
         set2: fuji, ocean, lava, plexus, topo
         (pass several names, a group, or: all)

Every animated element moves with an integer number of cycles per loop,
so the last frame flows straight back into the first — no visible seam.

Requires: python3, numpy, scipy, pillow, ffmpeg (libx264).
  Arch:   sudo pacman -S python-numpy python-scipy python-pillow ffmpeg noto-fonts-cjk

Examples:
  ./tokyonight_wallpapers.py all                         # 5 videos, 1920x1080, 30 s loops
  ./tokyonight_wallpapers.py city --duration 60 --fps 60
  ./tokyonight_wallpapers.py aurora --seed 7 -o ~/Videos/wallpapers
  ./tokyonight_wallpapers.py synthwave --preview          # write a few PNG stills only
  ./tokyonight_wallpapers.py all --portrait              # 1080x1920 versions for rotated monitors
  ./tokyonight_wallpapers.py bokeh --width 2560 --height 1440   # any size works
  mpvpaper -o "loop no-audio" '*' tokyonight_city.mp4
"""
import argparse
import glob
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import gaussian_filter

TAU = 2 * np.pi

TN = {
    "bg": "#1a1b26", "bg_dark": "#16161e", "storm": "#24283b", "bg_hl": "#292e42",
    "fg": "#c0caf5", "fg_dark": "#a9b1d6", "comment": "#565f89", "term_black": "#414868",
    "blue0": "#3d59a1", "blue": "#7aa2f7", "cyan": "#7dcfff", "blue1": "#2ac3de",
    "teal": "#73daca", "green": "#9ece6a", "purple": "#bb9af7", "magenta2": "#ff007c",
    "red": "#f7768e", "orange": "#ff9e64", "yellow": "#e0af68",
}


def C(c):
    h = TN.get(c, c).lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], np.float32) / 255.0


# ----------------------------------------------------------------- helpers
def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def ramp(t, stops):
    t = np.asarray(t, np.float32)
    pos = np.array([s[0] for s in stops], np.float32)
    cols = np.stack([C(s[1]) for s in stops])
    return np.stack([np.interp(t, pos, cols[:, i]) for i in range(3)], -1).astype(np.float32)


def vgrad(H, W, stops):
    g = ramp(np.linspace(0, 1, H), stops)[:, None, :]
    return np.ascontiguousarray(np.broadcast_to(g, (H, W, 3))).astype(np.float32)


def resize_f(arr, size):
    w, h = size
    out = np.empty((h, w, arr.shape[2]), np.float32)
    for c in range(arr.shape[2]):
        im = Image.fromarray(np.ascontiguousarray(arr[..., c], dtype=np.float32))
        out[..., c] = np.asarray(im.resize((w, h), Image.BILINEAR))
    return out


def glow(img, f=4, sigma=4.0):
    """Cheap bloom: downsample, blur, upsample."""
    h, w, c = img.shape
    hs, ws = max(1, h // f), max(1, w // f)
    s = img[:hs * f, :ws * f].reshape(hs, f, ws, f, c).mean((1, 3))
    s = gaussian_filter(s, (sigma, sigma, 0), mode="nearest")
    return resize_f(s, (w, h))


def ss_mask(W, H, draw_fn, ss=3):
    """Antialiased mask: draw at ss× then downsample. draw_fn(draw, scale)."""
    im = Image.new("L", (W * ss, H * ss), 0)
    draw_fn(ImageDraw.Draw(im), ss)
    return np.asarray(im.resize((W, H), Image.LANCZOS), np.float32) / 255.0


def gauss_kernel(r, sigma):
    y, x = np.mgrid[-r:r + 1, -r:r + 1]
    return np.exp(-(x * x + y * y) / (2 * sigma * sigma)).astype(np.float32)


def add_kernel(img, cx, cy, k, col, amt):
    H, W, _ = img.shape
    r = k.shape[0] // 2
    x0, y0 = int(cx) - r, int(cy) - r
    x1, y1 = x0 + k.shape[1], y0 + k.shape[0]
    cx0, cy0, cx1, cy1 = max(x0, 0), max(y0, 0), min(x1, W), min(y1, H)
    if cx0 >= cx1 or cy0 >= cy1:
        return
    kk = k[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0]
    img[cy0:cy1, cx0:cx1] += kk[..., None] * (col * amt)


def frac(x):
    return x - np.floor(x)


def make_stars(rng, n, W, ymax, keep=None):
    xs = rng.integers(0, W, n)
    ys = rng.integers(0, int(ymax), n)
    if keep is not None:
        ok = keep[ys, xs]
        xs, ys = xs[ok], ys[ok]
    m = len(xs)
    return dict(
        x=xs, y=ys,
        b=(rng.random(m) ** 2.2 * 0.6 + 0.08).astype(np.float32),
        k=rng.integers(1, 5, m), ph=rng.random(m).astype(np.float32),
        col=np.stack([C(c) for c in rng.choice(["fg", "fg", "fg", "cyan", "purple", "blue"], m)]),
        big=rng.random(m) < 0.12,
    )


def draw_stars(img, st, p, depth=0.35):
    tw = 1 - depth + depth * (0.5 + 0.5 * np.sin(TAU * (st["k"] * p + st["ph"])))
    v = st["col"] * (st["b"] * tw)[:, None]
    img[st["y"], st["x"]] += v
    b = st["big"]
    if b.any():
        H, W, _ = img.shape
        xs, ys, vb = st["x"][b], st["y"][b], v[b] * 0.35
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            img[np.clip(ys + dy, 0, H - 1), np.clip(xs + dx, 0, W - 1)] += vb


# ----------------------------------------------------------------- base
class Scene:
    name = "base"

    def __init__(self, W, H, D, fps, seed):
        self.W, self.H, self.D, self.fps = W, H, D, fps
        self.rng = np.random.default_rng(seed)
        self.s = min(W, H) / 1080          # pixel scale (1.0 at 1080p either way round)
        self.ref_w = 1920 * self.s         # reference width for horizontal frequencies
        self.area = W * H / (1920 * 1080)  # for density-based counts
        self.portrait = H > W
        self.setup()

    def cycles(self, period_s):
        """Integer cycle count per loop closest to a desired period (seconds)."""
        return max(1, int(round(self.D / period_s)))

    def setup(self):
        pass

    def frame(self, p):
        raise NotImplementedError


# ----------------------------------------------------------------- 1. neon city
class City(Scene):
    name = "city"

    def setup(self):
        W, H, rng = self.W, self.H, self.rng
        s = self.s
        img = vgrad(H, W, [(0, "#0c0c13"), (0.4, "#14141d"), (0.62, "#1c1b2e"),
                           (0.8, "#2a2348"), (1, "#3a2c5a")])

        # moon + halo
        mx, my, mr = 0.78 * W, 0.19 * H, 45 * s
        moon = ss_mask(W, H, lambda d, k: d.ellipse(
            [(mx - mr) * k, (my - mr) * k, (mx + mr) * k, (my + mr) * k], fill=255))
        halo = gaussian_filter(moon, 65 * s)
        img += halo[..., None] * C("fg") * 1.6 * 0.35
        mcol = ramp(np.linspace(0, 1, H), [(0, "#d6dcf7"), (1, "#b4bde6")])[:, None, :]
        img = img * (1 - moon[..., None]) + mcol * moon[..., None] * 0.92

        layers = [  # color, hmin, hmax, wmin, wmax, window scale
            ("#212338", 0.30, 0.55, 35, 110, 0.6),
            ("#191a29", 0.20, 0.44, 50, 150, 1.0),
            ("#101017", 0.07, 0.30, 90, 230, 1.45),
        ]
        idmap = np.zeros((H, W), np.int32)
        wins = []  # (col, bright, flick, k, ph, duty)
        antennas = []
        occluder = np.zeros((H, W), np.float32)
        for li, (col, hmin, hmax, wmin, wmax, wsc) in enumerate(layers):
            mask_im = Image.new("L", (W, H), 0)
            d = ImageDraw.Draw(mask_im)
            bld = []
            x = -int(rng.integers(0, 60))
            while x < W:
                bw = int(rng.integers(wmin, wmax) * s)
                bh = rng.uniform(hmin, hmax) * H
                top = int(H - bh)
                d.rectangle([x, top, x + bw, H], fill=255)
                if rng.random() < 0.3:
                    tw = int(bw * rng.uniform(0.4, 0.7))
                    th = int(bh * rng.uniform(0.05, 0.12))
                    tx = x + (bw - tw) // 2
                    d.rectangle([tx, top - th, tx + tw, top], fill=255)
                    top_a = top - th
                    ax = tx + tw // 2
                else:
                    top_a = top
                    ax = x + int(rng.integers(bw // 4, 3 * bw // 4 + 1))
                if li > 0 and rng.random() < 0.28:
                    al = int(rng.integers(20, 70) * s)
                    d.rectangle([ax, top_a - al, ax + max(1, int(2 * s)) - 1, top_a], fill=255)
                    antennas.append((ax, top_a - al, li))
                bld.append((x, top, bw))
                x += bw + int(rng.integers(-12, 14) * s)
            m = np.asarray(mask_im, np.float32) / 255.0
            img = img * (1 - m[..., None]) + C(col) * m[..., None]
            if li > 0:
                idmap[m > 0] = 0
            occluder = np.maximum(occluder, m)
            # windows
            ww, wh = max(2, round(4 * wsc * s)), max(3, round(6 * wsc * s))
            gx, gy = max(2, round(5 * wsc * s)), max(3, round(7 * wsc * s))
            for (bx, btop, bw) in bld:
                ncol = (bw - 2 * gx) // (ww + gx)
                if ncol < 1:
                    continue
                mxg = bx + (bw - (ncol * (ww + gx) - gx)) // 2
                plit = rng.uniform(0.08, 0.5)
                palette = rng.choice(["yellow", "yellow", "fg", "cyan", "purple", "orange"])
                y = btop + gy + 2
                while y + wh < H - 4:
                    floor_p = plit * rng.uniform(0.3, 1.7)
                    for c in range(ncol):
                        wx = mxg + c * (ww + gx)
                        if wx < 0 or wx + ww >= W:
                            continue
                        r = rng.random()
                        lit = r < floor_p
                        flick = (lit and rng.random() < 0.07) or (not lit and rng.random() < 0.015)
                        if not (lit or flick):
                            continue
                        cname = palette if rng.random() < 0.75 else rng.choice(
                            ["yellow", "fg", "cyan", "purple", "red"])
                        bright = rng.uniform(0.45, 1.0) * (0.55 if li == 0 else 0.75 if li == 1 else 1.0)
                        wins.append((C(cname), bright, flick, int(rng.integers(1, 5)),
                                     rng.random(), rng.uniform(0.3, 0.7)))
                        idmap[y:y + wh, wx:wx + ww] = len(wins)
                    y += wh + gy

        n = len(wins)
        self.wcol = np.vstack([np.zeros(3, np.float32)] + [w[0] for w in wins]).astype(np.float32)
        self.wbright = np.array([0] + [w[1] for w in wins], np.float32)
        self.wflick = np.array([False] + [w[2] for w in wins])
        self.wk = np.array([0] + [w[3] for w in wins], np.float32)
        self.wph = np.array([0] + [w[4] for w in wins], np.float32)
        self.wduty = np.array([0] + [w[5] for w in wins], np.float32)
        rows = np.where(idmap.any(1))[0]
        self.y0 = int(rows.min()) if len(rows) else H
        self.idmap = idmap[self.y0:]

        steady = self.wcol * (self.wbright * ~self.wflick)[:, None]
        steady_rgb = np.zeros((H, W, 3), np.float32)
        steady_rgb[self.y0:] = steady[self.idmap]
        img += glow(steady_rgb, 4, 2.5) * 0.9 + glow(steady_rgb, 8, 6) * 0.6
        self.base = img
        self.stars = make_stars(rng, int(200 * self.area), W, 0.6 * H, keep=occluder[: int(0.6 * H)] < 0.01)
        self.stars["b"] *= 0.7

        self.antennas = [(x, y, rng.random(), self.cycles(rng.uniform(1.4, 2.6))) for x, y, _ in antennas]
        self.blink_k = gauss_kernel(int(14 * s), 4 * s)

        # rain
        self.rain = []
        for count, lmin, lmax, wdt, amin, amax, speed in (
                (int(380 * self.area), 14, 30, 1, 35, 70, 950),
                (int(110 * self.area), 40, 75, 2, 45, 90, 1500)):
            for _ in range(count):
                L = rng.uniform(lmin, lmax) * s
                span = H + L + 40
                self.rain.append((rng.uniform(0, span), rng.uniform(0, W + 200), L, span,
                                  max(1, round(speed * s * self.D / span) + int(rng.integers(-2, 3))),
                                  wdt, int(rng.uniform(amin, amax))))
        self.tan = np.tan(np.radians(9))
        self.rain_col = C("#a9b8f0")

    def frame(self, p):
        img = self.base.copy()
        draw_stars(img, self.stars, p)
        on = ~self.wflick | (frac(self.wk * p + self.wph) < self.wduty)
        per = self.wcol * (self.wbright * on)[:, None]
        per[0] = 0
        img[self.y0:] += per[self.idmap]
        for x, y, ph, k in self.antennas:
            if frac(k * p + ph) < 0.14:
                add_kernel(img, x, y, self.blink_k, C("red"), 0.9)
        # rain
        rl = Image.new("L", (self.W, self.H), 0)
        d = ImageDraw.Draw(rl)
        sn = np.sin(np.radians(9))
        cs = np.cos(np.radians(9))
        for y0, x0, L, span, nw, wdt, a in self.rain:
            yy = (y0 + nw * p * span) % span - L
            xx = (x0 + yy * self.tan) % (self.W + 200) - 100
            d.line([(xx, yy), (xx + L * sn, yy + L * cs)], fill=a, width=wdt)
        a = np.asarray(rl, np.float32)[..., None] / 255.0
        img += (self.rain_col - img) * a
        return img


# ----------------------------------------------------------------- 2. bokeh
class Bokeh(Scene):
    name = "bokeh"

    def setup(self):
        W, H, rng = self.W, self.H, self.rng
        s = self.s
        img = vgrad(H, W, [(0, "#14141c"), (0.5, "#1a1b26"), (1, "#1d1b2e")])
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot((xx - 0.5 * W) / (0.6 * W), (yy - 0.55 * H) / (0.6 * H))
        img *= (1 - 0.38 * smoothstep(0.3, 1.4, r))[..., None]
        self.base = img
        cols = ["blue", "blue", "purple", "purple", "cyan", "blue1", "red", "magenta2", "teal"]
        self.parts = []
        for _ in range(int(80 * self.area)):
            R = float(np.exp(rng.uniform(np.log(14), np.log(130)))) * s
            depth = (R / (130 * s))
            self.parts.append(dict(
                kind="rise", R=R, col=C(rng.choice(cols)),
                amp=rng.uniform(0.07, 0.24) * (1.15 - 0.6 * depth),
                x0=rng.uniform(0, W), y0=rng.uniform(0, 1),
                n=1 if R > 50 * s else int(rng.integers(1, 3)),
                sway=rng.uniform(15, 80) * s, sk=int(rng.integers(1, 3)) * rng.choice([-1, 1]),
                sph=rng.random(), pk=int(rng.integers(1, 4)), pph=rng.random(),
                edge=max(1.5, R * (0.06 + 0.25 * depth))))
        for _ in range(5):
            self.parts.append(dict(
                kind="wash", R=rng.uniform(380, 620) * s, col=C(rng.choice(["blue", "purple", "blue0", "magenta2"])),
                amp=rng.uniform(0.04, 0.07), x0=rng.uniform(0.1, 0.9) * W, y0=rng.uniform(0.15, 0.85) * H,
                ax=rng.uniform(80, 220) * s, ay=rng.uniform(50, 140) * s, k=int(rng.choice([-1, 1])),
                ph=rng.random(), pk=1, pph=rng.random()))
        # draw big washes first, small bright ones last
        self.parts.sort(key=lambda q: -q["R"])

    def frame(self, p):
        img = self.base.copy()
        W, H = self.W, self.H
        for q in self.parts:
            R = q["R"]
            pulse = 0.78 + 0.22 * np.sin(TAU * (q["pk"] * p + q["pph"]))
            if q["kind"] == "wash":
                cx = q["x0"] + q["ax"] * np.cos(TAU * (q["k"] * p + q["ph"]))
                cy = q["y0"] + q["ay"] * np.sin(TAU * (q["k"] * p + q["ph"]))
            else:
                span = H + 2 * R
                cy = (q["y0"] * span - q["n"] * p * span) % span - R
                cx = q["x0"] + q["sway"] * np.sin(TAU * (q["sk"] * p + q["sph"]))
                cx = cx % (W + 2 * R) - R
            x0, x1 = int(max(cx - R - 2, 0)), int(min(cx + R + 3, W))
            y0, y1 = int(max(cy - R - 2, 0)), int(min(cy + R + 3, H))
            if x0 >= x1 or y0 >= y1:
                continue
            ys = np.arange(y0, y1, dtype=np.float32)[:, None] - cy
            xs = np.arange(x0, x1, dtype=np.float32)[None, :] - cx
            d = np.sqrt(xs * xs + ys * ys)
            if q["kind"] == "wash":
                a = np.exp(-2.2 * (d / R) ** 2)
            else:
                a = np.clip((R - d) / q["edge"], 0, 1)
                a = a * (0.75 + 0.45 * smoothstep(0.55 * R, R, d)) * (1 - 0.15 * (d / R))
            img[y0:y1, x0:x1] += a[..., None] * (q["col"] * q["amp"] * pulse)
        return img


# ----------------------------------------------------------------- 3. aurora
class Aurora(Scene):
    name = "aurora"

    def setup(self):
        W, H, rng = self.W, self.H, self.rng
        s = self.s
        self.hy = hy = int((0.82 if self.portrait else 0.80) * H)
        self.base = vgrad(H, W, [(0, "#0a0a11"), (0.45, "#11121c"), (0.8, "#1b1d31"), (1, "#1b1d31")])
        self.stars = make_stars(rng, int(650 * self.area), W, hy - 80 * s)
        self.f = 2
        self.wl, self.hl = W // self.f, hy // self.f
        self.u = (np.arange(self.wl, dtype=np.float32) * self.f / self.ref_w)[None, :]
        self.v = (np.arange(self.hl, dtype=np.float32) * self.f / H)[:, None]

        def curtain(base, amps, sf, height, strength, stops):
            return dict(base=base, amps=amps, sf=sf, tk=[int(rng.choice([-2, -1, 1, 2])) for _ in amps],
                        ph=rng.random(len(amps)) * TAU, height=height, strength=strength, stops=stops,
                        rf=[int(rng.integers(22, 34)), int(rng.integers(45, 70))],
                        rk=[int(rng.choice([-4, -3, 3, 4])), int(rng.choice([-6, -5, 5, 6]))],
                        rph=rng.random(2) * TAU, ek=[1, int(rng.choice([-2, 2]))], eph=rng.random(2) * TAU)

        self.curtains = [
            curtain((0.44 if self.portrait else 0.34) * hy / H, [0.05, 0.028, 0.012], [1.1, 2.7, 5.3], 0.15, 0.55,
                    [(0, "blue"), (0.6, "purple"), (1.5, "#9d7cd8"), (3, "#3d2f66")]),
            curtain((0.68 if self.portrait else 0.525) * hy / H, [0.075, 0.035, 0.014], [0.8, 1.9, 4.4], 0.23, 1.0,
                    [(0, "teal"), (0.3, "#7dcfff"), (0.8, "blue"), (1.5, "purple"), (3, "#3a2e63")]),
        ]
        # silhouettes
        xs = np.arange(-10, W + 20, 6)
        def ridge(base_y, amp, seeds):
            y = np.full(len(xs), base_y, np.float32)
            for a, f in seeds:
                y -= a * np.sin(xs / self.ref_w * TAU * f + rng.uniform(0, TAU))
            y -= rng.uniform(-1, 1, len(xs)) * 3 * s
            return y
        far = ridge(hy - 103 * s, 1, [(49 * s, 1.3), (22 * s, 3.1), (9 * s, 9.0)])
        near = ridge(hy - 49 * s, 1, [(22 * s, 2.1), (13 * s, 5.0), (5 * s, 13.0)])
        def poly(ys):
            return lambda d, k: d.polygon([(x * k, y * k) for x, y in zip(xs, ys)] +
                                          [((W + 20) * k, (hy + 2) * k), (-10 * k, (hy + 2) * k)], fill=255)
        self.m_far = ss_mask(W, H, poly(far))
        self.m_near = ss_mask(W, H, poly(near))

        def trees(d, k):
            x = -10.0
            while x < W + 10:
                cl = 0.5 + 0.5 * np.sin(x / self.ref_w * TAU * 3.3 + 1.0)
                h = rng.uniform(14, 34 + 46 * cl) * s
                w = h * rng.uniform(0.28, 0.38)
                for t in range(3):
                    apex = hy - h + t * h * 0.22
                    by = hy - h * 0.5 + t * h * 0.25
                    tw = w * (0.55 + 0.25 * t)
                    d.polygon([(x * k, apex * k), ((x - tw / 2) * k, by * k), ((x + tw / 2) * k, by * k)], fill=255)
                d.rectangle([(x - 0.8 * s) * k, (hy - h * 0.2) * k, (x + 0.8 * s) * k, (hy + 2) * k], fill=255)
                x += rng.uniform(4, 13) * s
        self.m_trees = ss_mask(W, H, trees)
        self.lake = vgrad(H - hy, W, [(0, "#121320"), (1, "#0b0b12")])
        self.ripk = self.cycles(6)

    def aurora(self, p):
        u, v = self.u, self.v
        out = np.zeros((self.hl, self.wl, 3), np.float32)
        for c in self.curtains:
            yc = c["base"] + sum(a * np.sin(TAU * (f * u + k * p) + ph)
                                 for a, f, k, ph in zip(c["amps"], c["sf"], c["tk"], c["ph"]))
            ray = (0.5 + 0.25 * np.sin(TAU * (c["rf"][0] * u + c["rk"][0] * p) + c["rph"][0])
                   + 0.25 * np.sin(TAU * (c["rf"][1] * u + c["rk"][1] * p) + c["rph"][1])) ** 1.6
            env = np.clip(0.5 + 0.5 * np.sin(TAU * (1.1 * u + c["ek"][0] * p) + c["eph"][0]), 0, 1)
            env = 0.15 + 0.85 * env * (0.65 + 0.35 * np.sin(TAU * (2.3 * u + c["ek"][1] * p) + c["eph"][1]))
            d = yc - v
            prof = np.where(d >= 0, np.exp(-d / c["height"]), np.exp(-(d / 0.035) ** 2))
            I = prof * env * (0.45 + 0.55 * ray) * c["strength"]
            col = ramp(np.clip(d / c["height"], 0, 3), c["stops"])
            out += I[..., None] * col
        return out * 0.95

    def frame(self, p):
        W, H, hy = self.W, self.H, self.hy
        img = self.base.copy()
        draw_stars(img, self.stars, p)
        au = resize_f(self.aurora(p), (W, hy))
        img[:hy] += au
        for m, col in ((self.m_far, "#151622"), (self.m_near, "#0f1018"), (self.m_trees, "#08080d")):
            img = img * (1 - m[..., None]) + C(col) * m[..., None]
        # lake reflection with gentle ripple
        rows = np.arange(hy, H)
        dist = rows - hy
        jit = np.round((0.6 + dist * 0.025) * np.sin(dist * 0.45 + TAU * self.ripk * p)).astype(int)
        src = np.clip(2 * hy - 1 - rows + jit, 0, hy - 1)
        fade = (0.55 - 0.3 * (dist / max(1, H - hy)))[:, None, None]
        img[hy:] = self.lake + img[src] * fade * np.array([0.75, 0.85, 1.0], np.float32)
        return img


# ----------------------------------------------------------------- 4. synthwave
class Synthwave(Scene):
    name = "synthwave"

    def setup(self):
        W, H, rng = self.W, self.H, self.rng
        s = self.s
        self.hy = hy = int((0.60 if self.portrait else 0.62) * H)
        img = vgrad(H, W, [(0, "#0b0b13"), (0.3, "#14141d"), (0.5, "#221d3a"), (0.62, "#3b2858"), (1, "#3b2858")])
        yy = np.arange(H, dtype=np.float32)[:, None, None]
        img += np.exp(-((yy - hy) / (76 * s)) ** 2) * C("red") * 0.25
        self.sky_stars = make_stars(rng, int(260 * self.area), W, hy - 260 * s)

        # sun
        self.cx, self.cy, self.R = W / 2, hy - 108 * s, 254 * s
        cx, cy, R = self.cx, self.cy, self.R
        self.sun_mask = ss_mask(W, H, lambda d, k: d.ellipse([(cx - R) * k, (cy - R) * k, (cx + R) * k, (cy + R) * k], fill=255))
        self.sy0, self.sy1 = int(cy - R - 2), min(hy + 2, int(cy + R + 2))
        sv = (np.arange(H, dtype=np.float32) - cy) / R
        self.sun_col = ramp(sv, [(-1, "yellow"), (-0.25, "orange"), (0.35, "red"), (1, "purple")])[:, None, :]
        sg = gaussian_filter(self.sun_mask, 76 * s)
        img += sg[..., None] * ramp(sv, [(-1, "orange"), (1, "red")])[:, None, :] * 0.9
        self.sv = sv
        self.stripe_k = self.cycles(3.5)

        # mountains
        xs = np.arange(-10, W + 20, 8)
        def ridge(base, amps):
            y = np.full(len(xs), base, np.float32)
            for a, f in amps:
                y -= a * np.abs(np.sin(xs / self.ref_w * TAU * f + rng.uniform(0, TAU))) ** 1.5
            return y
        self.ridges = []
        for base, amps, col in ((hy, [(119 * s, 1.6), (54 * s, 4.2), (16 * s, 11)], "#1a1430"),
                                (hy + 1, [(65 * s, 2.4), (32 * s, 6.1), (11 * s, 15)], "#0f0c19")):
            ys = ridge(base, amps)
            ys = np.minimum(ys, hy + 1)
            m = ss_mask(W, H, lambda d, k, ys=ys: d.polygon([(x * k, y * k) for x, y in zip(xs, ys)] +
                                                            [((W + 20) * k, (hy + 2) * k), (-10 * k, (hy + 2) * k)], fill=255))
            e = ss_mask(W, H, lambda d, k, ys=ys: d.line([(x * k, y * k) for x, y in zip(xs, ys)], fill=255, width=int(2 * k)))
            self.ridges.append((m, e, col))
        self.base = img

        # floor
        fh = H - hy
        self.K = fh * fh / (100 * s)
        self.F = 140 * s * (self.K / fh)
        dy = np.arange(fh, dtype=np.float32)[:, None] + 0.5
        x = np.arange(W, dtype=np.float32)[None, :] - W / 2
        self.z = self.K / dy
        self.dudy = self.K / dy ** 2
        self.xc = x
        floor = vgrad(fh, W, [(0, "#1d1533"), (0.3, "#130f22"), (1, "#0c0a14")])
        rx = np.exp(-(x / (211 * s)) ** 2)
        floor += (rx * np.exp(-dy / (80 * s)))[..., None] * C("orange") * 0.22
        floor += np.exp(-dy / (26 * s))[..., None] * C("red") * 0.35
        self.floor = floor
        self.fog = (0.3 + 0.7 * smoothstep(0, 0.9, dy / fh))
        self.fadeH = smoothstep(12, 36, 1 / self.dudy)
        self.grid_col = ramp(dy[:, 0] / fh, [(0, "red"), (0.25, "#d38fd6"), (0.7, "purple"), (1, "purple")])[:, None, :]
        self.scroll_n = max(1, int(round(self.D * 0.9)))

    def frame(self, p):
        W, H, hy = self.W, self.H, self.hy
        img = self.base.copy()
        draw_stars(img, self.sky_stars, p, 0.5)
        # sun with moving stripes (1-D per row, supersampled)
        y0, y1 = self.sy0, self.sy1
        rows = np.arange(y0, y1, dtype=np.float32)
        sub = (rows[:, None] + (np.arange(4, dtype=np.float32) + 0.5)[None, :] / 4 - self.cy) / self.R
        t = (sub + 0.5) / 1.5
        q = frac(t * 6 - p * self.stripe_k)
        gap = (0.1 + 0.6 * np.clip(t, 0, 1)) * (t > 0)
        cover = (q >= gap).mean(1)[:, None]
        a = (self.sun_mask[y0:y1] * cover)[..., None]
        img[y0:y1] = img[y0:y1] * (1 - a) + self.sun_col[y0:y1] * a
        for m, e, col in self.ridges:
            img = img * (1 - m[..., None]) + C(col) * m[..., None]
            img += e[..., None] * C("purple") * 0.55
        img += glow(np.ascontiguousarray(self.ridges[0][1][..., None] * C("purple")), 4, 2.0) * 0.8
        # floor grid
        sc = p * self.scroll_n
        u = self.z + sc
        dH = np.abs(u - np.round(u)) / self.dudy
        lineH = np.clip(1.4 - dH, 0, 1) * self.fadeH
        v = self.xc * self.z / self.F
        dvdx = self.z / self.F
        dV = np.abs(v - np.round(v)) / dvdx
        lineV = np.clip(1.4 - dV, 0, 1) * smoothstep(12, 36, 1 / dvdx)
        g = np.maximum(lineH, lineV) * self.fog
        grid = g[..., None] * self.grid_col
        fl = self.floor + grid * 0.95 + glow(grid, 4, 2.5) * 1.6
        img[hy:] = fl
        img[hy - 1:hy + 1] += C("red") * 0.7
        return img


# ----------------------------------------------------------------- 5. code rain
class CodeRain(Scene):
    name = "coderain"

    def setup(self):
        W, H, rng = self.W, self.H, self.rng
        s = self.s
        self.cw, self.ch = max(8, round(15 * s)), max(12, round(24 * s))
        self.cols, self.rows = -(-W // self.cw), -(-H // self.ch)
        fsize = round(19 * s)
        chars = [chr(c) for c in range(0xFF66, 0xFF9E)] + list("0123456789") + list("ZTKNXYV:=+*<>|")
        font = None
        cands = glob.glob("/usr/share/fonts/**/NotoSansCJK*Regular*.tt*", recursive=True) + \
                glob.glob("/usr/share/fonts/**/NotoSansMonoCJK*.tt*", recursive=True)
        for path in cands:
            try:
                font = ImageFont.truetype(path, fsize, index=0)
                break
            except Exception:
                pass
        if font is None:
            mono = glob.glob("/usr/share/fonts/**/DejaVuSansMono.ttf", recursive=True)
            font = ImageFont.truetype(mono[0], fsize) if mono else ImageFont.load_default()
            chars = list("0123456789ABCDEFZTKNXYV:=+*<>|$#@%&")
        atlas = []
        for chs in chars:
            im = Image.new("L", (self.cw * 2, self.ch * 2), 0)
            d = ImageDraw.Draw(im)
            bb = d.textbbox((0, 0), chs, font=font)
            if bb[2] - bb[0] <= 0:
                continue
            gw, gh = bb[2] - bb[0], bb[3] - bb[1]
            d.text(((self.cw * 2 - gw) / 2 - bb[0], (self.ch * 2 - gh) / 2 - bb[1]), chs, font=font, fill=255)
            g = np.asarray(im, np.float32) / 255.0
            g = g[self.ch // 2: self.ch // 2 + self.ch, self.cw // 2: self.cw // 2 + self.cw]
            atlas.append(g)
            atlas.append(g[:, ::-1].copy())
        self.atlas = np.stack(atlas).astype(np.float32)
        G = len(self.atlas)
        R, Cn = self.rows, self.cols
        self.K = 120
        self.table = rng.integers(0, G, (self.K, R, Cn))
        self.coff = rng.random((R, Cn)).astype(np.float32)
        self.rowi = np.arange(R, dtype=np.float32)[:, None]

        palettes = {"blue": ("blue", "blue0"), "cyan": ("cyan", "blue1"), "purple": ("purple", "#5a4a8a")}
        self.drops = []
        for c in range(Cn):
            depth = rng.uniform(0.35, 1.0)
            for _ in range(int(rng.integers(1, 3))):
                trail = rng.uniform(8, 26)
                span = R + trail + rng.uniform(5, 45)
                speed = rng.uniform(6, 16) * (0.6 + 0.4 * depth)
                n = max(1, int(round(speed * self.D / span)))
                pal = palettes[rng.choice(["blue", "blue", "blue", "cyan", "purple"])]
                self.drops.append((c, rng.uniform(0, span), span, n, trail, depth, C(pal[0]), C(pal[1])))
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot((xx - W / 2) / (0.62 * W), (yy - H / 2) / (0.62 * H))
        self.base = vgrad(H, W, [(0, "#16161e"), (0.5, "#1a1b26"), (1, "#16161e")]) * \
            (1 - 0.35 * smoothstep(0.4, 1.4, r))[..., None]
        self.dim = C("term_black") * 0.22

    def frame(self, p):
        R, Cn, ch, cw = self.rows, self.cols, self.ch, self.cw
        cellcol = np.zeros((R, Cn, 3), np.float32) + self.dim
        bright = np.zeros((R, Cn), np.float32)
        head = np.zeros((R, Cn), bool)
        for c, y0, span, n, trail, depth, col, col2 in self.drops:
            hpos = (y0 + n * p * span) % span
            d = hpos - self.rowi[:, 0]
            m = (d >= 0) & (d < trail)
            if not m.any():
                continue
            t = d[m] / trail
            b = ((1 - t) ** 1.6) * depth
            cur = bright[m, c]
            upd = b > cur
            idx = np.where(m)[0][upd]
            tt = t[upd][:, None]
            colmix = np.where(tt < 0.15, C("fg") * (1 - tt / 0.15) + col * (tt / 0.15),
                              col * (1 - (tt - 0.15) / 0.85) + col2 * ((tt - 0.15) / 0.85))
            bright[idx, c] = b[upd]
            cellcol[idx, c] = colmix
            hd = idx[d[idx] < 1.0]
            head[hd, c] = True
        bright = np.maximum(bright, 0.0)
        step_n = (np.floor(p * 30 + self.coff * 30).astype(int) * 4) % self.K
        step_h = int(np.floor(p * self.K)) % self.K
        rr, cc = np.indices((R, Cn))
        gi = np.where(head, self.table[step_h, rr, cc], self.table[step_n, rr, cc])
        A = self.atlas[gi]  # R,Cn,ch,cw
        A = A.transpose(0, 2, 1, 3).reshape(R * ch, Cn * cw)[: self.H, : self.W]
        lum = cellcol * np.maximum(bright, 0.12)[..., None]
        lum[head] = C("fg") * 1.05
        lumimg = np.repeat(np.repeat(lum, ch, 0), cw, 1)[: self.H, : self.W]
        layer = A[..., None] * lumimg
        return self.base + layer * 1.05 + glow(layer, 4, 3.0) * 1.0


# ----------------------------------------------------------------- shared bits for set 2
def sin_cos_2d(ax, by):
    """sin/cos of (ax[x] + by[y]) via outer products — far cheaper than a full 2-D sin."""
    sx, cx = np.sin(ax), np.cos(ax)
    sy, cy = np.sin(by), np.cos(by)
    s = sy[:, None] * cx[None, :] + cy[:, None] * sx[None, :]
    c = cy[:, None] * cx[None, :] - sy[:, None] * sx[None, :]
    return s, c


def cloud_strip(rng, W, h, puffs, s, sigma):
    im = Image.new("L", (W, h), 0)
    d = ImageDraw.Draw(im)
    for _ in range(puffs):
        x = rng.uniform(0, W)
        y = rng.normal(h * 0.55, h * 0.1)
        rx = rng.uniform(40, 170) * s
        ry = rx * rng.uniform(0.18, 0.35)
        v = int(rng.uniform(120, 255))
        for off in (-W, 0, W):
            d.ellipse([x + off - rx, y - ry, x + off + rx, y + ry], fill=v)
    a = np.asarray(im, np.float32) / 255.0
    a = gaussian_filter(a, sigma, mode=("constant", "wrap"))
    taper = np.clip(np.sin(np.linspace(0, np.pi, h, dtype=np.float32)), 0, 1)[:, None] ** 0.7
    return np.clip(a * 1.4, 0, 1) * taper


def roll_sub(a, sh):
    i = int(np.floor(sh))
    f = np.float32(sh - i)
    return np.roll(a, i, 1) * (1 - f) + np.roll(a, i + 1, 1) * f


def moon_layer(img, W, H, mx, my, mr, s, glow_amt=1.0):
    moon = ss_mask(W, H, lambda d, k: d.ellipse(
        [(mx - mr) * k, (my - mr) * k, (mx + mr) * k, (my + mr) * k], fill=255))
    img += gaussian_filter(moon, 70 * s)[..., None] * C("fg") * 0.55 * glow_amt
    img += gaussian_filter(moon, 220 * s)[..., None] * C("blue") * 0.35 * glow_amt
    mcol = ramp(np.linspace(0, 1, H), [(0, "#dfe4fb"), (1, "#b4bde6")])[:, None, :]
    return img * (1 - moon[..., None]) + mcol * moon[..., None] * 0.93


def reflect(img, hy, H, p, k, lake, strength=0.5):
    rows = np.arange(hy, H)
    dist = rows - hy
    jit = np.round((0.6 + dist * 0.025) * np.sin(dist * 0.45 + TAU * k * p)).astype(int)
    src = np.clip(2 * hy - 1 - rows + jit, 0, hy - 1)
    fade = (strength - 0.3 * (dist / max(1, H - hy)))[:, None, None]
    img[hy:] = lake + img[src] * fade * np.array([0.75, 0.85, 1.0], np.float32)


# ----------------------------------------------------------------- 6. fuji + sakura
class Fuji(Scene):
    name = "fuji"

    def setup(self):
        W, H, rng, s = self.W, self.H, self.rng, self.s
        self.hy = hy = int((0.74 if self.portrait else 0.72) * H)
        h = hy / H
        img = vgrad(H, W, [(0, "#0b0b12"), (h * 0.55, "#161726"), (h, "#2b2747"), (1, "#2b2747")])
        img = moon_layer(img, W, H, 0.2 * W, 0.17 * H, 50 * s, s)
        self.stars = make_stars(rng, int(380 * self.area), W, hy - 220 * s)
        self.sky = img

        # Fuji profile
        cx = 0.5 * W if self.portrait else 0.58 * W
        hw = 0.62 * W if self.portrait else 760 * s
        ph = 430 * s
        xs = np.arange(W, dtype=np.float32)
        u = np.abs(xs - cx) / hw
        u2 = np.clip((u - 0.06) / 0.94, 0, 1)
        prof = hy - ph * (1 - u2) ** 1.9 + 2 * s * np.sin(xs / (37 * s))
        Y = np.arange(H, dtype=np.float32)[:, None]
        m = np.clip(Y - prof[None, :] + 0.5, 0, 1) * (Y < hy)
        side = np.clip((xs - cx) / hw * 1.4 + 0.5, 0, 1)[None, :]  # 0 lit (left) .. 1 shadow
        body = (C("#262a40") * (1 - side[..., None]) + C("#14151f") * side[..., None])
        haze = smoothstep(hy - ph, hy, Y)[..., None]
        body = body * (1 - 0.35 * haze) + C("#2b2747") * 0.35 * haze
        r = xs / self.ref_w * TAU
        ph1, ph2, ph3 = rng.uniform(0, TAU, 3)
        jag = 0.5 + 0.5 * (0.5 * np.sin(r * 23 + ph1) + 0.3 * np.sin(r * 41 + ph2) + 0.2 * np.sin(r * 77 + ph3))
        ysnow = (hy - ph) + ph * (0.30 + 0.10 * jag)
        snow = m * np.clip((ysnow[None, :] - Y) / 1.5, 0, 1)
        sv = smoothstep(hy - ph, ysnow.max(), Y)[..., None]
        snowc = (C("#b8c0e8") * (1 - side[..., None]) + C("#4a5280") * side[..., None]) * (1 - 0.25 * sv)
        self.m_fuji, self.body, self.snow, self.snowc = m, body, snow, snowc

        self.cloud_far = cloud_strip(rng, W, int(150 * s), int(26 * W / 1920 + 6), s, 9 * s)
        self.cloud_near = cloud_strip(rng, W, int(190 * s), int(30 * W / 1920 + 6), s, 11 * s)
        self.cf_y = int(hy - ph * 0.86)
        self.cn_y = int(hy - 210 * s)

        # foreground mound + torii
        tx = 0.2 * W if self.portrait else 0.16 * W
        mound_top = hy + 0.05 * (H - hy) - 40 * s
        def fg(d, k):
            pts = [(-10, H + 5)]
            for i in range(61):
                x = -10 + i * (tx + 1100 * s) / 60
                y = mound_top + (55 * s) * ((x - tx) / (360 * s)) ** 2
                pts.append((x, min(y, H + 5)))
            pts.append((tx + 1100 * s, H + 5))
            d.polygon([(x * k, y * k) for x, y in pts], fill=255)
        self.m_mound = ss_mask(W, H, fg)
        bt = mound_top + 4 * s
        th = 235 * s
        top = bt - th
        def T(x0, y0, x1, y1):
            return [(tx + x0 * s, top + y0 * s), (tx + x1 * s, top + y1 * s)]
        def torii(d, k):
            def rect(x0, y0, x1, y1):
                d.rectangle([(tx + x0 * s) * k, (top + y0 * s) * k, (tx + x1 * s) * k, (top + y1 * s) * k], fill=255)
            # kasagi with upswept ends
            d.polygon([((tx + x * s) * k, (top + y * s) * k) for x, y in
                       [(-122, -10), (-100, 2), (100, 2), (122, -10), (118, 6), (96, 16), (-96, 16), (-118, 6)]], fill=255)
            rect(-98, 16, 98, 26)       # shimaki
            rect(-104, 62, 104, 74)     # nuki
            rect(-6, 26, 6, 62)         # gakuzuka
            for sx in (-1, 1):          # pillars, slightly splayed
                d.polygon([((tx + (sx * 70 - 8) * s) * k, (top + 26 * s) * k), ((tx + (sx * 70 + 8) * s) * k, (top + 26 * s) * k),
                           ((tx + (sx * 76 + 9) * s) * k, bt * k), ((tx + (sx * 76 - 9) * s) * k, bt * k)], fill=255)
        self.m_torii = ss_mask(W, H, torii)
        self.lake = vgrad(H - hy, W, [(0, "#14142a"), (1, "#0b0b12")])
        self.ripk = self.cycles(6)

        # petals
        self.petals = []
        for _ in range(int(140 * self.area)):
            sz = rng.uniform(3.5, 8) * s
            span = H + 40 * s
            speed = rng.uniform(45, 110) * s * (0.6 + sz / (8 * s) * 0.6)
            self.petals.append(dict(
                y0=rng.uniform(0, span), span=span, n=max(1, round(speed * self.D / span)),
                x0=rng.uniform(0, W + 80 * s), rx=sz, ry=sz * rng.uniform(0.45, 0.65),
                rot0=rng.uniform(0, TAU), rk=int(rng.choice([-2, -1, 1, 2])),
                fk=int(rng.integers(2, 6)), fph=rng.random(), sw=rng.uniform(10, 40) * s,
                sk=int(rng.integers(1, 4)), sph=rng.random(), v=int(rng.uniform(150, 255))))
        self.petal_col = C("red") * 0.55 + C("fg") * 0.45

    def frame(self, p):
        W, H, hy, s = self.W, self.H, self.hy, self.s
        img = self.sky.copy()
        draw_stars(img, self.stars, p)
        def cloud(strip, y, n, col, amt):
            a = roll_sub(strip, p * n * W)[..., None] * amt
            h = strip.shape[0]
            img[y:y + h] = img[y:y + h] * (1 - a) + C(col) * a
        cloud(self.cloud_far, self.cf_y, 1, "#2a2c45", 0.55)
        m = self.m_fuji[..., None]
        img = img * (1 - m) + self.body * m
        sn = self.snow[..., None]
        img = img * (1 - sn) + self.snowc * sn
        cloud(self.cloud_near, self.cn_y, 2, "#30324d", 0.7)
        reflect(img, hy, H, p, self.ripk, self.lake, 0.45)
        for mm, col in ((self.m_mound, "#09090e"), (self.m_torii, "#3a1622")):
            img = img * (1 - mm[..., None]) + C(col) * mm[..., None]
        # petals (2x supersampled)
        k = 2
        pl = Image.new("L", (W * k, H * k), 0)
        d = ImageDraw.Draw(pl)
        ang = np.linspace(0, TAU, 9)[:-1]
        for q in self.petals:
            y = (q["y0"] + q["n"] * p * q["span"]) % q["span"] - 20 * s
            x = (q["x0"] + y * 0.35 + q["sw"] * np.sin(TAU * (q["sk"] * p + q["sph"]))) % (W + 80 * s) - 40 * s
            rot = q["rot0"] + TAU * q["rk"] * p
            fl = 0.25 + 0.75 * abs(np.cos(TAU * (q["fk"] * p + q["fph"])))
            ex, ey = q["rx"] * np.cos(ang), q["ry"] * fl * np.sin(ang) * (1 + 0.25 * np.cos(ang))
            px = x + ex * np.cos(rot) - ey * np.sin(rot)
            py = y + ex * np.sin(rot) + ey * np.cos(rot)
            d.polygon([(a * k, b * k) for a, b in zip(px, py)], fill=q["v"])
        a = np.asarray(pl.resize((W, H), Image.BOX), np.float32)[..., None] / 255.0
        img = img * (1 - a) + self.petal_col * a
        return img


# ----------------------------------------------------------------- 7. moonlit ocean
class Ocean(Scene):
    name = "ocean"

    def setup(self):
        W, H, rng, s = self.W, self.H, self.rng, self.s
        self.hy = hy = int((0.58 if self.portrait else 0.55) * H)
        h = hy / H
        img = vgrad(H, W, [(0, "#0a0a12"), (h * 0.6, "#141522"), (h, "#262640"), (1, "#262640")])
        self.mx = mx = 0.5 * W if self.portrait else 0.62 * W
        my = hy - 310 * s
        self.stars = make_stars(rng, int(420 * self.area), W, hy - 30 * s)
        img = moon_layer(img, W, H, mx, my, 52 * s, s, 1.2)
        yy = np.arange(H, dtype=np.float32)[:, None, None]
        img += np.exp(-((yy - hy) / (40 * s)) ** 2) * C("blue0") * 0.35
        self.sky = img
        self.clouds = [(cloud_strip(rng, W, int(90 * s), int(18 * W / 1920 + 5), s, 8 * s), int(my - 160 * s), 1, 0.45),
                       (cloud_strip(rng, W, int(70 * s), int(14 * W / 1920 + 5), s, 6 * s), int(my + 40 * s), 2, 0.35)]
        # little islands on the horizon
        def islands(d, k):
            for cxf, wf, hf in ((0.12, 0.16, 26), (0.83, 0.22, 38), (0.95, 0.08, 16)):
                cx, w = cxf * W, wf * W
                xs = np.linspace(cx - w / 2, cx + w / 2, 60)
                t = (xs - cx) / (w / 2)
                ys = hy - hf * s * np.clip(1 - t * t, 0, 1) ** 0.6 * (1 + 0.15 * np.sin(xs / (23 * s)))
                d.polygon([(x * k, y * k) for x, y in zip(xs, ys)] + [((cx + w / 2) * k, (hy + 1) * k), ((cx - w / 2) * k, (hy + 1) * k)], fill=255)
        self.m_isl = ss_mask(W, H, islands)

        fh = H - hy
        self.K, self.F = 2000 * s, 900 * s
        dy = np.arange(fh, dtype=np.float32)[:, None] + 0.5
        x = np.arange(W, dtype=np.float32)[None, :] - W / 2
        self.dy, self.z = dy, self.K / dy
        self.xw = x * self.z / self.F
        self.pxw = dy * dy / self.K  # screen px per world unit (vertical)
        self.water = vgrad(fh, W, [(0, "#22223a"), (0.12, "#15162a"), (1, "#0b0b14")])
        self.waves = []
        for k, th, a, m in ((1.6, 88, 1.0, 2), (2.7, 74, 0.6, 3), (3.9, 106, 0.42, 4), (6.1, 62, 0.25, 6), (8.7, 121, 0.17, 8)):
            t = np.radians(th + rng.uniform(-4, 4))
            fade = smoothstep(4, 14, (TAU / k) * self.pxw)
            self.waves.append((k * np.cos(t), k * np.sin(t), a * fade, a * k * fade, m, rng.uniform(0, TAU)))
        self.norm = sum(w[3] for w in self.waves) + 1e-6
        hwid = (22 + 0.4 * dy) * s
        self.col = np.exp(-((x + W / 2 - mx) / hwid) ** 2)
        self.gcol = C("fg") * 0.7 + C("cyan") * 0.3

    def frame(self, p):
        W, H, hy = self.W, self.H, self.hy
        img = self.sky.copy()
        draw_stars(img, self.stars, p)
        for strip, y, n, amt in self.clouds:
            a = roll_sub(strip, p * n * W)[..., None] * amt
            img[y:y + strip.shape[0]] = img[y:y + strip.shape[0]] * (1 - a) + C("#2a2c45") * a
        img = img * (1 - self.m_isl[..., None]) + C("#0e0f18") * self.m_isl[..., None]
        hsum = np.zeros_like(self.xw)
        sl = np.zeros_like(self.xw)
        for kx, kz, a, ak, m, ph in self.waves:
            arg = kx * self.xw + kz * self.z + TAU * m * p + ph
            hsum += a * np.sin(arg)
            sl += ak * np.cos(arg)
        sl /= self.norm
        fh = H - hy
        rel = self.dy / fh
        water = self.water + (0.5 + 0.3 * hsum)[..., None] * C("blue0") * 0.06
        glint = self.col * smoothstep(0.2, 0.7, sl) * (1.15 - 0.6 * rel)
        water += glint[..., None] * self.gcol
        water += (self.col ** 0.5 * 0.05)[..., None] * C("blue")
        water += (smoothstep(0.82, 1.0, sl) * 0.07 * (1 - rel))[..., None] * C("blue")
        img[hy:] = water
        img[hy:hy + 1] += C("blue") * 0.25
        return img


# ----------------------------------------------------------------- 8. lava lamp / metaballs
class Lava(Scene):
    name = "lava"

    def setup(self):
        W, H, rng, s = self.W, self.H, self.rng, self.s
        self.base = vgrad(H, W, [(0, "#121219"), (0.5, "#1a1b26"), (1, "#16141f")])
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot((xx - W / 2) / (0.65 * W), (yy - H / 2) / (0.65 * H))
        self.base *= (1 - 0.3 * smoothstep(0.4, 1.4, r))[..., None]
        self.f = 4
        self.xl = (np.arange(W // self.f, dtype=np.float32) + 0.5) * self.f
        self.yl = (np.arange(H // self.f, dtype=np.float32) + 0.5) * self.f
        n = int(12 * max(1.0, self.area ** 0.5))
        self.blobs = []
        for _ in range(n):
            self.blobs.append(dict(
                R=rng.uniform(42, 105) * s, x0=rng.uniform(0.12, 0.88) * W, ax=rng.uniform(30, 160) * s,
                kx=int(rng.choice([-1, 1, 2])), phx=rng.random(),
                y0=rng.uniform(0.25, 0.75) * H, ay=rng.uniform(0.15, 0.4) * H,
                ky=int(rng.choice([-1, 1])), phy=rng.random()))
        self.col = ramp(np.linspace(0, 1, H), [(0, "blue"), (0.45, "purple"), (0.8, "red"), (1, "orange")])[:, None, :]

    def frame(self, p):
        W, H = self.W, self.H
        F = np.zeros((len(self.yl), len(self.xl)), np.float32)
        for b in self.blobs:
            x = b["x0"] + b["ax"] * np.sin(TAU * (b["kx"] * p + b["phx"]))
            y = b["y0"] + b["ay"] * np.sin(TAU * (b["ky"] * p + b["phy"]))
            dx2 = (self.xl - x) ** 2
            dy2 = (self.yl - y) ** 2
            F += b["R"] ** 2 / (dy2[:, None] + dx2[None, :] + 1.0)
        F = resize_f(F[..., None], (W, H))[..., 0]
        inside = smoothstep(0.97, 1.03, F)[..., None]
        core = ((1 - 1 / np.maximum(F, 1.0)) ** 2)[..., None]
        rim = (smoothstep(1.0, 1.12, F) * (1 - smoothstep(1.12, 1.5, F)))[..., None]
        blob = self.col * (0.72 + 0.28 * core) + C("fg") * rim * 0.12
        g = (np.clip(F, 0, 1) ** 2.5)[..., None] * self.col * 0.3
        img = self.base + g * (1 - inside)
        return img * (1 - inside) + blob * inside


# ----------------------------------------------------------------- 9. plexus network
class Plexus(Scene):
    name = "plexus"

    def setup(self):
        W, H, rng, s = self.W, self.H, self.rng, self.s
        img = vgrad(H, W, [(0, "#14141c"), (0.5, "#1a1b26"), (1, "#16161e")])
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot((xx - W / 2) / (0.6 * W), (yy - H / 2) / (0.6 * H))
        img += (np.exp(-r * r * 1.5) * 0.06)[..., None] * C("purple")
        img *= (1 - 0.35 * smoothstep(0.4, 1.4, r))[..., None]
        self.base = img
        t = np.clip((xx / W) * 0.7 + (yy / H) * 0.3, 0, 1)
        self.linecol = ramp(t, [(0, "blue"), (0.55, "#9d7cd8"), (1, "purple")])
        n = int(150 * self.area)
        self.n = n
        self.x0 = rng.uniform(-0.05, 1.05, n) * W
        self.y0 = rng.uniform(-0.05, 1.05, n) * H
        self.ax, self.ay = rng.uniform(20, 110, n) * s, rng.uniform(20, 110, n) * s
        self.kx = rng.choice([-2, -1, 1, 2], n)
        self.ky = rng.choice([-2, -1, 1, 2], n)
        self.phx, self.phy = rng.random(n), rng.random(n)
        self.depth = rng.uniform(0.45, 1.0, n)
        self.r = rng.uniform(1.6, 3.6, n) * s * (0.5 + 0.5 * self.depth)
        self.pk, self.pph = rng.integers(1, 4, n), rng.random(n)
        self.ncol = np.stack([C(c) for c in rng.choice(["cyan", "cyan", "blue", "purple", "fg"], n)])
        self.D = 205 * s
        X, Y = self.pos(0.0)
        d = np.hypot(X[:, None] - X[None, :], Y[:, None] - Y[None, :])
        cand = [(i, j) for i in range(n) for j in range(i + 1, n) if d[i, j] < 0.75 * self.D]
        idx = rng.choice(len(cand), min(len(cand), int(30 * self.area)), replace=False)
        self.packets = [(cand[i][0], cand[i][1], int(rng.integers(2, 6)) * int(rng.choice([-1, 1])), rng.random(),
                         C(rng.choice(["cyan", "red", "teal", "yellow"]))) for i in idx]
        self.pkern = gauss_kernel(int(10 * s), 2.5 * s)
        self.nkern = gauss_kernel(int(12 * s), 3.5 * s)

    def pos(self, p):
        X = self.x0 + self.ax * np.sin(TAU * (self.kx * p + self.phx))
        Y = self.y0 + self.ay * np.sin(TAU * (self.ky * p + self.phy))
        return X, Y

    def frame(self, p):
        W, H, s = self.W, self.H, self.s
        X, Y = self.pos(p)
        d = np.hypot(X[:, None] - X[None, :], Y[:, None] - Y[None, :])
        ii, jj = np.where(np.triu(d < self.D, 1))
        k = 2
        ll = Image.new("L", (W * k, H * k), 0)
        dr = ImageDraw.Draw(ll)
        for i, j in zip(ii, jj):
            a = (1 - d[i, j] / self.D) ** 1.5 * min(self.depth[i], self.depth[j])
            dr.line([(X[i] * k, Y[i] * k), (X[j] * k, Y[j] * k)], fill=int(a * 235), width=k)
        lines = np.asarray(ll.resize((W, H), Image.BOX), np.float32)[..., None] / 255.0
        layer = lines * self.linecol
        nodes = np.zeros((H, W, 3), np.float32)
        pulse = 0.6 + 0.4 * np.sin(TAU * (self.pk * p + self.pph))
        for i in range(self.n):
            if -20 < X[i] < W + 20 and -20 < Y[i] < H + 20:
                kern = self.nkern if self.r[i] > 2.6 * s else self.pkern
                add_kernel(nodes, X[i], Y[i], kern, self.ncol[i], 0.55 * pulse[i] * self.depth[i])
        for i, j, kk, ph, col in self.packets:
            if d[i, j] < self.D:
                t = frac(kk * p + ph)
                add_kernel(nodes, X[i] + (X[j] - X[i]) * t, Y[i] + (Y[j] - Y[i]) * t, self.pkern, col,
                           0.9 * (1 - d[i, j] / self.D) ** 0.5)
        layer = layer + nodes
        return self.base + layer + glow(layer, 4, 2.5) * 0.8


# ----------------------------------------------------------------- 10. topographic contours
class Topo(Scene):
    name = "topo"

    def setup(self):
        W, H, rng, s = self.W, self.H, self.rng, self.s
        base = vgrad(H, W, [(0, "#16161e"), (0.5, "#1a1b26"), (1, "#16161e")])
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot((xx - W / 2) / (0.62 * W), (yy - H / 2) / (0.62 * H))
        self.base = base * (1 - 0.35 * smoothstep(0.4, 1.4, r))[..., None]
        self.xs = np.arange(W, dtype=np.float32)
        self.ys = np.arange(H, dtype=np.float32)
        self.waves = []
        for i in range(7):
            k = rng.uniform(1.3, 4.8) * TAU / self.ref_w
            th = rng.uniform(0, TAU)
            a = 1 / (k * self.ref_w / TAU) ** 0.9
            self.waves.append((k * np.cos(th), k * np.sin(th), a, int(rng.choice([-1, 1])), rng.uniform(0, TAU)))
        tot = sum(w[2] for w in self.waves)
        self.waves = [(kx, ky, a / tot * 1.8, m, ph) for kx, ky, a, m, ph in self.waves]
        self.L = 0.075
        self.light = np.array([-0.6, -0.8], np.float32)

    def frame(self, p):
        f = np.zeros((self.H, self.W), np.float32)
        gx = np.zeros_like(f)
        gy = np.zeros_like(f)
        for kx, ky, a, m, ph in self.waves:
            sn, cs = sin_cos_2d(kx * self.xs, ky * self.ys + TAU * m * p + ph)
            f += a * sn
            gx += a * kx * cs
            gy += a * ky * cs
        q = f / self.L
        nq = np.round(q)
        g = np.sqrt(gx * gx + gy * gy) / self.L + 1e-6
        dist = np.abs(q - nq) / g
        major = (np.mod(nq, 5) == 0)
        line = np.where(major, np.clip(1.9 - dist, 0, 1) * 0.95, np.clip(1.25 - dist, 0, 1) * 0.42)
        col = ramp(np.clip(f, -1, 1) * 0.5 + 0.5, [(0, "blue0"), (0.35, "blue"), (0.65, "purple"), (1, "red")])
        layer = line[..., None] * col
        shade = (gx * self.light[0] + gy * self.light[1]) / (np.abs(gx).max() + np.abs(gy).max() + 1e-6)
        img = self.base * (1 + 0.25 * shade[..., None]) + layer
        return img + glow(layer, 4, 2.0) * 0.6


SET1 = (City, Bokeh, Aurora, Synthwave, CodeRain)
SET2 = (Fuji, Ocean, Lava, Plexus, Topo)
STYLES = {c.name: c for c in SET1 + SET2}
GROUPS = {"all": list(STYLES), "set1": [c.name for c in SET1], "set2": [c.name for c in SET2]}


# ----------------------------------------------------------------- driver
def to_u8(img, dither):
    return (np.clip(img + dither, 0, 1) * 255 + 0.5).astype(np.uint8)


def render(name, a):
    W, H = a.width, a.height
    scene = STYLES[name](W, H, a.duration, a.fps, a.seed)
    dither = (np.random.default_rng(1).random((H, W, 1), np.float32) - 0.5) / 255.0
    os.makedirs(a.outdir, exist_ok=True)
    if a.preview:
        for ph in (0.0, 0.37, 0.71):
            out = os.path.join(a.outdir, f"preview_{name}{'_portrait' if H > W else ''}_{int(ph * 100):02d}.png")
            Image.fromarray(to_u8(scene.frame(ph), dither)).save(out)
            print("wrote", out)
        return
    N = int(round(a.duration * a.fps))
    tag = f"{name}_portrait" if H > W else name
    if (W, H) not in ((1920, 1080), (1080, 1920)):
        tag += f"_{W}x{H}"
    final = os.path.join(a.outdir, f"tokyonight_{tag}.mp4")
    tmp = os.path.join(a.outdir, f".tokyonight_{tag}.partial.mp4")
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
           "-s", f"{W}x{H}", "-r", str(a.fps), "-i", "-", "-c:v", "libx264",
           "-preset", a.preset, "-crf", str(a.crf), "-x264-params", "aq-mode=3",
           "-pix_fmt", "yuv420p", "-movflags", "+faststart", tmp]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    t0 = time.time()
    for i in range(N):
        proc.stdin.write(to_u8(scene.frame(i / N), dither).tobytes())
        if i % max(1, N // 10) == 0:
            el = time.time() - t0
            print(f"[{name}] {i}/{N}  {el:.0f}s elapsed", flush=True)
    proc.stdin.close()
    if proc.wait() != 0:
        sys.exit(f"ffmpeg failed for {name}")
    os.replace(tmp, final)
    print(f"[{name}] done -> {final} ({time.time() - t0:.0f}s)")


def main():
    ap = argparse.ArgumentParser(description="Tokyo Night procedural looping video wallpapers")
    ap.add_argument("style", nargs="+", choices=list(STYLES) + list(GROUPS),
                    help="one or more styles, or a group: all / set1 / set2")
    ap.add_argument("--duration", type=float, default=30, help="loop length in seconds (default 30)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--width", type=int, default=None, help="default 1920 (1080 with --portrait)")
    ap.add_argument("--height", type=int, default=None, help="default 1080 (1920 with --portrait)")
    ap.add_argument("--portrait", action="store_true", help="vertical 1080x1920 (rotated monitors)")
    ap.add_argument("--seed", type=int, default=42, help="change for a different layout")
    ap.add_argument("--crf", type=int, default=18)
    ap.add_argument("--preset", default="veryfast")
    ap.add_argument("-o", "--outdir", default=".")
    ap.add_argument("--preview", action="store_true", help="write PNG stills instead of a video")
    a = ap.parse_args()
    dw, dh = (1080, 1920) if a.portrait else (1920, 1080)
    a.width, a.height = a.width or dw, a.height or dh
    if a.width % 2 or a.height % 2:
        sys.exit("width and height must be even (yuv420p)")
    names = []
    for st in a.style:
        for n in GROUPS.get(st, [st]):
            if n not in names:
                names.append(n)
    for n in names:
        render(n, a)


if __name__ == "__main__":
    main()
