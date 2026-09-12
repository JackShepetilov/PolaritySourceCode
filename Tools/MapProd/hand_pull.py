# hand_pull.py
# Забрать ручную лепку из подуровня точки и сохранить её как РАЗНИЦУ.
#
# Зачем так, а не «второй слой редактирования». План из хендоффа предполагал, что генератор пишет в
# слой base, а автор лепит в слой hand, и они живут рядом. Он неисполним: в питон-API ландшафта нет
# ни чтения, ни записи ОТДЕЛЬНОГО слоя редактирования. `export_heightmap` отдаёт итог всех слоёв
# разом, а `list_layers` это про КРАСЯЩИЕ слои, не про слои редактирования. Проверено 2026-09-07.
#
# Что работает вместо этого: помнить, что мы в точку положили, и вычитать это из того, что там
# лежит сейчас. Разница и есть рука. Она хранится своим файлом, переживает любую пересборку рецепта
# и накладывается поверх нового рельефа.
#
# Цикл:
#   1. ue_import_poi.py  - залить рельеф и запомнить эталон (imported.png)
#   2. автор лепит в редакторе
#   3. ue_export_poi.py  - выгрузить, что стало (exported.png)
#   4. hand_pull.py I2   - посчитать разницу -> hand.png
#   5. terrain.py        - собрать заново; hand.png ложится поверх сам
#
# ВАЖНО про воду. Всё, что лепит ландшафт кистью (WaterBrushManager с включённым Affects Landscape),
# попадает в разницу как «ручная правка» и запечётся в неё навсегда, а потом кисть наложится ещё
# раз поверх. Перед первым pull воду надо снять с ландшафта.

import argparse
import io
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

# Ноль разницы в 16 битах. Смещение, а не знаковый тип, потому что PNG знаковых не знает.
ZERO = 32768


def poi_dir(sid):
    return os.path.join(HERE, "poi", sid)


def read_raw(path):
    """16-битная карта высот в сыром виде, как её понимает Unreal."""
    img = Image.open(path)
    if img.mode != "I;16":
        img = img.convert("I;16")
    return np.asarray(img).astype(np.int32)


def pull(sid, quiet=False):
    d = poi_dir(sid)
    base_p = os.path.join(d, "imported.png")
    now_p = os.path.join(d, "exported.png")
    out_p = os.path.join(d, "hand.png")

    for p in (base_p, now_p):
        if not os.path.exists(p):
            raise SystemExit("нет файла {}.\n"
                             "  imported.png кладёт ue_import_poi.py, exported.png - ue_export_poi.py"
                             .format(p))

    base = read_raw(base_p)
    now = read_raw(now_p)
    if base.shape != now.shape:
        raise SystemExit("размеры не совпадают: эталон {} против выгрузки {}"
                         .format(base.shape, now.shape))

    delta = now - base
    moved = int(np.count_nonzero(delta))
    if moved == 0:
        if not quiet:
            print("{}: ручных правок нет, разница пустая".format(sid))
        if os.path.exists(out_p):
            os.remove(out_p)
            print("  старый hand.png удалён")
        return 0

    stored = np.clip(delta + ZERO, 0, 65535).astype(np.uint16)
    lost = int(np.count_nonzero((delta + ZERO < 0) | (delta + ZERO > 65535)))
    Image.fromarray(stored, mode="I;16").save(out_p)

    # В метрах, чтобы число было человеческим. z_scale берётся из спека при наложении, здесь
    # печатаем по стандартным 100.
    z_m = delta.astype(np.float64) * 100.0 / 12800.0
    if not quiet:
        print("{}: {} клеток тронуто ({:.2f}% поля)".format(sid, moved, 100.0 * moved / delta.size))
        print("  разница {:+.2f} .. {:+.2f} м -> {}".format(z_m.min(), z_m.max(), out_p))
        if lost:
            print("  ВНИМАНИЕ: {} клеток не влезли в 16 бит и обрезаны".format(lost))
    return moved


def main():
    ap = argparse.ArgumentParser(description="Разница между эталоном и вылепленным вручную")
    ap.add_argument("poi", help="ид слота, например I2")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    pull(a.poi, a.quiet)


if __name__ == "__main__":
    main()
