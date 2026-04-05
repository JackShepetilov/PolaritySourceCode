# POLARITY — Demo Dialogue Script

---

## 1. BEACH — Arrival + Tutorial Setup

**Контекст:** Игрок появляется на пляже тропического острова. Дезориентирован. Голос z3r0day звучит по рации.

```
z3r0day: "yo. you good?"
z3r0day: "okay so... slight change of plans"
z3r0day: "i may have not told you the whole thing"
ramless_: "dude. where am i"
z3r0day: "pocket dimension. private islands. rich people stuff"
z3r0day: "look i didn't wanna freak you out before you agreed"
ramless_: "AGREED? you said join a call!"
z3r0day: "technically you did join. just... physically"
ramless_: "..."
z3r0day: "look there's a port building behind you. i stashed something there"
z3r0day: "go through the back entrance. there's a suit"
ramless_: "a suit"
z3r0day: "trust me. you'll need it"
```

**Триггер:** Автоматически после спавна игрока, строки появляются с задержками.

---

## 2. PORT BUILDING — Back Entrance + Suit Discovery

**Контекст:** Игрок находит чёрный вход в служебную зону порта. Тёмные коридоры, кабели, импровизированное оборудование хакера.

```
[Игрок находит чёрный вход]

z3r0day: "through here. watch your head"

[Игрок находит костюм в тайнике]

z3r0day: "put it on"

[Игрок надевает костюм — драматический момент, подсветка, звуковой эффект]

z3r0day: "okay now the real talk"
z3r0day: "this place is guarded by AI. they call them clankers"
z3r0day: "trained on the worst content imaginable"
z3r0day: "they're... not smart. but there's a lot of them"
ramless_: "zero i swear to god"
```

**Триггер:** Proximity trigger у входа + interaction с костюмом.

---

## 3. PORT BUILDING — Movement Tutorial

**Контекст:** Основное здание порта. Светлое, открытое. Тесные коридоры идеальны для обучения wallrun.

```
z3r0day: "just try the wallrun. see that wall?"

[TUTORIAL: wallrun prompt — игрок выполняет wallrun]

z3r0day: "nice. now try the dash"

[TUTORIAL: dash prompt — игрок выполняет dash]

z3r0day: "alright you're not completely hopeless"
ramless_: "why am i doing this again"
z3r0day: "ram. lots of it. more than you've ever seen"
ramless_: "..."
ramless_: "please zero i need this. my pc is kinda ramless"
z3r0day: "that's the spirit"
```

**Триггер:** Каждая строка после успешного выполнения tutorial prompt.

---

## 4. PORT BUILDING — First Combat

**Контекст:** Игрок входит в главный зал порта. Первая встреча с кланкерами.

```
[Кланкеры появляются]

z3r0day: "heads up. clankers"
z3r0day: "use the polarity system. match their charge"

[TUTORIAL: combat + polarity — игрок уничтожает кланкеров]

z3r0day: "see? brainrot machines. their last thoughts are memes"
ramless_: "did that one just say bombardiro crocodilo"
z3r0day: "told you. worst content imaginable"
```

**Триггер:** Spawn trigger при входе в зал + строки после первого убийства.

---

## 5. JUNGLE PATH — Approach to Mansion

**Контекст:** Игрок выходит из порта. Деревянный настил с неоновой подсветкой ведёт через джунгли к мэншну на холме.

```
z3r0day: "mansion's up the hill. that's where the good stuff is"
z3r0day: "first RAM cache should be inside"
ramless_: "it better be worth getting teleported to another dimension"
z3r0day: "oh it is"
```

**Триггер:** Proximity trigger на выходе из порта.

---

## 6. MANSION — Raid + First Upgrade

**Контекст:** Трёхэтажная вилла. Бои на террасах, в интерьерах. RAM-хранилище в подвале.

```
[Игрок входит в мэншн]

z3r0day: "clear the floors. RAM vault should be in the basement"

[Игрок зачищает этажи, находит апгрейд]

z3r0day: "nice. that's a suit upgrade. install it"

[Игрок получает апгрейд]

z3r0day: "feeling it?"
ramless_: "okay this is actually sick"
z3r0day: "told you. now there's a datacenter on the other side of the island"
z3r0day: "that's the real target"
```

**Триггер:** Proximity + interaction triggers при входе и у апгрейда.

---

## 7. DATACENTER — Final Push

**Контекст:** Датацентр — крупное техническое здание. Серверные стойки, неоновое освещение, тяжёлый бой.

```
[Игрок входит в датацентр]

z3r0day: "main hall. destroy the nodes, grab the core RAM unit from the center"
z3r0day: "that thing has enough data to make this whole trip worth it"

[Игрок зачищает зал, подходит к центральному RAM-юниту]

ramless_: "got it. downloading ram dot exe"
z3r0day: "hilarious. sending it to me now"
```

**Триггер:** Proximity trigger при входе + interaction с RAM-юнитом.

---

## 8. ENDING SEQUENCE — Cinematic (макс. 45 секунд)

**Контекст:** Игрок теряет управление. Камера переходит в кинематографический режим. Музыка стихает до эмбиента.

```
[0:00 — Экран слегка дёргается. Игрок теряет управление]

z3r0day: "..."

[0:03 — Пауза. Тишина]

z3r0day: "okay that was fast"

[0:06]

ramless_: "what"

[0:08]

z3r0day: "i'm in. decrypting now"

[0:12 — Пауза 3 секунды. Музыка падает до тихого эмбиента]

z3r0day: "oh no"

[0:16]

ramless_: "what. what is it"

[0:18]

z3r0day: "this isn't just RAM storage"

[0:21]

z3r0day: "these are files. real files. names. dates. everything"

[0:25]

ramless_: "files about what"

[0:27]

z3r0day: "about THEM. the owners. what they've been doing"

[0:31 — Пауза 3 секунды]

z3r0day: "we know too much now"

[0:35]

z3r0day: "both of us"

[0:38]

ramless_: "zero..."

[0:40]

z3r0day: "we're cooked bro"

[0:42 — SMASH CUT TO BLACK]

[0:43 — Title card: POLARITY]

[0:45 — Конец демо]
```

**Режиссёрские заметки:**
- Между "oh no" и "this isn't just RAM storage" — самая длинная пауза. Напряжение строится тишиной
- Голос z3r0day в финале меняет тон — из расслабленного хакера в реально испуганного человека
- "we're cooked bro" — последняя строка, доставлена тихо, без иронии. Brainrot-термин сказанный серьёзно
- SMASH CUT должен быть резким — никаких fade-out. Чёрный экран мгновенно
- Title card появляется через 1 секунду тишины после чёрного экрана

---

## ЗАМЕТКИ ПО РЕАЛИЗАЦИИ

### Система диалогов
- Все строки триггерятся через Level Sequence или Blueprint triggers
- z3r0day — только голос по рации (slightly distorted, comms filter)
- ramless_ — голос от первого лица (чистый, без фильтра)
- Субтитры отображаются в стиле чата (тот же шрифт JetBrains Mono, те же цвета)

### Ключевые моменты для звукового дизайна
- Телепортация на пляж — резкий звуковой удар + тишина + волны
- Надевание костюма — power-up звук, гул энергии
- Первое убийство кланкера — brainrot text pop + vine boom
- "oh no" — музыка полностью стихает за 1 секунду
- SMASH CUT — резкий обрыв всех звуков

### Хронометраж
| Секция | Примерное время |
|--------|----------------|
| Beach arrival | 30-40 сек |
| Port back entrance | 20-30 сек |
| Movement tutorial | 1-2 мин (зависит от игрока) |
| First combat | 1-2 мин |
| Jungle path | 15-20 сек |
| Mansion raid | 5-8 мин |
| Datacenter | 5-8 мин |
| Ending cinematic | 45 сек |
| **TOTAL DEMO** | **~15-20 мин** |
