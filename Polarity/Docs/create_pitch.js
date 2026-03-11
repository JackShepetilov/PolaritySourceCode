const pptxgen = require("pptxgenjs");
const path = require("path");

const pres = new pptxgen();
pres.layout = "LAYOUT_16x9";
pres.author = "Polarity Team";
pres.title = "Polarity — Publisher Pitch";

const BG = "0B0E17";
const BG_ACCENT = "111827";
const CYAN = "00E5FF";
const CYAN_DIM = "0A3D5C";
const WHITE = "F0F0F0";
const MUTED = "6B7280";
const ORANGE = "FF6B35";

function addDecor(slide) {
  slide.addShape(pres.shapes.RECTANGLE, { x: 0, y: 0, w: 10, h: 0.02, fill: { color: CYAN } });
  slide.addShape(pres.shapes.RECTANGLE, { x: 0, y: 5.4, w: 10, h: 0.225, fill: { color: BG_ACCENT } });
  slide.addShape(pres.shapes.LINE, { x: 0.4, y: 0.3, w: 0.4, h: 0, line: { color: CYAN, width: 1 } });
  slide.addShape(pres.shapes.LINE, { x: 0.4, y: 0.3, w: 0, h: 0.3, line: { color: CYAN, width: 1 } });
  slide.addShape(pres.shapes.LINE, { x: 9.2, y: 5.1, w: 0.4, h: 0, line: { color: CYAN_DIM, width: 1 } });
  slide.addShape(pres.shapes.LINE, { x: 9.6, y: 4.8, w: 0, h: 0.3, line: { color: CYAN_DIM, width: 1 } });
}

function addHeader(slide, number, title) {
  slide.addText(number, {
    x: 0.5, y: 0.25, w: 1, h: 0.5,
    fontSize: 28, fontFace: "Arial Black", color: CYAN_DIM, margin: 0,
  });
  slide.addText(title, {
    x: 0.5, y: 0.6, w: 9, h: 0.5,
    fontSize: 22, fontFace: "Arial Black", color: WHITE, charSpacing: 2, margin: 0,
  });
  slide.addShape(pres.shapes.LINE, {
    x: 0.5, y: 1.15, w: 0.8, h: 0, line: { color: CYAN, width: 1.5 },
  });
}

function bullets(items, opts = {}) {
  return items.map((t, i) => ({
    text: t,
    options: {
      bullet: true, breakLine: i < items.length - 1,
      fontSize: opts.fontSize || 12, fontFace: "Arial", color: opts.color || WHITE,
    },
  }));
}

// ─── SLIDE 1: TITLE ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  s.addText("POLARITY", {
    x: 0.5, y: 1.4, w: 9, h: 1,
    fontSize: 42, fontFace: "Arial Black", color: WHITE, align: "center", charSpacing: 8, margin: 0,
  });
  s.addShape(pres.shapes.LINE, { x: 3.8, y: 2.5, w: 2.4, h: 0, line: { color: CYAN, width: 1.5 } });
  s.addText("PUBLISHER PITCH", {
    x: 0.5, y: 2.65, w: 9, h: 0.5,
    fontSize: 12, fontFace: "Arial", color: MUTED, align: "center", charSpacing: 6, margin: 0,
  });
  s.addText("FPS / Arena Shooter / EMF Physics", {
    x: 0.5, y: 3.3, w: 9, h: 0.4,
    fontSize: 11, fontFace: "Arial", color: CYAN_DIM, align: "center", margin: 0,
  });
  s.addText("Unreal Engine 5", {
    x: 0.5, y: 3.7, w: 9, h: 0.4,
    fontSize: 10, fontFace: "Arial", color: MUTED, align: "center", margin: 0,
  });
}

// ─── SLIDE 2: КОНЦЕПТ ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "01", "КОНЦЕПТ");

  s.addText("Elevator Pitch", {
    x: 0.5, y: 1.4, w: 4.3, h: 0.35,
    fontSize: 13, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
  });
  s.addText(
    "Аренашутер от первого лица, где каждый объект и враг подчиняется EMF-физике. " +
    "Игрок управляет электромагнитной полярностью — притягивает, отталкивает, захватывает " +
    "и запускает врагов и предметы как снаряды. Скорость, импульс и креативность — основа боя.",
    {
      x: 0.5, y: 1.8, w: 4.3, h: 1.6,
      fontSize: 11, fontFace: "Arial", color: WHITE, align: "left", valign: "top", margin: 0,
      lineSpacingMultiple: 1.3,
    }
  );

  s.addText("Ключевые столпы", {
    x: 5.3, y: 1.4, w: 4.2, h: 0.35,
    fontSize: 13, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
  });
  s.addText(bullets([
    "Физика полярности как ядро геймплея",
    "Скорость и импульс вознаграждаются",
    "Враги = оружие (захват и запуск)",
    "Ресурсные петли: агрессия → награда",
    "Doom Eternal DNA + уникальная механика EMF",
  ]), {
    x: 5.3, y: 1.85, w: 4.2, h: 2.5,
    valign: "top", margin: 0, lineSpacingMultiple: 1.5,
  });

  s.addText("Жанр: FPS Arena Shooter  |  Движок: UE5  |  Камера: от первого лица  |  Режим: Singleplayer", {
    x: 0.5, y: 4.6, w: 9, h: 0.35,
    fontSize: 9, fontFace: "Arial", color: MUTED, margin: 0,
  });
}

// ─── SLIDE 3: ГЕЙМПЛЕЙ (sequential list) ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "02", "ГЕЙМПЛЕЙ");

  const items = [
    "у игрока есть электромагнитная полярность — положительная или отрицательная",
    "полярность переключается одной кнопкой на лету",
    "все объекты и враги на уровне имеют заряд — одноимённые отталкиваются, разноимённые притягиваются",
    "удержание кнопки полярности создаёт пластину — она захватывает врагов и предметы в поле",
    "захваченных врагов и пропы можно запускать как снаряды — реверс полярности отправляет их в полёт",
    "пропы взрываются при столкновении с NPC, оглушая окружающих и спавня пикапы HP",
    "оглушённых врагов можно добить мечом с AoE-уроном за время стана",
    "убийства от пропов и дронов дропают хилки, убийства через захват дропают броню — агрессия = выживание",
    "движение: двойной прыжок, скольжение, стенобег, воздушный рывок, rocket boost зарядомётом",
    "импульс от движения конвертируется в урон ближнего боя и дропкика",
    "оружие: хитскан-винтовка (ионизирует цели), зарядомёт (EMF-снаряды), подбираемый меч",
    "4 типа врагов: стрелок, мили, летающий дрон, снайперская турель",
    "апгрейды расширяют боевой словарь: 360 Shot, Charge Flip, Testosterone Boost, Suppression Fire",
    "Hard-core post-breakbeat music",
  ];

  s.addText(items.map((t, i) => ({
    text: t,
    options: {
      bullet: true,
      breakLine: i < items.length - 1,
      fontSize: 10,
      fontFace: "Arial",
      color: WHITE,
    },
  })), {
    x: 0.5, y: 1.3, w: 9, h: 4.0,
    valign: "top", margin: 0, lineSpacingMultiple: 1.15,
  });
}

// ─── SLIDE 4: УНИКАЛЬНЫЕ МЕХАНИКИ ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "03", "УНИКАЛЬНЫЕ МЕХАНИКИ");

  const loops = [
    { name: "EMF Физика", desc: "Каждый объект имеет заряд. Одноимённые отталкиваются, разноимённые притягиваются. Игрок переключает свою полярность на лету." },
    { name: "Враги = Снаряды", desc: "Захват NPC через канализацию → запуск в стену или других врагов. Wallslam урон, цепные столкновения." },
    { name: "Пропы → Взрыв → HP", desc: "Физ. пропы взрываются при столкновении с NPC, оглушая окружающих на 2с и спавня пикапы HP." },
    { name: "Ресурсные петли", desc: "Пропы/дроны → хилки. Захват/бросок → броня. Агрессия напрямую конвертируется в выживание." },
    { name: "Апгрейды", desc: "Скилл-апгрейды расширяют словарь боя: 360 Shot, Charge Flip, Suppression Fire. Риск → награда." },
  ];

  loops.forEach((l, i) => {
    const y = 1.4 + i * 0.72;
    s.addText(l.name, {
      x: 0.5, y, w: 2.5, h: 0.35,
      fontSize: 11, fontFace: "Arial", color: CYAN, bold: true, margin: 0, valign: "top",
    });
    s.addText(l.desc, {
      x: 3.1, y, w: 6.4, h: 0.6,
      fontSize: 10, fontFace: "Arial", color: WHITE, margin: 0, valign: "top",
      lineSpacingMultiple: 1.2,
    });
  });
}

// ─── SLIDE 5: ЦЕЛЕВАЯ АУДИТОРИЯ ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "04", "ЦЕЛЕВАЯ АУДИТОРИЯ");

  s.addText("Ядро", {
    x: 0.5, y: 1.4, w: 4.3, h: 0.3,
    fontSize: 12, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
  });
  s.addText(bullets([
    "Фанаты DOOM Eternal, ULTRAKILL, Titanfall 2",
    "Хардкорные FPS-игроки, ценящие скилл-потолок",
    "Любители спидрана и стайл-рейтинга",
    "Аудитория, уставшая от мультиплеерных шутеров",
  ], { fontSize: 11 }), {
    x: 0.5, y: 1.8, w: 4.3, h: 2.0, valign: "top", margin: 0, lineSpacingMultiple: 1.5,
  });

  s.addText("Расширенная", {
    x: 5.3, y: 1.4, w: 4.2, h: 0.3,
    fontSize: 12, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
  });
  s.addText(bullets([
    "Игроки в иммерсив-симы (физика = инструмент)",
    "Зрители на Twitch/YouTube (зрелищный геймплей)",
    "Ретро-шутер комьюнити (boomer shooter fans)",
  ], { fontSize: 11 }), {
    x: 5.3, y: 1.8, w: 4.2, h: 2.0, valign: "top", margin: 0, lineSpacingMultiple: 1.5,
  });

  s.addText("Возраст: 18-35  |  Платформы: PC (Steam)  |  Рейтинг: PEGI 16+", {
    x: 0.5, y: 4.6, w: 9, h: 0.35,
    fontSize: 9, fontFace: "Arial", color: MUTED, margin: 0,
  });
}

// ─── SLIDE 6: КОНКУРЕНТЫ ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "05", "КОНКУРЕНТЫ");

  const hdr = ["Игра", "Общее", "Отличие Polarity"];
  const rows = [
    ["DOOM Eternal", "Ресурсные петли, агрессия", "EMF-физика, враги как снаряды"],
    ["ULTRAKILL", "Стиль, скорость, комбо", "Захват и манипуляция объектами"],
    ["Titanfall 2", "Движение, импульс", "Полярность, интерактивная физика"],
    ["Control", "Телекинез, физика", "Скорость, аренашутер, заряды"],
    ["Mag Runner", "Магнитная механика", "FPS бой, не платформер"],
  ];

  const colW = [1.8, 3.0, 4.2];
  const tableRows = [
    hdr.map(h => ({ text: h, options: { bold: true, fontSize: 10, color: "0B0E17", fill: { color: CYAN } } })),
    ...rows.map(r => r.map(c => ({ text: c, options: { fontSize: 9, color: WHITE, fill: { color: BG_ACCENT } } }))),
  ];
  s.addTable(tableRows, {
    x: 0.5, y: 1.5, w: 9, colW,
    border: { pt: 0.5, color: CYAN_DIM },
    rowH: [0.35, 0.35, 0.35, 0.35, 0.35, 0.35],
  });
}

// ─── SLIDE 7: БИЗНЕС-МОДЕЛЬ ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "06", "БИЗНЕС-МОДЕЛЬ");

  s.addText("Монетизация", {
    x: 0.5, y: 1.4, w: 4.3, h: 0.3,
    fontSize: 12, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
  });
  s.addText(bullets([
    "Premium: разовая покупка",
    "Целевая цена: $XX",
    "Без микротранзакций",
    "Потенциал DLC (новые арены, враги, апгрейды)",
  ], { fontSize: 11 }), {
    x: 0.5, y: 1.8, w: 4.3, h: 2.0, valign: "top", margin: 0, lineSpacingMultiple: 1.5,
  });

  s.addText("Платформы и релиз", {
    x: 5.3, y: 1.4, w: 4.2, h: 0.3,
    fontSize: 12, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
  });
  s.addText(bullets([
    "PC (Steam) — основная платформа",
    "Early Access → Full Release",
    "Консоли — после релиза на PC",
    "Демо на Steam Next Fest",
  ], { fontSize: 11 }), {
    x: 5.3, y: 1.8, w: 4.2, h: 2.0, valign: "top", margin: 0, lineSpacingMultiple: 1.5,
  });

  s.addText("[Заполнить: бюджет, запрашиваемое финансирование, прогноз продаж]", {
    x: 0.5, y: 4.2, w: 9, h: 0.35,
    fontSize: 9, fontFace: "Arial", color: ORANGE, italic: true, margin: 0,
  });
}

// ─── SLIDE 8: КОМАНДА ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "07", "КОМАНДА");

  s.addText("[Заполнить: участники команды, роли, опыт]", {
    x: 0.5, y: 1.5, w: 9, h: 0.4,
    fontSize: 10, fontFace: "Arial", color: ORANGE, italic: true, margin: 0,
  });

  const members = [
    { role: "Game Designer / Programmer", desc: "Геймплей, AI, боевые системы, EMF-физика" },
    { role: "[Role 2]", desc: "[Описание опыта и обязанностей]" },
    { role: "[Role 3]", desc: "[Описание опыта и обязанностей]" },
  ];
  members.forEach((m, i) => {
    const y = 2.2 + i * 0.8;
    s.addShape(pres.shapes.RECTANGLE, { x: 0.5, y, w: 9, h: 0.65, fill: { color: BG_ACCENT } });
    s.addText(m.role, {
      x: 0.7, y: y + 0.05, w: 3, h: 0.25,
      fontSize: 11, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
    });
    s.addText(m.desc, {
      x: 0.7, y: y + 0.32, w: 8.5, h: 0.25,
      fontSize: 10, fontFace: "Arial", color: WHITE, margin: 0,
    });
  });
}

// ─── SLIDE 9: РОАДМАП ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);
  addHeader(s, "08", "РОАДМАП");

  const phases = [
    { phase: "Pre-Alpha", period: "[Q? 20XX]", items: "Ядро геймплея, движение, EMF-физика, 4 типа врагов, AI координатор" },
    { phase: "Alpha", period: "[Q? 20XX]", items: "Система апгрейдов, левел-дизайн, баланс, UI/HUD" },
    { phase: "Beta", period: "[Q? 20XX]", items: "Полировка, QA, оптимизация, Steam страница, трейлер" },
    { phase: "Early Access", period: "[Q? 20XX]", items: "Steam Early Access, обратная связь, итерации контента" },
    { phase: "Full Release", period: "[Q? 20XX]", items: "Финальный контент, консоли, маркетинг" },
  ];

  s.addShape(pres.shapes.LINE, {
    x: 1.5, y: 1.6, w: 0, h: 3.2, line: { color: CYAN_DIM, width: 1.5 },
  });

  phases.forEach((p, i) => {
    const y = 1.5 + i * 0.65;
    s.addShape(pres.shapes.OVAL, { x: 1.38, y: y + 0.08, w: 0.24, h: 0.24, fill: { color: CYAN } });
    s.addText(p.phase, {
      x: 2.0, y, w: 1.8, h: 0.35,
      fontSize: 11, fontFace: "Arial", color: CYAN, bold: true, margin: 0,
    });
    s.addText(p.period, {
      x: 3.8, y, w: 1.2, h: 0.35,
      fontSize: 9, fontFace: "Arial", color: MUTED, margin: 0,
    });
    s.addText(p.items, {
      x: 5.0, y, w: 4.5, h: 0.55,
      fontSize: 9, fontFace: "Arial", color: WHITE, margin: 0, valign: "top",
      lineSpacingMultiple: 1.2,
    });
  });
}

// ─── SLIDE 10: КОНТАКТЫ ───
{
  const s = pres.addSlide();
  s.background = { color: BG };
  addDecor(s);

  s.addText("СПАСИБО", {
    x: 0.5, y: 1.5, w: 9, h: 0.8,
    fontSize: 36, fontFace: "Arial Black", color: WHITE, align: "center", charSpacing: 6, margin: 0,
  });
  s.addShape(pres.shapes.LINE, { x: 4, y: 2.4, w: 2, h: 0, line: { color: CYAN, width: 1.5 } });

  s.addText("[Имя / Студия]", {
    x: 0.5, y: 2.8, w: 9, h: 0.4,
    fontSize: 14, fontFace: "Arial", color: WHITE, align: "center", margin: 0,
  });

  const contacts = [
    "[email@example.com]",
    "[twitter.com/handle]",
    "[store.steampowered.com/app/XXXXX]",
  ];
  s.addText(contacts.map((c, i) => ({
    text: c,
    options: { breakLine: i < contacts.length - 1, fontSize: 11, fontFace: "Arial", color: MUTED },
  })), {
    x: 0.5, y: 3.4, w: 9, h: 1.2, align: "center", margin: 0, lineSpacingMultiple: 1.6,
  });
}

const outPath = path.join(__dirname, "PitchTemplate.pptx");
pres.writeFile({ fileName: outPath }).then(() => {
  console.log("Created: " + outPath);
});
