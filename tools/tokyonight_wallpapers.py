#!/usr/bin/env python3
"""
tokyonight_wallpapers.py — procedural, seamlessly looping Tokyo Night video wallpapers.

Styles:  city, bokeh, aurora, synthwave, coderain   (or: all)

Every animated element moves with an integer number of cycles per loop,
so the last frame flows straight back into the first — no visible seam.

Requires: python3, numpy, scipy, pillow, and an ffmpeg with libx264. Noto Sans
CJK gives coderain its katakana; without it, coderain falls back to ASCII.
  Gentoo: sudo emerge dev-python/numpy dev-python/scipy dev-python/pillow \
              media-fonts/noto-cjk          # and media-video/ffmpeg with USE=x264
  Fedora: sudo dnf install python3-numpy python3-scipy python3-pillow \
              google-noto-sans-cjk-fonts ffmpeg   # RPM Fusion's ffmpeg: ffmpeg-free has no libx264

Videos land as tokyonight_<style>.mp4. Write them into fanshell-qs's wallpaper
directory (wallpaper.dir, default ~/Pictures/wallpapers) and they show up in the
wallpaper picker, played by fanvid (fanvid-qs) with VA-API.

Examples:
  ./tokyonight_wallpapers.py all -o ~/Pictures/wallpapers   # 5 videos, 1920x1080, 30 s loops
  ./tokyonight_wallpapers.py city --duration 60 --fps 60
  ./tokyonight_wallpapers.py aurora --seed 7 --width 3840 --height 2160
  ./tokyonight_wallpapers.py synthwave --preview             # write a few PNG stills only
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
        s = H / 1080
        img = vgrad(H, W, [(0, "#0c0c13"), (0.4, "#14141d"), (0.62, "#1c1b2e"),
                           (0.8, "#2a2348"), (1, "#3a2c5a")])

        # moon + halo
        mx, my, mr = 0.78 * W, 0.19 * H, 0.042 * H
        moon = ss_mask(W, H, lambda d, k: d.ellipse(
            [(mx - mr) * k, (my - mr) * k, (mx + mr) * k, (my + mr) * k], fill=255))
        halo = gaussian_filter(moon, 0.06 * H)
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
        self.stars = make_stars(rng, int(200 * W / 1920), W, 0.6 * H, keep=occluder[: int(0.6 * H)] < 0.01)
        self.stars["b"] *= 0.7

        self.antennas = [(x, y, rng.random(), self.cycles(rng.uniform(1.4, 2.6))) for x, y, _ in antennas]
        self.blink_k = gauss_kernel(int(14 * s), 4 * s)

        # rain
        self.rain = []
        for count, lmin, lmax, wdt, amin, amax, speed in (
                (int(380 * W / 1920), 14, 30, 1, 35, 70, 950),
                (int(110 * W / 1920), 40, 75, 2, 45, 90, 1500)):
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
        s = H / 1080
        img = vgrad(H, W, [(0, "#14141c"), (0.5, "#1a1b26"), (1, "#1d1b2e")])
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.hypot((xx - 0.5 * W) / (0.6 * W), (yy - 0.55 * H) / (0.6 * H))
        img *= (1 - 0.38 * smoothstep(0.3, 1.4, r))[..., None]
        self.base = img
        cols = ["blue", "blue", "purple", "purple", "cyan", "blue1", "red", "magenta2", "teal"]
        self.parts = []
        for _ in range(int(80 * W / 1920)):
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
        s = H / 1080
        self.hy = hy = int(0.80 * H)
        self.base = vgrad(H, W, [(0, "#0a0a11"), (0.45, "#11121c"), (0.8, "#1b1d31"), (1, "#1b1d31")])
        self.stars = make_stars(rng, int(650 * W / 1920), W, hy - 0.08 * H)
        self.f = 2
        self.wl, self.hl = W // self.f, hy // self.f
        self.u = (np.arange(self.wl, dtype=np.float32) / self.wl)[None, :]
        self.v = (np.arange(self.hl, dtype=np.float32) * self.f / H)[:, None]

        def curtain(base, amps, sf, height, strength, stops):
            return dict(base=base, amps=amps, sf=sf, tk=[int(rng.choice([-2, -1, 1, 2])) for _ in amps],
                        ph=rng.random(len(amps)) * TAU, height=height, strength=strength, stops=stops,
                        rf=[int(rng.integers(22, 34)), int(rng.integers(45, 70))],
                        rk=[int(rng.choice([-4, -3, 3, 4])), int(rng.choice([-6, -5, 5, 6]))],
                        rph=rng.random(2) * TAU, ek=[1, int(rng.choice([-2, 2]))], eph=rng.random(2) * TAU)

        self.curtains = [
            curtain(0.27, [0.05, 0.028, 0.012], [1.1, 2.7, 5.3], 0.15, 0.55,
                    [(0, "blue"), (0.6, "purple"), (1.5, "#9d7cd8"), (3, "#3d2f66")]),
            curtain(0.42, [0.075, 0.035, 0.014], [0.8, 1.9, 4.4], 0.23, 1.0,
                    [(0, "teal"), (0.3, "#7dcfff"), (0.8, "blue"), (1.5, "purple"), (3, "#3a2e63")]),
        ]
        # silhouettes
        xs = np.arange(-10, W + 20, 6)
        def ridge(base_y, amp, seeds):
            y = np.full(len(xs), base_y, np.float32)
            for a, f in seeds:
                y -= a * np.sin(xs / W * TAU * f + rng.uniform(0, TAU))
            y -= rng.uniform(-1, 1, len(xs)) * 3 * s
            return y
        far = ridge(0.705 * H, 1, [(0.045 * H, 1.3), (0.02 * H, 3.1), (0.008 * H, 9.0)])
        near = ridge(0.755 * H, 1, [(0.02 * H, 2.1), (0.012 * H, 5.0), (0.005 * H, 13.0)])
        def poly(ys):
            return lambda d, k: d.polygon([(x * k, y * k) for x, y in zip(xs, ys)] +
                                          [((W + 20) * k, (hy + 2) * k), (-10 * k, (hy + 2) * k)], fill=255)
        self.m_far = ss_mask(W, H, poly(far))
        self.m_near = ss_mask(W, H, poly(near))

        def trees(d, k):
            x = -10.0
            while x < W + 10:
                cl = 0.5 + 0.5 * np.sin(x / W * TAU * 3.3 + 1.0)
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
        s = H / 1080
        self.hy = hy = int(0.62 * H)
        img = vgrad(H, W, [(0, "#0b0b13"), (0.3, "#14141d"), (0.5, "#221d3a"), (0.62, "#3b2858"), (1, "#3b2858")])
        yy = np.arange(H, dtype=np.float32)[:, None, None]
        img += np.exp(-((yy - hy) / (0.07 * H)) ** 2) * C("red") * 0.25
        self.sky_stars = make_stars(rng, int(260 * W / 1920), W, 0.45 * H)

        # sun
        self.cx, self.cy, self.R = W / 2, hy - 0.10 * H, 0.235 * H
        cx, cy, R = self.cx, self.cy, self.R
        self.sun_mask = ss_mask(W, H, lambda d, k: d.ellipse([(cx - R) * k, (cy - R) * k, (cx + R) * k, (cy + R) * k], fill=255))
        self.sy0, self.sy1 = int(cy - R - 2), min(hy + 2, int(cy + R + 2))
        sv = (np.arange(H, dtype=np.float32) - cy) / R
        self.sun_col = ramp(sv, [(-1, "yellow"), (-0.25, "orange"), (0.35, "red"), (1, "purple")])[:, None, :]
        sg = gaussian_filter(self.sun_mask, 0.07 * H)
        img += sg[..., None] * ramp(sv, [(-1, "orange"), (1, "red")])[:, None, :] * 0.9
        self.sv = sv
        self.stripe_k = self.cycles(3.5)

        # mountains
        xs = np.arange(-10, W + 20, 8)
        def ridge(base, amps):
            y = np.full(len(xs), base, np.float32)
            for a, f in amps:
                y -= a * np.abs(np.sin(xs / W * TAU * f + rng.uniform(0, TAU))) ** 1.5
            return y
        self.ridges = []
        for base, amps, col in ((hy - 0.0 * H, [(0.11 * H, 1.6), (0.05 * H, 4.2), (0.015 * H, 11)], "#1a1430"),
                                (hy + 1, [(0.06 * H, 2.4), (0.03 * H, 6.1), (0.01 * H, 15)], "#0f0c19")):
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
        rx = np.exp(-(x / (0.11 * W)) ** 2)
        floor += (rx * np.exp(-dy / (0.18 * fh)))[..., None] * C("orange") * 0.22
        floor += np.exp(-dy / (0.06 * fh))[..., None] * C("red") * 0.35
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
        s = H / 1080
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


STYLES = {c.name: c for c in (City, Bokeh, Aurora, Synthwave, CodeRain)}


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
            out = os.path.join(a.outdir, f"preview_{name}_{int(ph * 100):02d}.png")
            Image.fromarray(to_u8(scene.frame(ph), dither)).save(out)
            print("wrote", out)
        return
    N = int(round(a.duration * a.fps))
    final = os.path.join(a.outdir, f"tokyonight_{name}.mp4")
    tmp = os.path.join(a.outdir, f".tokyonight_{name}.partial.mp4")
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
    ap.add_argument("style", choices=list(STYLES) + ["all"])
    ap.add_argument("--duration", type=float, default=30, help="loop length in seconds (default 30)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--seed", type=int, default=42, help="change for a different layout")
    ap.add_argument("--crf", type=int, default=18)
    ap.add_argument("--preset", default="veryfast")
    ap.add_argument("-o", "--outdir", default=".")
    ap.add_argument("--preview", action="store_true", help="write PNG stills instead of a video")
    a = ap.parse_args()
    for n in (STYLES if a.style == "all" else [a.style]):
        render(n, a)


if __name__ == "__main__":
    main()
