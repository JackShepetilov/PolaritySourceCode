# burn_preview.py (plain python, not the editor)
# The burn shader of M_FlashBurn (build_assets.BURN_HLSL) in numpy, on a screenshot, at a few ages and
# strengths, so the look can be judged without PIE.  python burn_preview.py <in.png> <out.png>
import sys
import numpy as np
from PIL import Image


def burn(scene, center, size, strength, age):
    h, w, _ = scene.shape
    ys, xs = np.mgrid[0:h, 0:w]
    uv = np.stack([(xs + 0.5) / w, (ys + 0.5) / h], -1)
    d = uv - np.array(center)
    d[..., 0] *= w / h
    r = np.linalg.norm(d, axis=-1) / max(size, 0.001)
    core = np.clip(1 - r, 0, 1)
    core = core * core * (3 - 2 * core)
    halo = np.exp(-r * r * 2.5)
    t = min(max(age, 0), 1)
    s = min(max(strength, 0), 1)
    bright = max(0.0, min(1.0, 1 - t * 4))
    ghost = max(0.0, min(1.0, (t - 0.08) * 3.0)) * (1 - t) ** 0.7
    warm = np.array([1.0, 0.96, 0.85])
    col = scene.copy()
    wash = s * s * max(0.0, min(1.0, 1 - t * 5)) * 0.35
    col = col + (warm - col) * wash
    col = col + warm * (core * 2.0 + halo * 0.5)[..., None] * s * bright
    tint = np.array([0.55, 0.38, 0.62])
    k = np.clip(core * 1.4 + halo * 0.3, 0, 1)[..., None] * ghost * np.sqrt(s) * 1.4
    col = col + (col * tint - col) * k
    lum = (col * np.array([0.3, 0.59, 0.11])).sum(-1, keepdims=True)
    col = col + (lum - col) * (np.clip(halo, 0, 1)[..., None] * s * (1 - t) * 0.5)
    return np.minimum(col, 1.0)


def main():
    img = np.asarray(Image.open(sys.argv[1]).convert("RGB").resize((480, 270)), dtype=np.float32) / 255.0
    rows = []
    for strength, size in ((1.0, 0.32), (0.4, 0.15)):
        row = [burn(img, (0.6, 0.45), size, strength, a) for a in (0.03, 0.2, 0.45, 0.8)]
        rows.append(np.concatenate(row, 1))
    out = np.concatenate(rows, 0)
    Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8)).save(sys.argv[2])
    print("saved", sys.argv[2])


main()
