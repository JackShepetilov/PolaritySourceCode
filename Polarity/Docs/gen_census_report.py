# -*- coding: utf-8 -*-
"""Generate Census_RogueliteFPS.pdf — широкая перепись инди-roguelite-FPS (июнь 2023 – июнь 2026).

Данные: census-workflow (8 агентов, 133 строки с дублями → сведено вручную).
Альбомная A4 под широкую таблицу. Шрифты — Times из Windows (надёжная кириллица).
"""

import os
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.colors import HexColor
from reportlab.lib.units import mm
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, HRFlowable
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(SCRIPT_DIR, "Census_RogueliteFPS.pdf")

WF = "C:/Windows/Fonts"
pdfmetrics.registerFont(TTFont("Body", os.path.join(WF, "times.ttf")))
pdfmetrics.registerFont(TTFont("Body-Bold", os.path.join(WF, "timesbd.ttf")))
pdfmetrics.registerFont(TTFont("Body-Italic", os.path.join(WF, "timesi.ttf")))

DARK=HexColor("#1A1A1A"); MID=HexColor("#666666"); WHITE=HexColor("#FFFFFF")
HR=HexColor("#CCCCCC"); ZEBRA=HexColor("#F6F6F6")
GREEN=HexColor("#1B9E77"); GREEN_BG=HexColor("#E7F4EF")
AMBER=HexColor("#C77A1A"); AMBER_BG=HexColor("#FBF1E4")
RED=HexColor("#C0392B");   RED_BG=HexColor("#FAE9E7")
BLUE=HexColor("#2D7DD2")

st = getSampleStyleSheet()
def S(n, **k):
    p = k.pop("parent", st["Normal"]); return ParagraphStyle(n, parent=p, **k)

title_s = S("T", parent=st["Title"], fontName="Body-Bold", fontSize=26, textColor=DARK, alignment=TA_CENTER, spaceAfter=3*mm)
sub_s   = S("Sub", fontName="Body", fontSize=13, textColor=MID, alignment=TA_CENTER, leading=17)
h1_s    = S("H1", fontName="Body-Bold", fontSize=16, textColor=BLUE, spaceBefore=6*mm, spaceAfter=2.5*mm)
body_s  = S("B", fontName="Body", fontSize=10.5, textColor=DARK, leading=15, spaceAfter=2*mm)
note_s  = S("N", fontName="Body-Italic", fontSize=9.5, textColor=MID, leading=13, spaceAfter=2*mm)
cell_s  = S("C", fontName="Body", fontSize=8.2, textColor=DARK, leading=10)
cellb_s = S("CB", fontName="Body-Bold", fontSize=8.2, textColor=DARK, leading=10)
hd_s    = S("HD", fontName="Body-Bold", fontSize=8.6, textColor=WHITE, leading=11)

def P(t, s=body_s): return Paragraph(t, s)
def H1(t): return Paragraph(t, h1_s)

# ---- canonical data: (name, date, status, reviews, sales, why) ----
HITS = [
    ("Roboquest","2023-11","1.0","~18.8k, Overwhelmingly 95%","1M+ копий все платформы (офиц.); ~$9–14M est","5 лет EA-полировки → лояльная база; эталон жанра"),
    ("Deadzone: Rogue","EA 2025-04 → 1.0 08","1.0","~14.8k, Very Positive 86%","~849k копий, ~$15.9M (Raijin); 150k за 2 нед","Топ-10 мировых на старте, 200k+ вишлистов, стримеры"),
    ("Witchfire","EGS 2023-09 → Steam EA 2024-09","EA","~14k, Very Positive 91%","~560–642k, ~$14–18M est","AAA-вид, 1M+ вишлистов; 1.0 во 2п2026"),
    ("SULFUR","2024-10","EA","~8.5k, Very Positive 89%","200k+ копий (офиц.), ~$2–3.5M est","Уникальный lo-fi арт + сарафан стримеров"),
    ("Far Far West  [1 источник]","2026-04","EA","~15k, Overwhelmingly 96%","1M+ за <2 нед (EG7 офиц.)","Co-op extraction (DRG+Helldivers); крупнейший брейкаут-2026"),
    ("Abyssus  [1 источник]","2025-08","1.0","~4.8k, Very Positive 85%","~272k копий (Gamalytic)","Подводный co-op; крепкий средний"),
]
MID = [
    ("Deadlink","2023-07","1.0","~3.6k, Very Positive 90%","~100–250k (SteamSpy 1–2M раздут giveaway)","Киберпанк Doom-like, sleeper, средний охват"),
    ("Mortal Sin  [пограничн.]","EA 2023 → 1.0 2025-08","1.0","~5.5k, Overwhelmingly 95%","~200–500k owners","FP, но melee, а не пушки"),
    ("Void Crew  [пограничн.]","2024-11","1.0","~5.5k, Very Positive 87%","n/d","Co-op корабль, больше systems-sim"),
    ("Mycopunk  [пограничн.]","2025-07","EA","~3.8k, Very Positive 91%","~120–270k","Mission-based, не чистый permadeath"),
    ("White Knuckle  [пограничн.]","2025-04","EA","~8k, Overwhelmingly 98%","~100k owners","FP speed-climbing; движение, не пушки"),
    ("ROBOBEAT","2024-05","1.0","~1.7k, Very Positive 94%","~20–80k owners","Ритм-FPS; тепло принят, малая аудитория"),
    ("CRUEL","2025-01","1.0","~1.7k, Overwhelmingly 95%","десятки тыс. @ $9.99","Соло-дев Quake/Blood; критик-дарлинг"),
    ("Vampire Hunters","EA 2023 → 1.0 2024-10","1.0","~1.8k, Very Positive 89%","20–50k @ $5.99","Survivors-like от 1-го лица; низкая выручка"),
    ("Bloodshed","EA 2025-05","EA","~1.7k, Very Positive 84%","~50–120k @ $3.89","Бумер × Survivors; объём через низкую цену"),
    ("Fortune's Run","2023-09","EA","~582, Very Positive 91%","~50–100k, ~$1.5M","Иммёрсив-сим/бумер; dev-проблемы тормозят"),
    ("» Voidborn","EA 2025-04","EA","~330, Very Positive 86%","n/d","Movement-роуглайт (dash/slide/wall-run) — БЛИЖЕ ВСЕХ к Polarity"),
    ("» High Fructose","EA 2025-12","EA","~25, Positive 88%","n/d","Survivor-like + паркур (Ghostrunner+Brotato)"),
    ("Desecrators","2025-02","1.0","~310, Overwhelmingly 96%","n/d","6DoF Descent-like co-op; крошечная фан-база"),
    ("SENTRY","EA 2024-11","EA","~850, Positive 82%","n/d","FPS + tower-defense; ниша, микро-студия"),
    ("Moros Protocol","2025-09","1.0","~650, Very Positive 80%","n/d","Пиксельный sci-fi бумер-роуглайт + co-op"),
    ("Holy Shoot / Twilight Manor / HELLBREAK / Unbroken","2024–26","mix","<250 каждая","крошечные","Длинный хвост микро-релизов, хорошие оценки, ~0 охват"),
]
FLOPS = [
    ("Battle Shapers","1.0 2024-12","1.0","~1.1k, 82% (свежие ~52% Mixed)","«Хвалят, не покупают»; финал-мощь плоская, аудитория угасла"),
    ("Deep Rock: Rogue Core","EA 2026-05","EA","~13k, Mixed 69%","Forced-upgrade + жёсткий run-таймер оттолкнули фанатов хит-IP"),
    ("Soulslinger: Envoy of Death","2025-04","1.0","~456, Mixed 64%","Сырой «ватный» ган-плей, тонкий контент; пик ~143 CCU"),
    ("Wild Bastards","2024-09","1.0","~650, Mostly Positive 74%","Критику хвалит, продажи провал; $34.99 дорого, пик ~368 CCU"),
    ("GUNHEAD","2023-11","1.0","~155, Mixed 68%","Мелкий луп + слабый сюжет; ~$52k выручки, ~1 игрок сейчас"),
    ("RIPOUT","2024-05","1.0","774, Mixed 66%","1.0 вышел «как патч»; повторяющиеся миссии/карты"),
    ("The Backrooms Deluxe 2","2025-12","1.0","155, Negative 30%","Полусырой старт + сломанные серверы; пик 2.8k → коллапс"),
    ("Rogue Point","EA 2025-02","EA","n/d (Mixed/low)","Тихий старт даже с Crowbar Collective + Team17"),
    ("B.L.A.C.K. - 1","2023-09","1.0","3 отзыва","Без маркетинга и видимости — вышел и исчез"),
    ("Hyper Light Breaker  [3-е лицо]","EA 2025-01","заброшен","Mixed 63%","Пик 5.2k CCU → <50; студия остановила разработку + увольнения"),
]

def make_table(rows, header, head_bg, six=True):
    data = [[Paragraph(h, hd_s) for h in header]]
    for r in rows:
        cells = [Paragraph(r[0], cellb_s)] + [Paragraph(x, cell_s) for x in r[1:]]
        data.append(cells)
    if six:
        widths = [40*mm, 30*mm, 15*mm, 42*mm, 52*mm, 88*mm]
    else:
        widths = [44*mm, 28*mm, 16*mm, 46*mm, 133*mm]
    t = Table(data, colWidths=widths, repeatRows=1)
    style = [
        ("BACKGROUND",(0,0),(-1,0),head_bg),
        ("ROWBACKGROUNDS",(0,1),(-1,-1),[WHITE,ZEBRA]),
        ("GRID",(0,0),(-1,-1),0.4,HR),
        ("VALIGN",(0,0),(-1,-1),"TOP"),
        ("TOPPADDING",(0,0),(-1,-1),3),("BOTTOMPADDING",(0,0),(-1,-1),3),
        ("LEFTPADDING",(0,0),(-1,-1),4),("RIGHTPADDING",(0,0),(-1,-1),4),
    ]
    t.setStyle(TableStyle(style))
    return t

story = []
story.append(Spacer(1, 30*mm))
story.append(P("POLARITY", title_s))
story.append(P("Перепись инди-roguelite-FPS на Steam (июнь 2023 – июнь 2026)", sub_s))
story.append(Spacer(1, 3*mm))
story.append(P("Широкий охват сегмента: ~33 игры в окне, сгруппированы по коммерческому исходу", sub_s))
story.append(Spacer(1, 10*mm))
story.append(P("13 июня 2026 · данные: census-workflow (8 агентов, Steam / SteamSpy / Gamalytic / VG Insights)", note_s))
story.append(PageBreak())

story.append(H1("Хиты / брейкауты"))
story.append(make_table(HITS, ["Игра","Релиз","Статус","Отзывы Steam","Оценка продаж","Почему взлетела"], GREEN))
story.append(Spacer(1, 5*mm))
story.append(H1("Средние / нишевые (хвалят, продажи умеренные)"))
story.append(make_table(MID, ["Игра","Релиз","Статус","Отзывы Steam","Оценка продаж","Заметка"], AMBER))
story.append(Spacer(1, 6*mm))

story.append(H1("Провалы / недотянули — анти-примеры для питча"))
story.append(make_table(FLOPS, ["Игра","Релиз","Статус","Отзывы Steam","Что пошло не так"], RED, six=False))
story.append(Spacer(1, 5*mm))

story.append(H1("Контекст (вне переписи)"))
story.append(P("<b>Вне окна (до июня 2023):</b> Gunfire Reborn (2021, «кат-кинг» жанра, 2M+ копий, ~$38–65M), Ziggurat 2 (2021), Immortal Redneck (2017), PERISH (2022-08, co-op DOOM-like).", body_s))
story.append(P("<b>Не сегмент (не флэтскрин-FPS-роуглайт):</b> Prodeus / Trepang2 / Selaco (линейная кампания, не roguelite), Synthetik 2 (изометрия), City of Beats (top-down rhythm), Die After Sunset (третье лицо).", body_s))
story.append(Spacer(1, 3*mm))

story.append(H1("Условные обозначения и оговорки"))
story.append(P("<b>»</b> — ближе всего к Polarity по движению. <b>[пограничн.]</b> — FP, но melee / climbing / mission-based. <b>[1 источник]</b> — данные из одного источника, требуют перепроверки.", note_s))
story.append(P("Все оценки продаж/выручки — МОДЕЛЬНЫЕ (Gamalytic, VG Insights, SteamSpy, Steam Revenue Calculator), а не аудит. Счётчики отзывов — снапшоты ~июня 2026, с тех пор растут. Для питча формулировать «по оценке X», с диапазоном.", note_s))

doc = SimpleDocTemplate(OUTPUT, pagesize=landscape(A4),
    topMargin=14*mm, bottomMargin=14*mm, leftMargin=12*mm, rightMargin=12*mm,
    title="POLARITY — Перепись roguelite-FPS", author="Polarity Team")
doc.build(story)
print("Created", OUTPUT)
