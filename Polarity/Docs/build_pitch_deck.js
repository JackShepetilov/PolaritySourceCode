// Polarity — Pitch Deck generator (iterative). Run: node build_pitch_deck.js
// Output: Polarity_Pitch_v2.pptx (does NOT touch the old Polarity_Pitch.pptx)
let pptxgen;
try { pptxgen = require("pptxgenjs"); }
catch (e) { pptxgen = require("C:/Users/Professional/AppData/Roaming/npm/node_modules/pptxgenjs"); }

const OUT = "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_6/Source/Polarity/Docs/Polarity_Pitch_v2.pptx";

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE"; // 13.33 x 7.5 in
pres.author = "Polarity";
pres.title = "Polarity — Pitch Deck";

// ---- Palette (dark sci-fi, EMF blue + explosion orange) ----
const BG = "0A0E16";   // deep midnight
const CARD = "121A2B";   // card fill
const SLOT = "0F1626";   // media slot fill
const BLUE = "35C8FF";   // electro-blue (EMF)
const ORANGE = "FF8A2B";   // explosion/wallslam orange
const TEXT = "EAF1FB";   // off-white body
const MUTE = "8A97AD";   // muted slate
const WHITE = "FFFFFF";

const HEAD = "Times New Roman";
const BODY = "Times New Roman";

const bg = (s) => { s.background = { color: BG }; };

function wordmark(s) {
  s.addText("POLARITY", { x: 0.6, y: 7.02, w: 4, h: 0.32, fontFace: HEAD, fontSize: 10, color: MUTE, charSpacing: 3, align: "left", valign: "middle", margin: 0 });
}

function sectionHeader(s, title) {
  // motif: small charged square (no slide numbers)
  s.addShape(pres.shapes.RECTANGLE, { x: 0.6, y: 0.77, w: 0.16, h: 0.16, fill: { color: BLUE } });
  s.addText(title, { x: 0.9, y: 0.5, w: 11.8, h: 0.7, fontFace: HEAD, fontSize: 31, bold: true, color: WHITE, charSpacing: 2, align: "left", valign: "middle", margin: 0 });
}

function gifSlot(s, x, y, w, h, caption, label) {
  s.addShape(pres.shapes.RECTANGLE, { x, y, w, h, fill: { color: SLOT }, line: { color: BLUE, width: 1.5, dashType: "dash" } });
  s.addText("▶", { x, y: y + h * 0.26, w, h: 0.9, fontFace: BODY, fontSize: 44, color: BLUE, align: "center", valign: "middle", margin: 0 });
  s.addText(label || "[ МЕСТО ПОД ГИФКУ ]", { x, y: y + h * 0.26 + 0.84, w, h: 0.4, fontFace: HEAD, fontSize: 15, color: TEXT, charSpacing: 2, align: "center", valign: "middle", margin: 0 });
  if (caption) {
    s.addText(caption, { x: x + 0.3, y: y + h - 0.72, w: w - 0.6, h: 0.5, fontFace: BODY, fontSize: 12.5, italic: true, color: MUTE, align: "center", valign: "middle", margin: 0 });
  }
}

// =========================================================
// SLIDE 1 — Title + Hook
// =========================================================
const s1 = pres.addSlide(); bg(s1);
// motif: small charged squares above the title
s1.addShape(pres.shapes.RECTANGLE, { x: 0.72, y: 1.18, w: 0.13, h: 0.13, fill: { color: BLUE } });
s1.addShape(pres.shapes.RECTANGLE, { x: 0.93, y: 1.18, w: 0.13, h: 0.13, fill: { color: BLUE } });
s1.addShape(pres.shapes.RECTANGLE, { x: 1.14, y: 1.18, w: 0.13, h: 0.13, fill: { color: ORANGE } });
s1.addText("POLARITY", { x: 0.68, y: 1.55, w: 7.7, h: 1.5, fontFace: HEAD, fontSize: 78, bold: true, color: WHITE, charSpacing: 5, align: "left", valign: "middle", margin: 0 });
s1.addText("Тугой roguelite-шутер, где твой ствол не убивает — он заряжает всё вокруг, превращая врагов и предметы в твой арсенал.",
  { x: 0.74, y: 3.25, w: 6.9, h: 1.9, fontFace: BODY, fontSize: 21, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.18, margin: 0 });
s1.addText("[ Студия ]   ·   email@example.com   ·   store.steampowered.com/app/...",
  { x: 0.74, y: 6.6, w: 8.5, h: 0.4, fontFace: BODY, fontSize: 13, color: MUTE, align: "left", valign: "middle", margin: 0 });
gifSlot(s1, 8.45, 1.45, 4.28, 4.75, "ключевой кадр / геймплей-гифка");

// =========================================================
// SLIDE 2 — About the game
// =========================================================
const s2 = pres.addSlide(); bg(s2); sectionHeader(s2, "ОБ ИГРЕ"); wordmark(s2);
gifSlot(s2, 0.6, 1.72, 5.35, 4.9, "физика = оружие");
const aboutRows = [
  ["Что это", "Высокоскоростной roguelite-шутер от первого лица."],
  ["Сеттинг", "Современность + «фантомное измерение» — цифровое пространство, где техно-миллиардеры прячут свои секреты."],
  ["Завязка", "Друг-стример втягивает тебя в рейд: в прямом эфире, под его комментарии, ты разносишь приватный мир крипто-миллиардера."],
  ["Основной прикол", "EMF-физика — оружие не наносит урон: ты убиваешь, превращая врагов и предметы в снаряды."],
];
let ay = 1.82;
const aRowH = 1.2, aGap = 0.07;
aboutRows.forEach(([label, body]) => {
  s2.addShape(pres.shapes.RECTANGLE, { x: 6.4, y: ay, w: 0.06, h: aRowH, fill: { color: BLUE } });
  s2.addText(label.toUpperCase(), { x: 6.62, y: ay, w: 6.1, h: 0.36, fontFace: HEAD, fontSize: 14, bold: true, color: BLUE, charSpacing: 1.5, align: "left", valign: "middle", margin: 0 });
  s2.addText(body, { x: 6.62, y: ay + 0.36, w: 6.25, h: aRowH - 0.36, fontFace: BODY, fontSize: 14.5, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.04, margin: 0 });
  ay += aRowH + aGap;
});

// =========================================================
// SLIDE 3 — USP
// =========================================================
const s3 = pres.addSlide(); bg(s3); sectionHeader(s3, "ЧЕМ МЫ УНИКАЛЬНЫ"); wordmark(s3);
const usp = [
  ["1", "Оружие, которое не убивает", "Стартовый ствол лишь электризует — урон рождается из физики, а не из пушки."],
  ["2", "Враги и предметы — твой арсенал", "Захватывай, заряжай и швыряй кого угодно во что угодно; заряженное взрывается."],
  ["3", "Троица билдов вместо generic-стихий", "Электро-телекинез / рукопашная / краденое оружие — два забега не похожи."],
  ["4", "Никакого «одного ствола на ран»", "Забирай оружие врагов; ствол служит, пока в магазине есть патроны."],
  ["5", "Стрим как мета-слой", "Донаты зрителей становятся мета-валютой между ранами."],
];
const cardX = 0.6, cardW = 12.13, cardH = 0.9, startY = 1.7, gapY = 0.14;
usp.forEach((u, i) => {
  const y = startY + i * (cardH + gapY);
  s3.addShape(pres.shapes.RECTANGLE, { x: cardX, y, w: cardW, h: cardH, fill: { color: CARD } });
  s3.addShape(pres.shapes.RECTANGLE, { x: cardX, y, w: 0.07, h: cardH, fill: { color: BLUE } });
  s3.addText(u[0], { x: cardX + 0.3, y, w: 0.7, h: cardH, fontFace: HEAD, fontSize: 30, bold: true, color: BLUE, align: "left", valign: "middle", margin: 0 });
  s3.addText(u[1], { x: cardX + 1.1, y, w: 4.5, h: cardH, fontFace: HEAD, fontSize: 17, bold: true, color: WHITE, align: "left", valign: "middle", lineSpacingMultiple: 0.98, margin: 0 });
  s3.addText(u[2], { x: cardX + 5.85, y, w: 6.0, h: cardH, fontFace: BODY, fontSize: 13, color: MUTE, align: "left", valign: "middle", lineSpacingMultiple: 1.03, margin: 0 });
});

// =========================================================
// SLIDE 4 — Gameplay in motion ("show me the game")
// =========================================================
const s4 = pres.addSlide(); bg(s4); sectionHeader(s4, "КАК ЭТО ИГРАЕТСЯ"); wordmark(s4);
const loops = [
  ["Враги = снаряды", "Электризуй, хватай и швыряй врагов друг в друга — стена и толпа добивают за тебя."],
  ["Декор = оружие", "Заряженный проп взрывается: стан по площади, хил и окно на добивание мечом."],
  ["Скорость = поток", "Бег по стене, слайд, рывок, rocket-boost — бой и движение не останавливаются."],
];
const s4x = [0.6, 4.75, 8.9];
const s4slotW = 3.8, s4slotH = 3.0, s4slotY = 1.7;
loops.forEach(([title, desc], i) => {
  const x = s4x[i];
  s4.addShape(pres.shapes.RECTANGLE, { x, y: s4slotY, w: s4slotW, h: s4slotH, fill: { color: SLOT }, line: { color: BLUE, width: 1.5, dashType: "dash" } });
  s4.addText("▶", { x, y: s4slotY + s4slotH / 2 - 0.6, w: s4slotW, h: 0.9, fontFace: BODY, fontSize: 38, color: BLUE, align: "center", valign: "middle", margin: 0 });
  s4.addText("[ ГИФ ]", { x, y: s4slotY + s4slotH / 2 + 0.28, w: s4slotW, h: 0.35, fontFace: HEAD, fontSize: 13, color: MUTE, charSpacing: 2, align: "center", valign: "middle", margin: 0 });
  s4.addText(title, { x, y: s4slotY + s4slotH + 0.2, w: s4slotW, h: 0.45, fontFace: HEAD, fontSize: 19, bold: true, color: WHITE, align: "left", valign: "top", margin: 0 });
  s4.addText(desc, { x, y: s4slotY + s4slotH + 0.72, w: s4slotW, h: 1.15, fontFace: BODY, fontSize: 13, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });
});

// =========================================================
// SLIDE 5 — Why now / single comp focus (Void Breaker)
// =========================================================
const s5 = pres.addSlide(); bg(s5); sectionHeader(s5, "ОРИЕНТИР: VOID BREAKER"); wordmark(s5);
s5.addText("Соло-разработчик уже доказал спрос на нашу нишу. Мы берём его ядро — и закрываем три слабых места.",
  { x: 0.6, y: 1.42, w: 12.1, h: 0.55, fontFace: BODY, fontSize: 15, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });
// Left — Void Breaker proof panel
s5.addShape(pres.shapes.RECTANGLE, { x: 0.6, y: 2.1, w: 4.95, h: 3.7, fill: { color: CARD } });
s5.addShape(pres.shapes.RECTANGLE, { x: 0.6, y: 2.1, w: 0.07, h: 3.7, fill: { color: BLUE } });
// logo placeholder + wordmark lockup
s5.addShape(pres.shapes.RECTANGLE, { x: 0.92, y: 2.3, w: 1.05, h: 1.05, fill: { color: SLOT }, line: { color: BLUE, width: 1.25, dashType: "dash" } });
s5.addText("ЛОГО", { x: 0.92, y: 2.3, w: 1.05, h: 1.05, fontFace: HEAD, fontSize: 11, color: MUTE, charSpacing: 1, align: "center", valign: "middle", margin: 0 });
s5.addText("VOID BREAKER", { x: 2.15, y: 2.3, w: 3.25, h: 1.05, fontFace: HEAD, fontSize: 19, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s5.addText("Соло-разработчик. Рогалик-FPS: электризуй, захватывай и швыряй объекты во врагов.", { x: 0.92, y: 3.5, w: 4.45, h: 0.7, fontFace: BODY, fontSize: 13, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.03, margin: 0 });
s5.addText("~2–3k*", { x: 0.92, y: 4.22, w: 4.45, h: 0.62, fontFace: HEAD, fontSize: 36, bold: true, color: BLUE, align: "left", valign: "middle", margin: 0 });
s5.addText("отзывов, Very Positive — спрос в нише доказан одним человеком.", { x: 0.92, y: 4.9, w: 4.45, h: 0.85, fontFace: BODY, fontSize: 12, color: MUTE, align: "left", valign: "top", lineSpacingMultiple: 1.03, margin: 0 });
// Right — deltas
s5.addText("…И ГДЕ МЫ ИДЁМ ДАЛЬШЕ", { x: 5.85, y: 2.05, w: 6.85, h: 0.38, fontFace: HEAD, fontSize: 15, bold: true, color: BLUE, charSpacing: 1.5, align: "left", valign: "middle", margin: 0 });
const deltas = [
  ["1. Зло с лицом", "Не generic-ИИ, а приватное измерение крипто-миллиардера + друг-стример."],
  ["2. Глубина вместо стихий", "Троица билдов и краденое оружие вместо «одного ствола на ран»."],
  ["3. Боссы не ломают луп", "Босс остаётся в той же боёвке, а не превращается в отдельный мини-режим."],
  ["4. Стрим как механика", "Режим стримера, где чат влияет на геймплей — встроенный маркетинг-движок, которого у Void Breaker нет."],
];
let dy = 2.5;
deltas.forEach(([t, d]) => {
  s5.addText(t, { x: 5.85, y: dy, w: 6.85, h: 0.34, fontFace: HEAD, fontSize: 16, bold: true, color: WHITE, align: "left", valign: "middle", margin: 0 });
  s5.addText(d, { x: 5.85, y: dy + 0.34, w: 6.85, h: 0.46, fontFace: BODY, fontSize: 12, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.02, margin: 0 });
  dy += 0.83;
});
s5.addText("* точные цифры — сверим по SteamDB / Gamalytic (сессия по экономике).",
  { x: 0.6, y: 5.95, w: 12.1, h: 0.3, fontFace: BODY, fontSize: 10, italic: true, color: MUTE, align: "left", valign: "middle", margin: 0 });
s5.addText("Аудитория: хардкор-FPS и спидранеры · фанаты физических песочниц (Half-Life++) · зумеры (edgy, eat-the-rich, стрим-культура).",
  { x: 0.6, y: 6.4, w: 12.1, h: 0.5, fontFace: BODY, fontSize: 13, color: TEXT, align: "left", valign: "middle", margin: 0 });

// =========================================================
// SLIDE 6 — Narrative (brief)
// =========================================================
const s6 = pres.addSlide(); bg(s6); sectionHeader(s6, "НАРРАТИВ"); wordmark(s6);
gifSlot(s6, 0.6, 1.7, 5.0, 4.35, "ключевой арт / атмосфера измерения", "[ КЛЮЧЕВОЙ АРТ ]");
s6.addText("Ты — обычный геймер. Друг-стример (архетип MrBeast) втягивает тебя в «фантомное измерение» — рейд на приватный мир крипто-миллиардера. В прямом эфире, под его комментарии, ты разносишь империю олигарха и тащишь компромат. Чем дальше — тем крупнее заговор.",
  { x: 6.0, y: 1.75, w: 6.7, h: 1.7, fontFace: BODY, fontSize: 15, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.15, margin: 0 });
s6.addText("ТОН", { x: 6.0, y: 3.6, w: 6.7, h: 0.34, fontFace: HEAD, fontSize: 14, bold: true, color: BLUE, charSpacing: 1.5, align: "left", valign: "middle", margin: 0 });
s6.addText("power-fantasy · чёрный юмор · eat-the-rich · стрим-культура · без моральной двусмысленности.",
  { x: 6.0, y: 3.95, w: 6.7, h: 0.6, fontFace: BODY, fontSize: 13, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.1, margin: 0 });
s6.addText("«we're cooked, chat»", { x: 6.0, y: 4.85, w: 6.7, h: 0.75, fontFace: HEAD, fontSize: 30, bold: true, italic: true, color: BLUE, align: "left", valign: "middle", margin: 0 });
s6.addText("— реакция друга, когда он расшифровывает первый компромат.", { x: 6.0, y: 5.6, w: 6.7, h: 0.4, fontFace: BODY, fontSize: 12, italic: true, color: MUTE, align: "left", valign: "top", margin: 0 });

// =========================================================
// SLIDE 7 — Stream: two modes + marketing engine
// =========================================================
const s7 = pres.addSlide(); bg(s7); sectionHeader(s7, "СТРИМ: ДВА РЕЖИМА"); wordmark(s7);
s7.addText("Игра про стримера — и сделанная для стриминга. Соло-игра полноценна сама по себе; режим стримера — мультипликатор охвата.",
  { x: 0.6, y: 1.45, w: 12.1, h: 0.6, fontFace: BODY, fontSize: 15, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });
const modeCardY = 2.3, modeCardH = 2.85;
// SOLO mode
s7.addShape(pres.shapes.RECTANGLE, { x: 0.6, y: modeCardY, w: 5.95, h: modeCardH, fill: { color: CARD } });
s7.addShape(pres.shapes.RECTANGLE, { x: 0.6, y: modeCardY, w: 0.07, h: modeCardH, fill: { color: BLUE } });
s7.addText("СОЛО-РЕЖИМ", { x: 0.92, y: modeCardY + 0.22, w: 5.4, h: 0.45, fontFace: HEAD, fontSize: 20, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s7.addText("Ты — геймер на стриме. Донаты зрителей (в фикшене) превращаются в мета-валюту между ранами. Полноценная одиночная игра — фундамент проекта.",
  { x: 0.92, y: modeCardY + 0.78, w: 5.4, h: 1.8, fontFace: BODY, fontSize: 13.5, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.1, margin: 0 });
// STREAMER mode
s7.addShape(pres.shapes.RECTANGLE, { x: 6.78, y: modeCardY, w: 5.95, h: modeCardH, fill: { color: CARD } });
s7.addShape(pres.shapes.RECTANGLE, { x: 6.78, y: modeCardY, w: 0.07, h: modeCardH, fill: { color: ORANGE } });
s7.addText("РЕЖИМ СТРИМЕРА", { x: 7.1, y: modeCardY + 0.22, w: 5.4, h: 0.45, fontFace: HEAD, fontSize: 20, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s7.addText("Реальный Twitch-чат влияет на геймплей вживую: зрители участвуют в забеге, а не просто смотрят. Опциональная надстройка над соло-игрой.",
  { x: 7.1, y: modeCardY + 0.78, w: 5.4, h: 1.8, fontFace: BODY, fontSize: 13.5, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.1, margin: 0 });
// Why-publisher strip
s7.addShape(pres.shapes.RECTANGLE, { x: 0.6, y: 5.5, w: 12.13, h: 1.05, fill: { color: CARD } });
s7.addShape(pres.shapes.RECTANGLE, { x: 0.6, y: 5.5, w: 0.07, h: 1.05, fill: { color: BLUE } });
s7.addText("ЗАЧЕМ ИЗДАТЕЛЮ", { x: 0.92, y: 5.62, w: 11.6, h: 0.32, fontFace: HEAD, fontSize: 14, bold: true, color: BLUE, charSpacing: 1.5, align: "left", valign: "middle", margin: 0 });
s7.addText("Игра про стрим → стримеры и их аудитории становятся органической воронкой. Клипабельные EMF-комбо, чат как ретеншн-механика. Фикшн становится фичей — встроенный маркетинг.",
  { x: 0.92, y: 5.96, w: 11.7, h: 0.5, fontFace: BODY, fontSize: 13, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });

// ---- helpers for cards / bullets ----
function card(s, x, y, w, h, edge) {
  s.addShape(pres.shapes.RECTANGLE, { x, y, w, h, fill: { color: CARD } });
  s.addShape(pres.shapes.RECTANGLE, { x, y, w: 0.07, h, fill: { color: edge || BLUE } });
}
function bulletList(items) {
  return items.map((t) => ({ text: t, options: { bullet: true, breakLine: true } }));
}

// =========================================================
// SLIDE 8 — Team
// =========================================================
const s8 = pres.addSlide(); bg(s8); sectionHeader(s8, "КОМАНДА"); wordmark(s8);
s8.addText("Соло-разработчик собрал играбельное ядро. Бюджет — на то, что осознанно оставлено заглушками.",
  { x: 0.6, y: 1.45, w: 12.1, h: 0.55, fontFace: BODY, fontSize: 15, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });
card(s8, 0.6, 2.25, 5.95, 3.0, BLUE);
s8.addText("СДЕЛАНО В ОДИНОЧКУ", { x: 0.92, y: 2.45, w: 5.3, h: 0.4, fontFace: HEAD, fontSize: 17, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s8.addText(bulletList(["Боевая система и 6 петель", "AI-координатор: токены, боевой круг, роли", "Электро-физика и взаимодействия", "Прогрессия: 4 независимых скилла", "5 типов врагов + босс"]),
  { x: 0.95, y: 2.95, w: 5.3, h: 2.15, fontFace: BODY, fontSize: 13, color: TEXT, valign: "top", paraSpaceAfter: 8, margin: 0 });
card(s8, 6.78, 2.25, 5.95, 3.0, ORANGE);
s8.addText("НАНИМАЕМ / АУТСОРС", { x: 7.1, y: 2.45, w: 5.3, h: 0.4, fontFace: HEAD, fontSize: 17, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s8.addText(bulletList(["Аниматор", "Артисты окружения и левелдизайн", "2× актёра озвучки", "Композитор + лицензии на музыку"]),
  { x: 7.13, y: 2.95, w: 5.3, h: 2.15, fontFace: BODY, fontSize: 13, color: TEXT, valign: "top", paraSpaceAfter: 10, margin: 0 });
card(s8, 0.6, 5.45, 12.13, 1.0, BLUE);
s8.addText("РОЛЬ АВТОРА", { x: 0.92, y: 5.57, w: 11.6, h: 0.32, fontFace: HEAD, fontSize: 14, bold: true, color: BLUE, charSpacing: 1.5, align: "left", valign: "middle", margin: 0 });
s8.addText("Геймдизайн + технический директор. Заглушки в арте и анимациях оставлены намеренно — это rabbit-hole-зоны, а не «не успел».",
  { x: 0.92, y: 5.91, w: 11.7, h: 0.5, fontFace: BODY, fontSize: 13, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });

// =========================================================
// SLIDE 9 — Marketing (two organic engines)
// =========================================================
const s9 = pres.addSlide(); bg(s9); sectionHeader(s9, "МАРКЕТИНГ"); wordmark(s9);
s9.addText("Игра расходится органически — два движка охвата почти без UA-бюджета.",
  { x: 0.6, y: 1.45, w: 12.1, h: 0.55, fontFace: BODY, fontSize: 15, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });
// Engine 1 — Stream (user copy)
card(s9, 0.6, 2.2, 5.95, 4.45, BLUE);
s9.addText("СТРИМ-ДВИЖОК", { x: 0.92, y: 2.5, w: 5.3, h: 0.5, fontFace: HEAD, fontSize: 21, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s9.addText("Воронка стримеров: один поиграл, ему понравилось, узнал другой, дошло до гиганта – тот доиграл до мини-уровня про себя,другие гиганты тоже заинтересовались",
  { x: 0.92, y: 3.2, w: 5.35, h: 3.2, fontFace: BODY, fontSize: 14.5, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.18, margin: 0 });
// Engine 2 — Ragebait (user copy)
card(s9, 6.78, 2.2, 5.95, 4.45, ORANGE);
s9.addText("РЕЙДЖБЕЙТ В ТВИТТЕРЕ", { x: 7.1, y: 2.5, w: 5.3, h: 0.5, fontFace: HEAD, fontSize: 21, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s9.addText("Эджовый сеттинг связан с провокациями и довольно злободневен. Сейчас многие видео на ютубе, рассказывающие о прототипах наших злодеев набирают массу просмотров. Использовать Эпштейна как идею для протагониста будет сильным ходом в ближайший год. К слову о рейджбейте, вот идеальный пример, как я считаю, стоит его проводить",
  { x: 7.1, y: 3.2, w: 5.35, h: 3.2, fontFace: BODY, fontSize: 14.5, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.18, margin: 0 });

// =========================================================
// SLIDE 10 — Production / timeline / risks
// =========================================================
const s10 = pres.addSlide(); bg(s10); sectionHeader(s10, "PRODUCTION"); wordmark(s10);
s10.addText("От готового боевого ядра до релиза — ~12 месяцев. Срок выведен из остатка работ и бюджета и привязан к траншам.",
  { x: 0.6, y: 1.45, w: 12.1, h: 0.7, fontFace: BODY, fontSize: 15, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.1, margin: 0 });
const ms = [
  ["Сейчас", "0", "Системы + 1 биом + босс готовы (5 мес уже вложено)"],
  ["Демо", "3 мес", "Полиш демо + Steam-страница → Next Fest"],
  ["Альфа", "7 мес", "Остальные биомы, боссы, дизайн врагов"],
  ["Бета", "10 мес", "VO, музыка, VFX и финальный полиш"],
  ["Релиз 1.0", "12 мес", "Релиз на Steam (PC)"],
];
const msX = [1.8, 4.2, 6.6, 9.0, 11.4];
const lineY = 3.78;
s10.addShape(pres.shapes.LINE, { x: 1.8, y: lineY, w: 9.6, h: 0, line: { color: BLUE, width: 2 } });
ms.forEach((m, i) => {
  const cx = msX[i];
  s10.addShape(pres.shapes.OVAL, { x: cx - 0.14, y: lineY - 0.14, w: 0.28, h: 0.28, fill: { color: i === 0 ? ORANGE : BLUE } });
  s10.addText(m[0], { x: cx - 1.15, y: 2.72, w: 2.3, h: 0.4, fontFace: HEAD, fontSize: 16, bold: true, color: WHITE, align: "center", valign: "bottom", margin: 0 });
  s10.addText(m[1], { x: cx - 1.15, y: 3.15, w: 2.3, h: 0.34, fontFace: HEAD, fontSize: 13, bold: true, color: BLUE, align: "center", valign: "bottom", margin: 0 });
  s10.addText(m[2], { x: cx - 1.15, y: 4.08, w: 2.3, h: 1.1, fontFace: BODY, fontSize: 11.5, color: MUTE, align: "center", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });
});
card(s10, 0.6, 5.5, 12.13, 1.0, ORANGE);
s10.addText("РИСКИ И МИТИГАЦИЯ", { x: 0.92, y: 5.62, w: 11.6, h: 0.32, fontFace: HEAD, fontSize: 14, bold: true, color: BLUE, charSpacing: 1.5, align: "left", valign: "middle", margin: 0 });
s10.addText("Scope — тугой рогалик, не опенворлд  ·  Найм — аутсорс под майлстон  ·  Сроки — транши привязаны к майлстонам.",
  { x: 0.92, y: 5.96, w: 11.7, h: 0.5, fontFace: BODY, fontSize: 13, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });

// =========================================================
// SLIDE 11 — Ask (publisher vs investor)
// =========================================================
const s11 = pres.addSlide(); bg(s11); sectionHeader(s11, "ЧТО НАМ НУЖНО"); wordmark(s11);
s11.addText("Издатель и инвестор — в идеале одна компания",
  { x: 0.6, y: 1.45, w: 12.1, h: 0.55, fontFace: BODY, fontSize: 15, color: TEXT, align: "left", valign: "top", lineSpacingMultiple: 1.05, margin: 0 });
card(s11, 0.6, 2.2, 5.95, 3.7, BLUE);
s11.addText("ОТ ИЗДАТЕЛЯ", { x: 0.92, y: 2.4, w: 5.3, h: 0.4, fontFace: HEAD, fontSize: 17, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s11.addText(bulletList(["Релиз в Steam, помощь с юридическими моментами", "Маркетинг и продвижение", "Помощь с поиском команды", "Менторство: game-feel, работа с аудиторией", "Помощь с покупкой ассетов на маркетплейсах", "Помощь с QA и локализацией"]),
  { x: 0.95, y: 2.9, w: 5.3, h: 2.9, fontFace: BODY, fontSize: 12.5, color: TEXT, valign: "top", paraSpaceAfter: 7, margin: 0 });
card(s11, 6.78, 2.2, 5.95, 3.7, ORANGE);
s11.addText("ОТ ИНВЕСТОРА", { x: 7.1, y: 2.4, w: 5.3, h: 0.4, fontFace: HEAD, fontSize: 17, bold: true, color: WHITE, charSpacing: 1, align: "left", valign: "middle", margin: 0 });
s11.addText(bulletList(["Финансирование разработки и маркетинга", "Сумма: 150к $: 100 на разработку и 40 на маркетинг", "Использование средств на разработку: команда/аутсорс, озвучка, музыка, ассеты.", "Использование средств на маркетинг: запуск воронки стримеров, работа с страницей в стим, затраты на трейлер"]),
  { x: 7.13, y: 2.9, w: 5.3, h: 2.9, fontFace: BODY, fontSize: 12.5, color: TEXT, valign: "top", paraSpaceAfter: 8, margin: 0 });

// =========================================================
// SLIDE 12 — Closing / contacts
// =========================================================
const s12 = pres.addSlide(); bg(s12);
s12.addShape(pres.shapes.RECTANGLE, { x: 6.375, y: 2.15, w: 0.14, h: 0.14, fill: { color: BLUE } });
s12.addShape(pres.shapes.RECTANGLE, { x: 6.595, y: 2.15, w: 0.14, h: 0.14, fill: { color: BLUE } });
s12.addShape(pres.shapes.RECTANGLE, { x: 6.815, y: 2.15, w: 0.14, h: 0.14, fill: { color: ORANGE } });
s12.addText("СПАСИБО", { x: 0, y: 2.55, w: 13.33, h: 1.3, fontFace: HEAD, fontSize: 72, bold: true, color: WHITE, charSpacing: 6, align: "center", valign: "middle", margin: 0 });
s12.addText("Готовы дать поиграть в билд и выслать демо + P&L.", { x: 0, y: 4.05, w: 13.33, h: 0.5, fontFace: BODY, fontSize: 18, color: TEXT, align: "center", valign: "middle", margin: 0 });
s12.addText("[ Студия ]    ·    email@example.com    ·    store.steampowered.com/app/…", { x: 0, y: 4.85, w: 13.33, h: 0.4, fontFace: BODY, fontSize: 14, color: MUTE, align: "center", valign: "middle", margin: 0 });
s12.addText("POLARITY", { x: 0, y: 6.65, w: 13.33, h: 0.35, fontFace: HEAD, fontSize: 12, color: MUTE, charSpacing: 4, align: "center", valign: "middle", margin: 0 });

pres.writeFile({ fileName: OUT }).then((f) => console.log("WROTE " + f)).catch((e) => { console.error(e); process.exit(1); });
