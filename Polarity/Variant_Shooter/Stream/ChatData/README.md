# Chat DataTables — Author Guide

Чат-broker (`UChatBroker`) читает 5 таблиц. Каждая отвечает за свой
producer. Эта документация описывает что куда класть и как расширять.

---

## Файлы

| CSV | Row Struct | Producer / Назначение |
|---|---|---|
| `DT_ChatPersonas.csv` | `FChatPersonaRow` | Справочник ников + цветов + персональностей (ссылочный) |
| `DT_ChatAmbient.csv` | `FChatAmbientRow` | Фоновый рандомный чат (TickAmbient, по таймеру) |
| `DT_ChatReactions.csv` | `FChatReactionRow` | Реакции на события (EmitReaction по `Chat.Event.*` тегу) |
| `DT_ChatScripted.csv` | `FChatScriptedRow` | Многошаговые сценки (RunScripted по `SequenceID`) |
| `DT_ChatHints.csv` | `FChatHintRow` | Hint'ы (TickHint, XP-под-использование) |

---

## Workflow

1. Открыть CSV в **Excel / Google Sheets / Numbers / любой текстовый редактор**
2. Добавить / поправить строки
3. Сохранить как **CSV (comma-separated, UTF-8)**
4. В Unreal: **Content Browser → правый клик на DT → Reimport**
5. Или открыть DT в редакторе → нажать **Reimport** наверху

**Важно:** RowName (первая колонка) должен быть уникальным внутри таблицы.
Если поставишь существующий ID — старая строка перезапишется.

---

## DT_ChatPersonas — Регулярные ники канала

Колонки:

| Поле | Тип | Назначение |
|---|---|---|
| `Name` (RowName) | FName | ID-ссылка для других таблиц (`per_leet`, `per_witch`) |
| `Username` | FString | Отображаемый ник (`leetgamer1337`) |
| `Color` | FColor | Цвет ника в чате `(R=255,G=140,B=0,A=255)` |
| `PersonalityTag` | FName | Косметический тег: `toxic_helpful`, `bot`, `fan`, `mystic` |

**Зачем нужны personas:**
- Регулярные ники узнаются игроком от рана к рану — даёт «личность» каналу
- Цвет назначается один раз, остальные таблицы переиспользуют

**Как добавить нового регуляра:**
```csv
per_newregular,scary_uncle_joe,"(R=153,G=255,B=153,A=255)",sketchy
```
Затем в других таблицах ссылайся через `PersonaRow=per_newregular`.

---

## DT_ChatAmbient — Фоновые сообщения

Срабатывает по таймеру (`ChatAmbientIntervalMin..Max` в DA_StreamConfig).
**Не зависит от событий** — просто живой фон.

| Поле | Тип | Назначение |
|---|---|---|
| `Name` (RowName) | FName | уникальный ID строки (`amb_001`, `amb_lore_hint_03`) |
| `PersonaRow` | FName | Ссылка в `DT_ChatPersonas` (или пусто) |
| `UsernameOverride` | FString | Если `PersonaRow` пуст — этот ник; если оба пусты — broker берёт случайный |
| `Message` | FText | Текст сообщения |
| `Weight` | float | Вес для weighted-random (1.0 норма; 0.3 — реже; 5.0 — чаще) |

**Когда использовать `UsernameOverride` вместо `PersonaRow`:**
- Хочешь one-shot ник (newcomer, случайный лурker) — `UsernameOverride="new_viewer_98"`
- Регулярный персонаж — `PersonaRow=per_leet`, `UsernameOverride` пусто

**Примеры:**
```csv
amb_001,per_leet,,this music slaps,1.0
amb_002,,early_bird_42,first?,0.7
amb_003,,,whats this game,1.0
```

---

## DT_ChatReactions — Реакции на события

Срабатывает когда code вызывает `Broker->EmitReaction(EventTag)`. Подходящие
по тегу строки фильтруются, weighted-random pick.

| Поле | Тип | Назначение |
|---|---|---|
| `Name` (RowName) | FName | уникальный ID (`react_h1`, `react_antenna_done_3`) |
| `EventTag` | FGameplayTag | Тег события (`Chat.Event.Headshot`, `Chat.Event.AntennaDone`) |
| `PersonaRow` | FName | Ссылка в personas (или пусто) |
| `UsernameOverride` | FString | Override ника |
| `Message` | FText | Текст реакции |
| `Weight` | float | Вес weighted-random |

**Какие EventTag'и сейчас стреляются автоматически:**

| Тег | Когда |
|---|---|
| `Chat.Event.Kill` | Любое убийство игрока (если не подошла более специфичная категория) |
| `Chat.Event.Headshot` | Headshot kill |
| `Chat.Event.AirDashKill` | Kill в дэше |
| `Chat.Event.YankKill` | Kill yank-нутым оружием |
| `Chat.Event.MeleeKill` | Kill через melee component |
| `Chat.Event.Multikill` | ≥2 kill за 1 сек |
| `Chat.Event.ChainElectrify` | Chain electrify ≥3 (если ты хукнешь) |
| `Chat.Event.AntennaDone` | Антенна активирована |
| `Chat.Event.PlayerDeath` | Игрок умер (Schadenfreude) |
| `Chat.Event.Boredom` | LPS=0 длительно |
| `Chat.Event.HypeBurst` | LPS пересёк High threshold (60-30 раз подряд) |
| `Chat.Event.ChannelSub` | Channel event tick |
| `Chat.Event.FriendSpoke` | (заготовлено) когда голос друга появится |

**Регистрация новых тегов:**
Project Settings → Project → GameplayTags → Manage Tag Sources → твой
`Config/Tags/GameplayTags_Chat.ini` → добавь строку:
```ini
GameplayTagList=(Tag="Chat.Event.YourNewEvent",DevComment="...")
```
После этого перезапусти редактор. Можно тестить через
`stream.reaction YourNewEvent` в консоли.

**Примеры:**
```csv
react_h1,Chat.Event.Headshot,,POG that headshot,,1.0
react_h2,Chat.Event.Headshot,per_leet,,W aim actually,1.0
react_death_1,Chat.Event.PlayerDeath,,F,,1.0
react_death_2,Chat.Event.PlayerDeath,per_granny,,oh honey,1.0
```

---

## DT_ChatScripted — Многошаговые сценки

Срабатывает когда code вызывает `Broker->RunScripted(SequenceID)`. Все строки
с одинаковым `SequenceID` группируются в последовательность, проигрываются
по `StepIndex` с указанными `DelaySec`.

| Поле | Тип | Назначение |
|---|---|---|
| `Name` (RowName) | FName | Уникальный ID строки (`scr_argue_0`, `lore_cart_laws_3`) |
| `SequenceID` | FName | ID последовательности (общий для всех её шагов) |
| `StepIndex` | int32 | Порядковый номер шага (0, 1, 2...) — сортируется по возрастанию |
| `DelaySec` | float | Сколько ждать ДО этого шага, относительно предыдущего |
| `PersonaRow` | FName | Ссылка в personas |
| `UsernameOverride` | FString | Override ника |
| `Message` | FText | Текст шага |

**Где scripted-сценки запускаются автоматически:**
- `stream_opening_normal` — на старте обычного рана (returning viewers)
- `stream_opening_first` — на старте первого рана игрока (полный onboarding для новых зрителей)
- `stream_opening_returning` — для возвращающихся зрителей которые не активные регуляры (короткий recap)
- Любой `ChatScriptedSequenceID` из `DT_Lore_*` — при выпадении лора с антенны

**Можно запускать вручную** из BP или консоли:
```
stream.scripted regulars_argue
```

**Шаблон новой сценки (3 шага):**
```csv
my_seq_0,my_sequence_id,0,0.5,per_chad,,opening line
my_seq_1,my_sequence_id,1,2.5,per_leet,,response after 2.5s
my_seq_2,my_sequence_id,2,2.0,per_witch,,final line after another 2.0s
```

**Подстановка `{PlayerName}`:**
В тексте можешь использовать `{PlayerName}` — broker подменит на текущее имя
игрока (`@ramless_` по умолчанию).
```csv
my_seq_3,my_sequence_id,3,2.0,per_chad,,alright {PlayerName} show them what you got
```

**Ограничения:**
- Один и тот же `SequenceID` не может играть дважды одновременно (broker
  делает duplicate-guard и игнорирует второй вызов)
- Последовательности **не пересекаются с фазой Opening**: scripted —
  единственный producer который активен в Opening, остальные подавлены
- Прерывание: `stream.stopscripted` или `Broker->StopAllScripted()`

---

## DT_ChatHints — Подсказки

Срабатывает по таймеру (`ChatHintCheckIntervalSec`). Сейчас pickается рандомно
из таблицы. В будущем будет анализ XP-категорий через `XPSubsystem`.

| Поле | Тип | Назначение |
|---|---|---|
| `Name` (RowName) | FName | уникальный ID |
| `XPUnderUsedCategory` | FName | Категория ESkillCategory (`Melee`, `Weapon`, `EMF`, `Movement`); пусто = generic hint |
| `PersonaRow` | FName | persona |
| `UsernameOverride` | FString | override |
| `Message` | FText | Текст подсказки |
| `Weight` | float | weighted-random |

**Сейчас `XPUnderUsedCategory` игнорируется** — все строки в общем пуле.
Когда XP-анализ заработает, broker будет фильтровать по этому полю.

**Пример:**
```csv
hint_melee_1,Melee,per_leet,,даун меч хоть раз достанешь?,1.0
hint_weapon_1,Weapon,,,ты на голом пистолете полрана уже,1.0
```

---

## Привязка таблиц к Stream Config

Все таблицы должны быть подключены в **`DA_StreamConfig`** в секциях:
- **Stream → Chat → Tables → Chat Ambient Table**
- **Stream → Chat → Tables → Chat Reactions Table**
- **Stream → Chat → Tables → Chat Scripted Table**
- **Stream → Chat → Tables → Chat Hints Table**
- **Stream → Chat → Tables → Chat Personas Table**

Если слот пустой — соответствующий producer молча не работает.

---

## Тестирование через консоль

Полный список debug-команд: `stream.help`.

Самые полезные:
- `stream.reaction <Tag>` — фигачит реакцию (без префикса `Chat.Event.`)
- `stream.scripted <SeqID>` — запускает сценку
- `stream.burst [N]` — N hype-burst подряд
- `stream.say <user> <msg>` — ручная инжекция строки

---

## Локализация

Все `Message` поля — `FText`. UE автоматически создаёт ключи локализации по
RowName + CSV-пути. Для перевода на другие языки используется стандартный
Localization Dashboard (Window → Localization Dashboard).
