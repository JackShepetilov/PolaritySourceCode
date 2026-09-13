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
- `ed5_turret.py`: `IA_FeedTurret` на первую свободную клавишу из списка в `IMC_Weapons`, `FeedAction` на
  персонаже, `BP_Buildable_Turret` на `ATurretBuildable` (тиски на кубе, тяжёлые стволы, дроп-классы),
  `DA_HitFeedback_Turret` (копия набора, вдвое тише), `RemoteHitMarkerImage` в прицеле. **Только после
  полного ребилда** с `ATurretBuildable`. Клавишу и список тяжёлых стволов проверить в логе.

- `ed6_turret_ranges.py`: стартовые `MountedRangeCm` на стволах (классовые + те, что дают дропы) и
  потолок радиуса турели 40 м. **Только после ребилда** с полем `MountedRangeCm`.

- `ed7_feed_widget.py`: `WBP_TurretFeed(Entry)`, слот `HUD.Slot.TurretFeed` в `WBP_HudRoot`, строка в
  `DA_HudLayout`, Escape в `IMC_BuildMenu`. **Только после ребилда** с `UTurretFeedWidget`.

`ed1`..`ed3`, `ed5`, `ed6` выполнены 2026-09-13; `ed4` и `ed7` ждут ребилда с виджетом.
