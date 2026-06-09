"""
Bake an Unreal Engine Color Grading LUT (256x16, 16^3 neutral cube, Epic layout).

Pipeline (display-referred sRGB, applied AFTER ACES tonemap):
  white balance -> lift/gamma/gain -> pivoted contrast / tone -> per-hue saturation + hue rotation

Spec conventions (match the workflow's SPEC_CONVENTIONS):
  whiteBalance.tempShift  -0.3..0.3   (+ warmer/amber)
  whiteBalance.tintShift  -0.3..0.3   (+ greener)
  toneCurve.blackPoint    0..0.06
  toneCurve.whitePoint    0.94..1.0
  toneCurve.pivot         ~0.4..0.5
  toneCurve.contrast      0..0.4
  liftGammaGain.lift[3]   each -0.08..0.08   (0 neutral)
  liftGammaGain.gamma[3]  each 0.8..1.25      (1 neutral)
  liftGammaGain.gain[3]   each 0.85..1.2      (1 neutral)
  globalSaturation        0.5..1.2 (1 neutral)
  perHueSaturation{8}     0.4..1.3 multiplier (1 neutral)
  perHueRotation{8}       -30..30 degrees
Hue band centers (HSL deg): red 0, orange 30, yellow 60, green 120, aqua 165, blue 220, purple 275, magenta 320
"""
import sys, json
import numpy as np
from PIL import Image

SIZE = 16  # cube resolution -> 256x16 image

HUE_CENTERS = {
    'red': 0.0, 'orange': 30.0, 'yellow': 60.0, 'green': 120.0,
    'aqua': 165.0, 'blue': 220.0, 'purple': 275.0, 'magenta': 320.0,
}
HUE_ORDER = ['red', 'orange', 'yellow', 'green', 'aqua', 'blue', 'purple', 'magenta']
BAND_SIGMA = 28.0  # degrees, smooth falloff between hue controls

IDENTITY_SPEC = {
    "whiteBalance": {"tempShift": 0.0, "tintShift": 0.0},
    "toneCurve": {"blackPoint": 0.0, "whitePoint": 1.0, "pivot": 0.45, "contrast": 0.0},
    "liftGammaGain": {"lift": [0, 0, 0], "gamma": [1, 1, 1], "gain": [1, 1, 1]},
    "globalSaturation": 1.0,
    "perHueSaturation": {k: 1.0 for k in HUE_ORDER},
    "perHueRotation": {k: 0.0 for k in HUE_ORDER},
}


def make_neutral_cube():
    """Return (H, W, 3) float array in [0,1] = Epic neutral LUT 256x16.
    Pixel (X,Y): slice = X//16 -> Blue; localX = X%16 -> Red; Y -> Green."""
    W, H = SIZE * SIZE, SIZE
    img = np.zeros((H, W, 3), dtype=np.float64)
    xs = np.arange(W)
    slice_idx = xs // SIZE           # blue
    local_x = xs % SIZE              # red
    for y in range(H):               # green
        img[y, :, 0] = local_x / (SIZE - 1)
        img[y, :, 1] = y / (SIZE - 1)
        img[y, :, 2] = slice_idx / (SIZE - 1)
    return img


# ---- HSL conversions (vectorized) ----
def rgb_to_hsl(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = np.max(rgb, axis=-1)
    mn = np.min(rgb, axis=-1)
    d = mx - mn
    l = (mx + mn) / 2.0
    s = np.zeros_like(l)
    nz = d > 1e-9
    s[nz] = d[nz] / (1.0 - np.abs(2.0 * l[nz] - 1.0) + 1e-12)
    h = np.zeros_like(l)
    # hue
    rm = (mx == r) & nz
    gm = (mx == g) & nz & ~rm
    bm = (mx == b) & nz & ~rm & ~gm
    h[rm] = ((g[rm] - b[rm]) / d[rm]) % 6.0
    h[gm] = ((b[gm] - r[gm]) / d[gm]) + 2.0
    h[bm] = ((r[bm] - g[bm]) / d[bm]) + 4.0
    h = h * 60.0
    h = h % 360.0
    return h, s, l


def hsl_to_rgb(h, s, l):
    c = (1.0 - np.abs(2.0 * l - 1.0)) * s
    hp = (h % 360.0) / 60.0
    x = c * (1.0 - np.abs(hp % 2.0 - 1.0))
    m = l - c / 2.0
    r = np.zeros_like(h); g = np.zeros_like(h); b = np.zeros_like(h)
    cond = (hp >= 0) & (hp < 1); r[cond], g[cond], b[cond] = c[cond], x[cond], 0
    cond = (hp >= 1) & (hp < 2); r[cond], g[cond], b[cond] = x[cond], c[cond], 0
    cond = (hp >= 2) & (hp < 3); r[cond], g[cond], b[cond] = 0, c[cond], x[cond]
    cond = (hp >= 3) & (hp < 4); r[cond], g[cond], b[cond] = 0, x[cond], c[cond]
    cond = (hp >= 4) & (hp < 5); r[cond], g[cond], b[cond] = x[cond], 0, c[cond]
    cond = (hp >= 5) & (hp < 6); r[cond], g[cond], b[cond] = c[cond], 0, x[cond]
    out = np.stack([r + m, g + m, b + m], axis=-1)
    return np.clip(out, 0.0, 1.0)


def hue_weights(h):
    """For each pixel hue h (deg), return dict-of-arrays weights per band (normalized)."""
    weights = {}
    total = np.zeros_like(h)
    for k in HUE_ORDER:
        c = HUE_CENTERS[k]
        d = np.abs((h - c + 180.0) % 360.0 - 180.0)  # circular distance 0..180
        w = np.exp(-(d * d) / (2.0 * BAND_SIGMA * BAND_SIGMA))
        weights[k] = w
        total += w
    total = np.maximum(total, 1e-9)
    for k in HUE_ORDER:
        weights[k] = weights[k] / total
    return weights


def apply_white_balance(rgb, temp, tint):
    out = rgb.copy()
    # temp: + warm => R up, B down. scale 0.5 keeps it subtle within range.
    out[..., 0] *= (1.0 + 0.5 * temp)
    out[..., 2] *= (1.0 - 0.5 * temp)
    # tint: + green => G up, R/B slightly down (magenta when negative)
    out[..., 1] *= (1.0 + 0.5 * tint)
    out[..., 0] *= (1.0 - 0.25 * tint)
    out[..., 2] *= (1.0 - 0.25 * tint)
    return np.clip(out, 0.0, 1.0)


def apply_lgg(rgb, lift, gamma, gain):
    out = rgb.copy()
    for ch in range(3):
        v = out[..., ch] * gain[ch] + lift[ch]
        v = np.clip(v, 0.0, 1.0)
        v = np.power(v, 1.0 / max(gamma[ch], 1e-3))
        out[..., ch] = v
    return np.clip(out, 0.0, 1.0)


def apply_tone(rgb, bp, wp, pivot, contrast):
    out = (rgb - bp) / max(wp - bp, 1e-3)
    out = np.clip(out, 0.0, 1.0)
    if contrast > 1e-6:
        p = float(pivot)
        k = 1.0 + contrast * 2.0  # contrast 0..0.4 -> exponent 1..1.8
        below = out < p
        ob = out.copy()
        # pivoted S-curve, stays in [0,1]
        ob_below = p * np.power(np.clip(out / max(p, 1e-4), 0, 1), k)
        ob_above = 1.0 - (1.0 - p) * np.power(np.clip((1.0 - out) / max(1.0 - p, 1e-4), 0, 1), k)
        out = np.where(below, ob_below, ob_above)
    return np.clip(out, 0.0, 1.0)


def apply_sat_hue(rgb, global_sat, per_sat, per_rot):
    h, s, l = rgb_to_hsl(rgb)
    w = hue_weights(h)
    sat_mult = np.zeros_like(h)
    rot = np.zeros_like(h)
    for k in HUE_ORDER:
        sat_mult += w[k] * per_sat[k]
        rot += w[k] * per_rot[k]
    s = np.clip(s * global_sat * sat_mult, 0.0, 1.0)
    h = (h + rot) % 360.0
    return hsl_to_rgb(h, s, l)


def apply_grade(rgb, spec):
    wb = spec["whiteBalance"]; tc = spec["toneCurve"]; lgg = spec["liftGammaGain"]
    out = apply_white_balance(rgb, float(wb["tempShift"]), float(wb["tintShift"]))
    out = apply_lgg(out, lgg["lift"], lgg["gamma"], lgg["gain"])
    out = apply_tone(out, float(tc["blackPoint"]), float(tc["whitePoint"]), float(tc["pivot"]), float(tc["contrast"]))
    out = apply_sat_hue(out, float(spec["globalSaturation"]), spec["perHueSaturation"], spec["perHueRotation"])
    return out


def save_lut(arr01, path):
    img8 = np.clip(arr01 * 255.0 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(img8, mode="RGB").save(path)
    print("wrote", path, img8.shape)


def main():
    spec_path = sys.argv[1] if len(sys.argv) > 1 else None
    out_path = sys.argv[2] if len(sys.argv) > 2 else "Island_LUT.png"
    spec = IDENTITY_SPEC
    if spec_path:
        with open(spec_path, "r", encoding="utf-8") as f:
            spec = json.load(f)
    neutral = make_neutral_cube()
    graded = apply_grade(neutral, spec)
    save_lut(graded, out_path)
    # sanity: identity round-trip error
    if spec is IDENTITY_SPEC:
        err = np.max(np.abs(graded - neutral))
        print("identity max abs error:", err)


if __name__ == "__main__":
    main()
