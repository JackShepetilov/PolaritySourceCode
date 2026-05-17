# Lore DataTables — Author Guide

Лор-сюжетная система. Когда игрок активирует антенну на арене, она вызывает
`ULoreSubsystem::PickAndConsumeLoreForArena(ArenaTag, Biome)`, тот выбирает
подходящую запись и запускает chat-commentary через `RunScripted`.

---

## Файлы

| CSV | Row Struct | Биом |
|---|---|---|
| `DT_Lore_Cartels.csv` | `FLoreEntryRow` | Cartels |
| `DT_Lore_Islands.csv` (когда появится) | FLoreEntryRow | Islands |
| `DT_Lore_Yachts.csv` (когда появится) | FLoreEntryRow | Yachts |
| `DT_Lore_OtherDim.csv` (когда появится) | FLoreEntryRow | OtherDim |

Шардинг по биомам — не обязательное правило, можно одну общую таблицу.
Сейчас по биомам сделано чтобы редактирование одного биома не задевало
остальные и Excel-файлы оставались маленькими.

---

## Workflow

1. Открыть CSV в редакторе
2. Добавить новую строку
3. Сохранить
4. В Unreal: **Content Browser → правый клик на DT → Reimport**
5. Убедиться что DT подключён в **`DA_StreamConfig → Lore → Lore Tables`**

Для каждого нового lore-entry **обязательно** нужно создать соответствующую
сценку в `DT_ChatScripted` (см. ниже). Без неё entry консьюмится но никакой
комментарий не играет.

---

## Колонки `FLoreEntryRow`

| Поле | Тип | Назначение |
|---|---|---|
| `Name` (RowName) | FName | Уникальное имя строки (`lore_cart_001`, любое) |
| `LoreID` | FName | **Уникальный ID** в рамках ВСЕХ lore-таблиц. Используется в prereqs и save game |
| `Biome` | FName | Биом: `Cartels` / `Islands` / `Yachts` / `OtherDim` |
| `ArenaTag` | FName | Тег конкретной арены (`cartel_office_main`); пусто если scope не Arena |
| `Scope` | enum | `Arena` (специфичный) / `Biome` (общий для биома) / `Global` (любая арена) |
| `RequiredLoreIDs` | TArray<FName> | Должны быть consumed раньше чем этот станет доступен (паззл) |
| `ChatScriptedSequenceID` | FName | ID сценки в `DT_ChatScripted` для комментария чата |
| `VoiceLineID` | FName | Зарезервировано под голос друга (пока пусто) |
| `Priority` | int32 | Чем больше — тем раньше выпадет при ничьих. 0..10 типично |
| `DesignerNotes` | FText | Editor-only пометка для дизайнера — не используется в игре |

---

## Scope-иерархия выбора

Когда антенна активируется на арене `cartel_office_main` в биоме `Cartels`,
`PickAndConsumeLoreForArena` идёт по фазам:

```
Phase 1: Arena-specific  → Scope==Arena AND ArenaTag=="cartel_office_main"
Phase 2: Biome-general   → Scope==Biome AND Biome=="Cartels"
Phase 3: Global          → Scope==Global
```

Если первая фаза дала candidates → выбирает из неё. Если пустая → переходит к
следующей. Внутри фазы:
- Фильтр: consumed=false, prerequisites met
- Сортировка по Priority desc
- Среди ties at top priority — **uniform random**

**На практике:**
- Хочешь чтобы на конкретной арене играл уникальный лор — `Scope=Arena` + `ArenaTag`
- Хочешь общую инфу по биому когда арена-специфичные исчерпаны — `Scope=Biome` + `Biome=...`
- Хочешь fallback везде — `Scope=Global` (без `ArenaTag` и без `Biome`)

---

## Prerequisites (паззл)

Если хочешь чтобы записи открывались последовательно — заполни
`RequiredLoreIDs`. До тех пор пока ВСЕ required не consumed, запись невидима
для picker'а.

**Пример (двухступенчатый reveal):**
```csv
lore_cart_001,villain_laws_homophobic,Cartels,cartel_office_main,Arena,,lore_cart_villain_laws,,10,"Villain pushes homophobic laws"
lore_cart_002,villain_closet_gay,Cartels,yacht_master_suite,Arena,villain_laws_homophobic,lore_cart_villain_closet,,10,"Reveals he's closeted — requires laws first"
```

Порядок: игрок должен сначала зайти в `cartel_office_main` (получит laws) →
только потом в `yacht_master_suite` ему откроется closet_gay. Иначе на яхте
выпадет либо biome-general, либо ничего.

**Если не хочешь паззлов** — оставь `RequiredLoreIDs` пустым у всех записей.

---

## Связка с `DT_ChatScripted`

Каждый lore-entry должен указывать `ChatScriptedSequenceID`, и в
`DT_ChatScripted` должна быть последовательность с этим ID.

**Пример pair'а:**

`DT_Lore_Cartels.csv`:
```csv
lore_cart_001,villain_laws_homophobic,Cartels,cartel_office_main,Arena,,lore_cart_villain_laws,,10,"..."
```

`DT_ChatScripted.csv`:
```csv
lore_cart_laws_0,lore_cart_villain_laws,0,0.5,per_witch,,what is on these files
lore_cart_laws_1,lore_cart_villain_laws,1,2.5,per_chad,,wait — homophobic lobby docs?
lore_cart_laws_2,lore_cart_villain_laws,2,2.8,per_leet,,@based_dept told you he was a piece of shit
lore_cart_laws_3,lore_cart_villain_laws,3,2.2,per_ratio,,actually proxies sign these laws not him
lore_cart_laws_4,lore_cart_villain_laws,4,1.8,per_stan,,PLOT THICKENS
```

Когда антенна на `cartel_office_main` выберет `villain_laws_homophobic`, broker
вызовет `RunScripted("lore_cart_villain_laws")` → 5 шагов отыграются с
указанными задержками.

---

## Конвенции

### `LoreID` — глобально уникальный

Один и тот же LoreID **не должен** появляться в нескольких таблицах. Это
полностью уникальный идентификатор записи. Соглашение:
- `<biome>_<topic>_<specifics>` — например `cartel_villain_laws_homophobic`,
  `yacht_party_witness_3`, `otherdim_origin_revelation`

### `ArenaTag` — повторяется между уровнями

Один и тот же `ArenaTag` (например `cartel_office_main`) может встречаться на
разных уровнях если у тебя несколько локаций «офис картеля». Соглашение:
- Имена в snake_case
- Префиксом биом или фракция
- Видо локации (`_office_main`, `_lab_alpha`, `_warehouse_west`)

**Этот же тег** ставится на `AArenaAntenna → Antenna → Lore → Arena Tag For Lore`
в каждом уровне.

### `Biome` — стандартизованные FName

Сейчас в использовании:
- `Cartels`
- `Islands`
- `Yachts`
- `OtherDim`

Будешь добавлять новый биом — придумай короткий, неизменяемый, CamelCase
тег. **Всегда сверяй** с тем что в `UStreamArenaConfig.Biome` (поле арены).
Мисматч = лор не находится.

### `Priority` — диапазон

Соглашение:
- `10` — главный raveal (сюжет-ключевая инфа этой арены)
- `5` — побочные раскрытия (мотивы NPC, побочные документы)
- `1-3` — filler / flavor (мелочи про злодея, easter-eggs)

При равенстве — random pick.

---

## Как добавить новую запись (полный flow)

1. **Открыть** `DT_Lore_<биом>.csv`
2. **Придумать LoreID** — глобально уникальный (типа `cartel_lab_subject_7_files`)
3. **Решить scope:**
   - Уникален для конкретной арены → `Scope=Arena`, `ArenaTag=cartel_lab_alpha`
   - Общая инфа по биому → `Scope=Biome`, `Biome=Cartels`, `ArenaTag=`
   - Везде → `Scope=Global`, оба пустые
4. **Решить про prereqs:**
   - Самостоятельная запись → `RequiredLoreIDs=` (пусто)
   - Раскрывает что-то предыдущее → перечислить через запятую в массиве
5. **Придумать `ChatScriptedSequenceID`** — например `lore_cart_lab_subject_7`
6. **Заполнить строку в `DT_Lore_<биом>`:**
   ```csv
   lore_cart_NNN,cartel_lab_subject_7_files,Cartels,cartel_lab_alpha,Arena,,lore_cart_lab_subject_7,,5,"Subject 7 was Anton — childhood friend"
   ```
7. **Создать сценку в `DT_ChatScripted.csv`** под этим ID — 3-6 шагов
8. **Reimport обе таблицы** в редакторе
9. **Тестировать:**
   ```
   lore.reset                                           ← на всякий
   lore.trigger cartel_lab_alpha Cartels               ← должен выпасть твой entry
   ```
   В чате должна заиграть сценка. В Output Log:
   ```
   [LORE_DEBUG] Picked cartel_lab_subject_7_files (scope=0, priority=5)
   [STREAM_DEBUG] Started scripted 'lore_cart_lab_subject_7' (...)
   ```

---

## Add a new biome (полный setup)

Когда захочешь второй биом (например Yachts):

1. Создать `DT_Lore_Yachts.csv` рядом со старым (тот же row struct `FLoreEntryRow`)
2. Заполнить парой entries — хотя бы один `Scope=Biome` для fallback
3. В **Unreal: Content Browser → Import → DT_Lore_Yachts.csv → Row Struct = FLoreEntryRow**
4. **`DA_StreamConfig → Lore → Lore Tables`** → добавить элемент → DT_Lore_Yachts
5. Создать `DA_UStreamArenaConfig_Yacht1`, на нём поле **Biome = `Yachts`**
6. На уровне яхты привязать этот ArenaConfig (через Level BP или ArenaManager BP)
7. На антеннах поставить `Arena Tag For Lore = yacht_master_suite` (или какой выберешь)

---

## Тестирование через консоль

Полный список: `lore.help`.

- `lore.list` — все записи со статусом (CONSUMED / available / locked)
- `lore.unconsumed <Biome>` — что ещё не услышано в биоме
- `lore.consume <LoreID>` — вручную пометить consumed (для тестов prereq-цепочек)
- `lore.reset` — обнулить ВСЕ consumed (начать заново)
- `lore.trigger <ArenaTag> [Biome]` — симулировать активацию антенны

---

## SaveGame (будущее)

`ConsumedLoreIDs` объявлен как `UPROPERTY(SaveGame)`, но реальной интеграции с
SaveGame пока нет. Сейчас прогресс живёт только в текущей PIE-сессии.
Когда SaveGame подключим — игрок будет продолжать с того места где остановился.
