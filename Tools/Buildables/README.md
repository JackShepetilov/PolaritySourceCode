# Скрипты постройки инженера (2026-09-13)

Запускать в редакторе через запускалку (см. `Docs/Gotchas/Python_Editor.md`, «длинный скрипт»):

```python
import unreal
p = "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/Buildables/ed4_refusal" + "." + "py"
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), {"__name__": "__main__", "__file__": p})
```

- `ed1_assets.py`: теги, палитра, материалы призрака, `BP_Buildable_*`, `BP_BuildablePreview`, `DA_Buildable_*`. Идемпотентен.
- `ed2_input.py`: `IA_Build*`, `IMC_BuildMenu`, `IMC_BuildPlacement`, `B` в `IMC_Weapons`, компонент на `BP_ShooterCharacter1`.
- `ed3_widgets.py`: `WBP_BuildMenu(Entry)`, `WBP_BuildableStatus(Entry)`, слоты в `WBP_HudRoot`, строки в `DA_HudLayout`.
- `ed4_refusal.py`: цвет и звук отказа в `WBP_BuildMenu`. **Только после полного ребилда** с `RefusedFlashTag`/`RefusedSound`.

Все уже выполнены 2026-09-13, кроме `ed4_refusal.py`.
