# Handoff: создание арен-подуровней (брифинг для нового чата)

> Самодостаточный бриф по итогам сессии 2026-06-10. Роль: левелдизайнер блокаутов generic-арен Polarity. Вместе с этим файлом читать: `Polarity/Docs/LevelDesign.md` (правила, метрики, архетипы — главный документ) и `Source/Tools/ArenaBlockout/` (инструменты).

## Что уже существует

**5 generic-арен биома 1 построены, отплейтещены автором и исправлены** (карты: `/Game/Variant_Shooter/Arenas/Biome1/<Name>/Lvl_<Name>`):

| Арена | Суть | Статус плейтеста |
|---|---|---|
| A1_Pier | S-интро: пирс, контейнерная восьмёрка, кран-L2 | Навигация ожила после фиксов |
| A2_Courtyard | M-эталон: двор 50×50, балконы +350, рампы+линки | ✅ одобрена; милишники прыгают по линкам |
| A3_Dome | M-L вертикаль: купол, спираль тумб, турель на вышке | Исправлена (турель, лесенки, доступ к антенне) |
| A4_Hangar | M интерьер: серверные ряды, потолок 900, светолюк | Гуманоид навигировал отлично; остальные — см. AbortMove ниже |
| A5_Amphitheater | L финал: 3 террасы к воде, вышка, сцена | ✅ навигация работает |

Спеки арен: `Source/Tools/ArenaBlockout/Arenas/*.json` — **JSON = источник истины**, правка арены = правка JSON + пересборка.

## Пайплайн (проверен ~20 сборками)

- **Сборка**: `build_arena.py <Имя> [--shots-only] [--quit]` читает JSON, идемпотентно пересоздаёт акторы с тегом `BLOCKOUT_<Имя>`, прокидывает менеджер/волны/триггеры/блокеры, делает бэкап `.umap`, RebuildNavigation, сейв, дамп акторов (`Build/<Имя>_dump.json` с материалами) и lit-скриншоты (`Build/Screenshots/`, камеры из секции `screenshots` спеки; PointLight-«вспышка» на камере для интерьеров).
- **Headless-запуск** (рабочая команда):
  `UnrealEditor-Cmd.exe "<proj>" /Engine/Maps/Entry "-ExecCmds=py C:\Users\Professional\AppData\Local\Temp\polarity_blockout_boot.py" -EnablePlugins=PythonScriptPlugin -DisablePlugins=VibeUE -nosound -unattended -nosplash -noLiveCoding -abslog="...\Build\<лог>.log"`
  Бутстрап в Temp читает аргументы из `$env:POLARITY_BLOCKOUT_ARGS` (например `"A2_Courtyard --quit"`). Тёплый старт ~1.5–2 мин. `-DisablePlugins=VibeUE` нужен, пока модуль VibeUE не собран.
- **In-editor**: консоль `py "...\build_arena.py" A2_Courtyard` — предпочтительно, когда редактор автора открыт (нет гонок).
- Лог-тег `[ARENA_BLOCKOUT]`; nav-отладка проекта `[NAV_DEBUG]`.

## Грабли (каждая стоила итерации — не повторять)

1. `-run=pythonscript` commandlet крашится на операциях с уровнями; `-nullrhi` крашится на спавне кастомных классов (INT_DIVIDE_BY_ZERO). Только полный редактор + ExecCmds + реальный RHI.
2. UMaterial из питона не строить — только MaterialInstanceConstant от `/Game/LevelPrototyping/Materials/M_FlatCol` (vector param **«Base Color»**). **Тинты переприменять каждый ран** — сохранённые override после загрузки рендерятся серым родителем. Поле-стакан: `M_PolygonPrototype_Glass` (слот `field`).
3. Python-имена: `unreal.RenderingLibrary` (не Kismet…), `show_flag_settings` (не flags), `EditorLevelUtils.get_levels` (у World нет `streaming_levels`).
4. **Asset registry сканируется асинхронно**: перед `does_asset_exist` обязателен `wait_for_completion` + проверка файла на диске, иначе `new_level` затирает существующую карту.
5. **Сублевелы автора святы**: он добавляет к аренам свои сублевелы (отладочный персонаж, свет). Все операции фильтровать по пакету persistent-уровня (`in_package`); поле `extra_sublevels` в спеке прикрепляет его сублевелы при каждой сборке (пути спросить у автора). Не пересобирать карту, пока автор в ней работает; бэкапы в `Build/Backups/`.
6. **NavLinkProxy: только SMART link** (`smart_link_is_relevant` + те же точки в `NavLinkCustomComponent`) — `PolarityPathFollowingComponent` прыгает по `CustomNavLinkId`. Прыжки ДЛИННЫЕ: верх ≥250–350 вглубь яруса, приземление в 650–800 от стен, точки сверять с bounds (включая yaw-повёрнутые объекты!).
7. Известный код-вопрос (на стороне автора): [PolarityPathFollowingComponent.cpp:252](../AI/Navigation/PolarityPathFollowingComponent.cpp) — при `bAllowNavLinkJumps=false` AbortMove останавливает NPC насовсем вместо перестройки пути; флаг выключается где-то в ST/BP. Гуманоид прыгает, остальные нет.
8. Навмеш после headless-сборки бывает несвежим: в билдере уже есть RebuildNavigation; если NPC тупят — Build Paths в редакторе.

## Правила автора (нарушение = переделка)

- **Без гуманоидов в биоме 1** (только роботы: shooter/melee/drone/kamikaze/turret).
- **Никакого света в persistent** (свет — сублевелом автора); верификация скриншотами.
- **Спавн-поинты: клиренс ≥250** от любых коллизий (с учётом повёрнутых bounds).
- **EMF-акселераторы не ставить** (не готовы).
- Турель: на вышке у периметра, на краю в сторону входа, рядом лесенка 2×250; мёртвая зона = 1.7×высоты — не ставить высоко в центре.
- Антенна: доступ лесенкой/ступенями обязателен; в интерьере — проём в потолке над ней (beacon в небо); под антенной всегда платформа (не висит).
- Поля-стаканы (glass, в `exit_blocker_ids`) во всех аренах — запирают дронов и игрока на время боя.
- Все геймплейные классы — BP-сабклассы (пути в LevelDesign.md §6), кроме `AArenaSpawnPoint` (C++).
- Центральный арт-якорь чаши — хороший приём, но опционален.

## Новый воркфлоу (важно!)

Автор установил **VibeUE** — MCP-плагин для прямого управления редактором из Claude (вьюпорт, блупринты и т.д.). Как только модуль собран: пробовать живую работу через MCP вместо headless-циклов; headless остаётся для батчей. Уточнить у автора статус.
