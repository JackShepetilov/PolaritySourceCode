# -*- coding: utf-8 -*-
"""Generate MarketReport_RogueliteFPS.pdf — рыночный анализ инди-roguelite-FPS для питча.

Источник данных — deep-research workflow (июнь 2026), 21 источник, 25 верифицированных
утверждений (17 подтверждено, 8 отвергнуто адверсариальной проверкой 3 голосами).
Шрифты — Times New Roman из Windows (надёжная кириллица), как в gen_dialogue_ru_pdf.py.
"""

import os
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.colors import HexColor
from reportlab.lib.units import mm
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_JUSTIFY
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
    PageBreak, HRFlowable, KeepTogether
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(SCRIPT_DIR, "MarketReport_RogueliteFPS.pdf")

WINFONTS = "C:/Windows/Fonts"
pdfmetrics.registerFont(TTFont("Body", os.path.join(WINFONTS, "times.ttf")))
pdfmetrics.registerFont(TTFont("Body-Bold", os.path.join(WINFONTS, "timesbd.ttf")))
pdfmetrics.registerFont(TTFont("Body-Italic", os.path.join(WINFONTS, "timesi.ttf")))
pdfmetrics.registerFont(TTFont("Mono", os.path.join(WINFONTS, "consola.ttf")))

# Palette
DARK     = HexColor("#1A1A1A")
MID      = HexColor("#666666")
ACCENT   = HexColor("#2D7DD2")   # blue — neutral headings
GOOD     = HexColor("#1B9E77")   # green — successes / what we do
WARN     = HexColor("#D95F02")   # amber — failure modes
BAD      = HexColor("#CC3333")   # red — do not use
WHITE    = HexColor("#FFFFFF")
HR_CLR   = HexColor("#CCCCCC")
ZEBRA    = HexColor("#F5F5F5")
BOX_GOOD = HexColor("#EAF6F1")
BOX_WARN = HexColor("#FCF0E6")
BOX_BAD  = HexColor("#FBEAEA")

styles = getSampleStyleSheet()

def S(name, **kw):
    base = kw.pop("parent", styles["Normal"])
    return ParagraphStyle(name, parent=base, **kw)

title_s    = S("T", parent=styles["Title"], fontName="Body-Bold", fontSize=30, textColor=DARK, leading=34, alignment=TA_CENTER, spaceAfter=4*mm)
subtitle_s = S("Sub", fontName="Body", fontSize=14, textColor=MID, alignment=TA_CENTER, leading=18)
meta_s     = S("Meta", fontName="Body", fontSize=10, textColor=MID, alignment=TA_CENTER, leading=14)
h1_s       = S("H1", fontName="Body-Bold", fontSize=18, textColor=ACCENT, spaceBefore=8*mm, spaceAfter=3*mm, leading=22)
h2_s       = S("H2", fontName="Body-Bold", fontSize=13, textColor=DARK, spaceBefore=5*mm, spaceAfter=2*mm, leading=16)
body_s     = S("B", fontName="Body", fontSize=10.5, textColor=DARK, leading=15, alignment=TA_JUSTIFY, spaceAfter=2.5*mm)
bullet_s   = S("Bul", fontName="Body", fontSize=10.5, textColor=DARK, leading=15, leftIndent=6*mm, spaceAfter=1.5*mm)
quote_s    = S("Q", fontName="Body-Italic", fontSize=10.5, textColor=HexColor("#333333"), leading=15, leftIndent=8*mm, rightIndent=6*mm, spaceAfter=2*mm)
small_s    = S("Sm", fontName="Body", fontSize=9, textColor=MID, leading=12, spaceAfter=1*mm)
src_s      = S("Src", fontName="Mono", fontSize=8, textColor=MID, leading=11, leftIndent=6*mm)
cell_s     = S("Cell", fontName="Body", fontSize=9, textColor=DARK, leading=12)
cellb_s    = S("CellB", fontName="Body-Bold", fontSize=9, textColor=DARK, leading=12)
cellh_s    = S("CellH", fontName="Body-Bold", fontSize=9.5, textColor=WHITE, leading=12)

def P(t, s=body_s): return Paragraph(t, s)
def H1(t): return Paragraph(t, h1_s)
def H2(t): return Paragraph(t, h2_s)
def B(t): return Paragraph(f"&bull;&nbsp;&nbsp;{t}", bullet_s)
def Q(t): return Paragraph(t, quote_s)
def HR(): return HRFlowable(width="100%", thickness=0.5, color=HR_CLR, spaceBefore=3*mm, spaceAfter=3*mm)

def callout(title, lines, box_color, border_color):
    """A coloured info box."""
    inner = [Paragraph(title, S("ct", fontName="Body-Bold", fontSize=11, textColor=border_color, leading=14, spaceAfter=1.5*mm))]
    for ln in lines:
        inner.append(Paragraph(ln, S("cb", fontName="Body", fontSize=10, textColor=DARK, leading=14, spaceAfter=1*mm)))
    t = Table([[inner]], colWidths=[170*mm])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0,0), (-1,-1), box_color),
        ("BOX", (0,0), (-1,-1), 1, border_color),
        ("LEFTPADDING", (0,0), (-1,-1), 6*mm),
        ("RIGHTPADDING", (0,0), (-1,-1), 5*mm),
        ("TOPPADDING", (0,0), (-1,-1), 3*mm),
        ("BOTTOMPADDING", (0,0), (-1,-1), 3*mm),
    ]))
    return t

story = []

# ============ TITLE ============
story.append(Spacer(1, 45*mm))
story.append(P("POLARITY", title_s))
story.append(P("Анализ рынка инди-roguelite-FPS (2023–2026)", subtitle_s))
story.append(Spacer(1, 4*mm))
story.append(P("Материал для питча издателю: разбор провалов и причин успеха сегмента", meta_s))
story.append(Spacer(1, 18*mm))
story.append(P("13 июня 2026 г.", meta_s))
story.append(P("Источник данных: deep-research (21 источник, 25 верифицированных утверждений,<br/>адверсариальная проверка в 3 голоса — 17 подтверждено, 8 отвергнуто)", meta_s))
story.append(PageBreak())

# ============ 0. ДИСКЛЕЙМЕР ============
story.append(H1("0. Как пользоваться этим документом"))
story.append(P("Этот документ собран, чтобы ответить на конкретную претензию издателя: <b>не ссылаться на хорошую игру (Void Breaker) со словами «мы как она», а показать провалы сегмента и объяснить, почему Polarity их не повторит.</b> Ниже — расслоение рынка, три задокументированных способа провала (с прямыми цитатами разработчиков) и готовая рамка для питча."))
story.append(callout(
    "Важно про цифры продаж",
    ["Все оценки продаж и выручки (Gamalytic, VG Insights, Games-Stats, SteamSpy) — это <b>модельные оценки</b> по методу Boxleiter, а не финансовая отчётность. В питче формулируй «по оценке X», с диапазоном — не как голый факт.",
     "Данные — снапшоты конкретных дат 2025–2026 гг. Счётчики обзоров и выручки с тех пор выросли.",
     "Раздел 5 перечисляет утверждения, которые <b>провалили</b> проверку — их использовать нельзя."],
    BOX_WARN, WARN))
story.append(PageBreak())

# ============ 1. EXECUTIVE SUMMARY ============
story.append(H1("1. Главный вывод"))
story.append(P("За окно июнь-2023 — июнь-2026 на Steam вышла плотная когорта инди-roguelite-FPS. Проверенные данные чётко расслаивают сегмент на «коммерчески взлетевших» и «хвалят, но не покупают». При этом ключевой инсайт для питча контринтуитивен:"))
story.append(callout(
    "Рынок проваливается не по случайности, а по трём повторяющимся причинам",
    ["<b>1. Мета-прогрессия, убивающая ощущение силы.</b> Игрок не чувствует роста — игра скучнеет.",
     "<b>2. Сырой Early Access при горстке тестеров.</b> Старт бьёт по репутации и вишлистам.",
     "<b>3. Бесконечный Early Access без выхода в 1.0.</b> Игра «застывает», теряет импульс.",
     "На эти грабли наступают <b>даже сильные команды</b> (Witchfire от создателей Vanishing of Ethan Carter). Это и есть сила аргумента: не «конкуренты плохие», а «вот системные ловушки жанра — Polarity обходит каждую по конструкции»."],
    BOX_GOOD, GOOD))
story.append(P("Такая рамка отвечает на претензию издателя напрямую и звучит увереннее, чем сравнение с хорошей игрой: ты демонстрируешь, что <b>изучил кладбище жанра</b> и спроектировал игру против его типичных ошибок.", body_s))
story.append(PageBreak())

# ============ 2. РАССЛОЕНИЕ СЕГМЕНТА ============
story.append(H1("2. Расслоение сегмента: кто и как продался"))
story.append(P("Игры, по которым удалось верифицировать данные тремя независимыми проверками. Это <b>не</b> полная перепись сегмента (см. раздел 6 — пробелы), а проверенное ядро."))

hdr = [Paragraph(x, cellh_s) for x in ["Игра", "Статус", "Отзывы / рейтинг", "Оценка продаж", "Вывод"]]
rows = [
    hdr,
    [Paragraph("<b>Roboquest</b>", cellb_s), Paragraph("1.0<br/>self-pub", cell_s),
     Paragraph("~24k, 95%<br/>«Overwhelmingly Positive»", cell_s),
     Paragraph("~832k копий, ~$14.5M<br/>(Gamalytic); >1M на всех<br/>платформах", cell_s),
     Paragraph("Эталон успеха", S("g", fontName="Body-Bold", fontSize=9, textColor=GOOD, leading=12))],
    [Paragraph("<b>Deadzone: Rogue</b>", cellb_s), Paragraph("EA<br/>апр-2025", cell_s),
     Paragraph("82% «Very Positive»<br/>при 2500+ обзорах", cell_s),
     Paragraph("150k+ копий за 2 недели<br/>(заявл. студии), Top-10<br/>Global Seller", cell_s),
     Paragraph("Взрывной старт", S("g2", fontName="Body-Bold", fontSize=9, textColor=GOOD, leading=12))],
    [Paragraph("<b>Deadlink</b>", cellb_s), Paragraph("1.0", cell_s),
     Paragraph("2775 обзоров, ~89%<br/>«Very Positive»", cell_s),
     Paragraph("~$1.3M (Games-Stats)", cell_s),
     Paragraph("Крепкий средний", S("m", fontName="Body", fontSize=9, textColor=MID, leading=12))],
    [Paragraph("<b>Battle Shapers</b>", cellb_s), Paragraph("1.0<br/>дек-2024", cell_s),
     Paragraph("82.2%, но всего<br/>~1038 обзоров", cell_s),
     Paragraph("~31–54k владельцев<br/>(VGI / Gamalytic)", cell_s),
     Paragraph("Анти-пример:<br/>хвалят, не покупают", S("b2", fontName="Body-Bold", fontSize=9, textColor=BAD, leading=12))],
]
t = Table(rows, colWidths=[26*mm, 18*mm, 38*mm, 46*mm, 32*mm])
t.setStyle(TableStyle([
    ("BACKGROUND", (0,0), (-1,0), ACCENT),
    ("ROWBACKGROUNDS", (0,1), (-1,-1), [WHITE, ZEBRA]),
    ("GRID", (0,0), (-1,-1), 0.5, HR_CLR),
    ("VALIGN", (0,0), (-1,-1), "TOP"),
    ("TOPPADDING", (0,0), (-1,-1), 4),
    ("BOTTOMPADDING", (0,0), (-1,-1), 4),
    ("LEFTPADDING", (0,0), (-1,-1), 5),
    ("RIGHTPADDING", (0,0), (-1,-1), 5),
]))
story.append(t)
story.append(Spacer(1, 3*mm))
story.append(P("<b>Что общего у успешных:</b> сильный фундамент вишлистов к старту, цена $24.99, self-publishing с собственной аудиторией, и — главное — мгновенно считываемый «крючок». <b>Battle Shapers</b> — лучший учебный анти-пример: добротная игра с хорошими отзывами, но низкий объём обзоров и оценки владельцев выдают слабые продажи. Вывод для питча: <b>«хорошая игра» — необходимое, но НЕ достаточное условие. Нужен крючок и вишлисты.</b>", body_s))
story.append(PageBreak())

# ============ 3. FAILURE MODES ============
story.append(H1("3. Три способа провала (с цитатами разработчиков)"))

story.append(H2("Failure mode #1 — мета-прогрессия против ощущения силы"))
story.append(P("<b>Witchfire</b> в раннем EA авто-скейлил мир: «ведьма» повышала уровень синхронно с игроком. Игрок никогда не мог почувствовать себя сильнее или перевести дух. Реакция была эмоционально-негативной, и студия The Astronauts откатила систему, заменив её на Gnosis (янв-2024)."))
story.append(Q("«…то, что ведьма растёт в уровне вместе с игроком, означает, что игрок не может перевести дыхание, не говоря уже о том, чтобы стать сверхсильным… На бумаге всё имело смысл… но людям было всё равно. Достаточному числу людей, чтобы вернуть нас за чертёжную доску.»  — блог The Astronauts, янв-2024"))

story.append(H2("Failure mode #2 — сырой Early Access при горстке тестеров"))
story.append(P("Старт Witchfire в EA директор Адриан Хмеляж описал словом «<b>humiliating</b>» (унизительно). Команда думала, что «всё сделала идеально», но живые игроки мгновенно вскрыли проблемы онбординга: очевидные разработчикам вещи игрокам никто не объяснил."))
story.append(Q("«…до запуска у нас было 12 пар глаз, после — тысячи… мы сделали глупые ошибки, которые пришлось срочно исправлять.»  — Адриан Хмеляж, интервью Escapist"))

story.append(H2("Failure mode #3 — бесконечный Early Access без выхода в 1.0"))
story.append(P("<b>SULFUR</b> вышел в EA 28 октября 2024 и спустя 19+ месяцев всё ещё не вышел в 1.0 — несмотря на заявленную цель «поставить рекорд самого короткого EA в истории». Приём при этом хороший (86.8%, 8878 обзоров), но игра «застыла» в раннем доступе и потеряла нарративный импульс релиза. Затяжной EA лишает игру второй волны внимания прессы и стримеров, которую даёт выход 1.0."))
story.append(PageBreak())

# ============ 4. КАК ЭТО ЛОЖИТСЯ НА POLARITY ============
story.append(H1("4. Как это ложится на Polarity"))
story.append(P("Для каждого failure mode — конкретное проектное решение Polarity. Это и есть скелет нужного издателю слайда «они споткнулись о X — мы делаем иначе»."))

story.append(callout(
    "#1 Мета-прогрессия → горизонтальная, а не вертикальная",
    ["Witchfire наказал игрока авто-скейлингом. В Polarity апгрейды по дизайну <b>расширяют боевой словарь, а не качают числа</b> (GDD §8.6), а враги не масштабируются под уровень игрока. Прогрессия делает игру глубже, не «беговой дорожкой» — ловушка Witchfire исключена конструктивно."],
    BOX_GOOD, GOOD))
story.append(callout(
    "#2 Сырой EA → демо и плейтесты до старта",
    ["Урок Witchfire — не процессное допущение, а обязательство: публичное демо и волны плейтестов <b>до</b> EA, чтобы вскрыть онбординг на десятках игроков, а не на тысячах покупателей. Это пункт плана релиза, который издатель хочет видеть явно."],
    BOX_GOOD, GOOD))
story.append(callout(
    "#3 Бесконечный EA → конечный, дисциплинированный скоуп",
    ["Структура Polarity конечна и отгружаема: <b>4 тира × 3 арены = 12 арен, ран из 4 арен</b>. Это позволяет заявить честную дату выхода в 1.0, а не зависнуть в EA, как SULFUR."],
    BOX_GOOD, GOOD))
story.append(callout(
    "Урок Battle Shapers → крючок + вишлисты решают",
    ["Хорошие отзывы не спасли Battle Shapers от слабых продаж. Крючок Polarity — <b>скоростное движение в духе Titanfall 2 + физическая электромагнитная боёвка</b> (заряжай врагов и предметы, швыряй друг в друга). Ни одна верифицированная игра сегмента не совмещает оба столпа — ниша свободна (см. раздел 6)."],
    BOX_GOOD, GOOD))
story.append(PageBreak())

# ============ 5. НЕ ИСПОЛЬЗОВАТЬ ============
story.append(H1("5. НЕ использовать в питче (провалили проверку)"))
story.append(P("Эти заманчивые цифры всплыли в источниках, но <b>провалили</b> адверсариальную верификацию (отвергнуты 3 голосами против 0 или 2 против 1). Если вставишь их в питч и издатель проверит — ударит по доверию ко всему остальному."))
story.append(callout(
    "Запрещённые утверждения",
    ["✗ «У Battle Shapers пик онлайна всего 206 игроков» (отвергнуто 0–3)",
     "✗ «У Deadzone: Rogue было ~400k вишлистов на анонс» (0–3)",
     "✗ «SULFUR продал ~486k копий / $7.9M» (0–3)",
     "✗ «Witchfire преодолел 500k продаж на Steam» (1–2)",
     "✗ Любые выводы про «похожие на Mullet Madjack» из алгоритма Steam «More Like This»"],
    BOX_BAD, BAD))
story.append(Spacer(1, 4*mm))

# ============ 6. ПРОБЕЛЫ ============
story.append(H1("6. Пробелы (что ещё не закрыто)"))
story.append(B("<b>Перепись неполная.</b> По 8 кандидатам нет верифицированных данных: Gunhead, Soulslinger: Envoy of Death, City of Beats, Robobeat, PERISH, Vampire Hunters, Rogue Point, Die After Sunset. Чтобы ответить «сколько всего игр вышло за окно» — нужен второй прицельный раунд."))
story.append(B("<b>Нет маркетинговых постмортемов.</b> Все подтверждённые провалы — про дизайн и EA-стратегию. Провалов именно маркетинга (слабая капсула, нет демо, мало вишлистов) с разбором «почему не взлетело» найти не удалось — стоит искать игры с рейтингом «Mixed/Negative» и постмортемом."))
story.append(B("<b>Нет прямого компаратора.</b> Ни одна верифицированная игра не совмещает «скоростное движение + физическая ЭМ-боёвка». Для позиционирования это <b>плюс</b> — ниша не занята, — но прямого якоря «мы как X, но лучше» в сегменте нет."))
story.append(Spacer(1, 4*mm))

# ============ ИСТОЧНИКИ ============
story.append(H1("Источники (верифицированные)"))
for s in [
    "Roboquest — gamalytic.com/game/692890 · store.steampowered.com/app/692890",
    "Deadzone: Rogue — games-stats.com/steam/game/deadzone-rogue · steamspy.com/app/3228590",
    "Battle Shapers — steamdb.info/app/1421290 · steamspy.com/app/1421290",
    "Witchfire (Gnosis) — theastronauts.com/2024/01/witchfire-new-feature-gnosis",
    "Witchfire (EA «humiliating») — escapistmagazine.com/news-witchfire-interview-adrian-chmielarz",
    "SULFUR — app.sensortower.com/vgi/game/sulfur · steambase.io/games/sulfur/reviews",
    "Deadlink — games-stats.com/steam/game/deadlink · steambase.io/games/deadlink/reviews",
]:
    story.append(Paragraph(s, src_s))

doc = SimpleDocTemplate(
    OUTPUT, pagesize=A4,
    topMargin=20*mm, bottomMargin=18*mm, leftMargin=20*mm, rightMargin=20*mm,
    title="POLARITY — Анализ рынка инди-roguelite-FPS", author="Polarity Team",
)
doc.build(story)
print("Created", OUTPUT)
