"""Generate DemoDialogue_RU.pdf — Russian version of dialogue script."""

import os, csv
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.colors import HexColor
from reportlab.lib.units import mm
from reportlab.lib.enums import TA_CENTER
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
    PageBreak, HRFlowable
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(SCRIPT_DIR, "DemoDialogue_RU.pdf")

WINFONTS = "C:/Windows/Fonts"
pdfmetrics.registerFont(TTFont("Rubik", os.path.join(WINFONTS, "times.ttf")))
pdfmetrics.registerFont(TTFont("Rubik-Bold", os.path.join(WINFONTS, "timesbd.ttf")))
pdfmetrics.registerFont(TTFont("Rubik-SemiBold", os.path.join(WINFONTS, "timesbd.ttf")))

WHITE = HexColor("#FFFFFF")
MID_GREY = HexColor("#888888")
DARK = HexColor("#222222")
ACCENT = HexColor("#2D7DD2")
Z3R0_CLR = HexColor("#1B9E77")
RAMLESS_CLR = HexColor("#D95F02")
STAGE_CLR = HexColor("#7570B3")
TRIGGER_CLR = HexColor("#E7298A")
NOTE_HEAD = HexColor("#2D7DD2")
NOTE_CLR = HexColor("#444444")
TS_CLR = HexColor("#666666")
HR_CLR = HexColor("#CCCCCC")
RED = HexColor("#CC3333")

styles = getSampleStyleSheet()

title_style = ParagraphStyle("CustomTitle", parent=styles["Title"], fontSize=32, textColor=DARK, spaceAfter=6*mm, fontName="Rubik-Bold")
subtitle_style = ParagraphStyle("Subtitle", parent=styles["Normal"], fontSize=16, textColor=MID_GREY, fontName="Rubik", alignment=TA_CENTER)
conf_style = ParagraphStyle("Conf", parent=styles["Normal"], fontSize=10, textColor=RED, fontName="Rubik-Bold", alignment=TA_CENTER)
section_style = ParagraphStyle("Section", parent=styles["Heading1"], fontSize=17, textColor=ACCENT, spaceBefore=10*mm, spaceAfter=4*mm, fontName="Rubik-Bold")
context_style = ParagraphStyle("Context", parent=styles["Normal"], fontSize=10, textColor=MID_GREY, spaceAfter=3*mm, fontName="Rubik", leftIndent=4*mm)
trigger_style = ParagraphStyle("Trigger", parent=styles["Normal"], fontSize=9, textColor=TRIGGER_CLR, spaceBefore=3*mm, spaceAfter=2*mm, fontName="Rubik-SemiBold", leftIndent=4*mm)
z3r0_style = ParagraphStyle("Z3r0", parent=styles["Normal"], fontSize=11, textColor=Z3R0_CLR, spaceAfter=1.5*mm, fontName="Rubik-SemiBold", leftIndent=12*mm)
ramless_style = ParagraphStyle("Ramless", parent=styles["Normal"], fontSize=11, textColor=RAMLESS_CLR, spaceAfter=1.5*mm, fontName="Rubik-SemiBold", leftIndent=12*mm)
stage_dir_style = ParagraphStyle("StageDir", parent=styles["Normal"], fontSize=10, textColor=STAGE_CLR, spaceAfter=2*mm, fontName="Rubik", leftIndent=8*mm)
note_heading_style = ParagraphStyle("NoteHeading", parent=styles["Heading2"], fontSize=13, textColor=NOTE_HEAD, spaceBefore=6*mm, spaceAfter=3*mm, fontName="Rubik-Bold")
note_body_style = ParagraphStyle("NoteBody", parent=styles["Normal"], fontSize=10, textColor=NOTE_CLR, spaceAfter=1.5*mm, fontName="Rubik", leftIndent=6*mm)
timestamp_style = ParagraphStyle("Timestamp", parent=styles["Normal"], fontSize=10, textColor=TS_CLR, spaceAfter=1.5*mm, fontName="Rubik-SemiBold", leftIndent=8*mm)


def d(speaker, text):
    s = z3r0_style if speaker == "z3r0day" else ramless_style
    return Paragraph(f'<b>{speaker}:</b>  \u201c{text}\u201d', s)

def stage(t): return Paragraph(f"[{t}]", stage_dir_style)
def ctx(t): return Paragraph(t, context_style)
def trg(t): return Paragraph(f"\u0422\u0440\u0438\u0433\u0433\u0435\u0440: {t}", trigger_style)
def sec(t): return Paragraph(t, section_style)
def nh(t): return Paragraph(t, note_heading_style)
def note(t): return Paragraph(f"\u2022 {t}", note_body_style)
def ts(t): return Paragraph(t, timestamp_style)
def hr(): return HRFlowable(width="100%", thickness=0.5, color=HR_CLR, spaceAfter=4*mm, spaceBefore=4*mm)

story = []

# Title
story.append(Spacer(1, 60*mm))
story.append(Paragraph("POLARITY", title_style))
story.append(Paragraph("\u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0439 \u0434\u0438\u0430\u043b\u043e\u0433\u043e\u0432 \u0434\u0435\u043c\u043e", subtitle_style))
story.append(Spacer(1, 10*mm))
story.append(Paragraph("\u041a\u041e\u041d\u0424\u0418\u0414\u0415\u041d\u0426\u0418\u0410\u041b\u042c\u041d\u041e", conf_style))
story.append(PageBreak())

# --- Sections ---
sections = {
    "beach": ("1. \u041f\u041b\u042f\u0416 \u2014 \u041f\u0440\u0438\u0431\u044b\u0442\u0438\u0435",
              "\u0418\u0433\u0440\u043e\u043a \u043f\u043e\u044f\u0432\u043b\u044f\u0435\u0442\u0441\u044f \u043d\u0430 \u043f\u043b\u044f\u0436\u0435. \u0414\u0435\u0437\u043e\u0440\u0438\u0435\u043d\u0442\u0438\u0440\u043e\u0432\u0430\u043d. \u0413\u043e\u043b\u043e\u0441 z3r0day \u043f\u043e \u0440\u0430\u0446\u0438\u0438.",
              "\u0410\u0432\u0442\u043e-\u0442\u0440\u0438\u0433\u0433\u0435\u0440 \u043f\u043e\u0441\u043b\u0435 \u0441\u043f\u0430\u0432\u043d\u0430 \u0438\u0433\u0440\u043e\u043a\u0430."),
    "preport": ("2. \u041f\u041e\u0420\u0422 \u2014 \u0427\u0451\u0440\u043d\u044b\u0439 \u0432\u0445\u043e\u0434 + \u041a\u043e\u0441\u0442\u044e\u043c",
                "\u0418\u0433\u0440\u043e\u043a \u043d\u0430\u0445\u043e\u0434\u0438\u0442 \u0447\u0451\u0440\u043d\u044b\u0439 \u0432\u0445\u043e\u0434 \u0432 \u0441\u043b\u0443\u0436\u0435\u0431\u043d\u0443\u044e \u0437\u043e\u043d\u0443 \u043f\u043e\u0440\u0442\u0430.",
                "Proximity trigger \u0443 \u0432\u0445\u043e\u0434\u0430 + interaction \u0441 \u043a\u043e\u0441\u0442\u044e\u043c\u043e\u043c."),
    "port": ("3. \u041f\u041e\u0420\u0422 \u2014 \u0420\u0435\u0430\u043b\u044c\u043d\u044b\u0439 \u0440\u0430\u0437\u0433\u043e\u0432\u043e\u0440",
             "\u041f\u043e\u0441\u043b\u0435 \u043d\u0430\u0434\u0435\u0432\u0430\u043d\u0438\u044f \u043a\u043e\u0441\u0442\u044e\u043c\u0430. z3r0day \u043e\u0431\u044a\u044f\u0441\u043d\u044f\u0435\u0442 \u043f\u0440\u043e \u043a\u043b\u0430\u043d\u043a\u0435\u0440\u043e\u0432.",
             "\u041f\u043e\u0441\u043b\u0435 \u044d\u043a\u0438\u043f\u0438\u0440\u043e\u0432\u043a\u0438 \u043a\u043e\u0441\u0442\u044e\u043c\u0430."),
    "tut": ("4. \u0422\u0423\u0422\u041e\u0420\u0418\u0410\u041b \u2014 \u0414\u0432\u0438\u0436\u0435\u043d\u0438\u0435",
            "\u041e\u0431\u0443\u0447\u0435\u043d\u0438\u0435 \u0434\u0432\u043e\u0439\u043d\u043e\u043c\u0443 \u043f\u0440\u044b\u0436\u043a\u0443, \u0440\u044b\u0432\u043a\u0443, \u0441\u0442\u0435\u043d\u043e\u0431\u0435\u0433\u0443.",
            "\u041f\u043e\u0441\u043b\u0435 \u0432\u044b\u043f\u043e\u043b\u043d\u0435\u043d\u0438\u044f \u043a\u0430\u0436\u0434\u043e\u0433\u043e tutorial prompt."),
    "combat": ("5. \u041f\u0415\u0420\u0412\u042b\u0419 \u0411\u041e\u0419",
               "\u041f\u0435\u0440\u0432\u0430\u044f \u0432\u0441\u0442\u0440\u0435\u0447\u0430 \u0441 \u043a\u043b\u0430\u043d\u043a\u0435\u0440\u0430\u043c\u0438. \u041e\u0431\u0443\u0447\u0435\u043d\u0438\u0435 \u0431\u043e\u044e.",
               "Spawn trigger \u043f\u0440\u0438 \u0432\u0445\u043e\u0434\u0435 \u0432 \u0437\u0430\u043b."),
    "jungle": ("6. \u0414\u0416\u0423\u041d\u0413\u041b\u0418 \u2014 \u041f\u043e\u0434\u0445\u043e\u0434 \u043a \u043e\u0441\u043e\u0431\u043d\u044f\u043a\u0443",
               "\u0418\u0433\u0440\u043e\u043a \u0438\u0434\u0451\u0442 \u043f\u043e \u043d\u0435\u043e\u043d\u043e\u0432\u043e\u043c\u0443 \u043d\u0430\u0441\u0442\u0438\u043b\u0443 \u0447\u0435\u0440\u0435\u0437 \u0434\u0436\u0443\u043d\u0433\u043b\u0438.",
               "Proximity trigger \u043d\u0430 \u0432\u044b\u0445\u043e\u0434\u0435 \u0438\u0437 \u043f\u043e\u0440\u0442\u0430."),
    "premansion": ("7. \u041e\u0421\u041e\u0411\u041d\u042f\u041a \u2014 \u0412\u0445\u043e\u0434",
                   "\u0418\u0433\u0440\u043e\u043a \u0432\u0445\u043e\u0434\u0438\u0442 \u0432 \u043e\u0441\u043e\u0431\u043d\u044f\u043a.",
                   "Proximity trigger \u043f\u0440\u0438 \u0432\u0445\u043e\u0434\u0435."),
    "mansion": ("8. \u041e\u0421\u041e\u0411\u041d\u042f\u041a \u2014 \u0410\u043f\u0433\u0440\u0435\u0439\u0434",
                "\u0418\u0433\u0440\u043e\u043a \u043d\u0430\u0445\u043e\u0434\u0438\u0442 \u0430\u043f\u0433\u0440\u0435\u0439\u0434 \u043a\u043e\u0441\u0442\u044e\u043c\u0430.",
                "Interaction trigger \u0443 \u0430\u043f\u0433\u0440\u0435\u0439\u0434\u0430."),
    "pre_dc": ("9. \u041f\u0415\u0420\u0415\u0425\u041e\u0414 \u041a \u0414\u0410\u0422\u0410-\u0426\u0415\u041d\u0422\u0420\u0423",
               "\u0418\u0433\u0440\u043e\u043a \u0438\u0434\u0451\u0442 \u043e\u0442 \u043e\u0441\u043e\u0431\u043d\u044f\u043a\u0430 \u043a \u0434\u0430\u0442\u0430-\u0446\u0435\u043d\u0442\u0440\u0443.",
               "Proximity trigger \u043c\u0435\u0436\u0434\u0443 \u043e\u0441\u043e\u0431\u043d\u044f\u043a\u043e\u043c \u0438 \u0434\u0430\u0442\u0430-\u0446\u0435\u043d\u0442\u0440\u043e\u043c."),
    "dc_enter": ("10. \u0414\u0410\u0422\u0410-\u0426\u0415\u041d\u0422\u0420 \u2014 \u0412\u0445\u043e\u0434",
                 "\u0418\u0433\u0440\u043e\u043a \u0432\u0445\u043e\u0434\u0438\u0442 \u0432 \u0434\u0430\u0442\u0430-\u0446\u0435\u043d\u0442\u0440.",
                 "Proximity trigger \u043f\u0440\u0438 \u0432\u0445\u043e\u0434\u0435."),
    "dc_shield": ("11. \u0414\u0410\u0422\u0410-\u0426\u0415\u041d\u0422\u0420 \u2014 \u0429\u0438\u0442 \u0441\u043d\u044f\u0442",
                  "\u0429\u0438\u0442 \u043f\u0430\u0434\u0430\u0435\u0442 \u043f\u043e\u0441\u043b\u0435 \u0443\u043d\u0438\u0447\u0442\u043e\u0436\u0435\u043d\u0438\u044f \u0441\u0442\u043e\u0435\u043a.",
                  "\u0421\u043e\u0431\u044b\u0442\u0438\u0435 \u043f\u0430\u0434\u0435\u043d\u0438\u044f \u0449\u0438\u0442\u0430."),
    "dc_grab": ("12. \u0414\u0410\u0422\u0410-\u0426\u0415\u041d\u0422\u0420 \u2014 \u0417\u0430\u0445\u0432\u0430\u0442 RAM",
                "\u0418\u0433\u0440\u043e\u043a \u0437\u0430\u0431\u0438\u0440\u0430\u0435\u0442 RAM-\u044e\u043d\u0438\u0442.",
                "Interaction \u0441 RAM-\u044e\u043d\u0438\u0442\u043e\u043c."),
}

# Read CSV
rows_by_section = {}
with open(os.path.join(SCRIPT_DIR, "DemoDialogue_RU.csv"), encoding="utf-8") as f:
    reader = csv.DictReader(f)
    for row in reader:
        rid = row["ID"]
        # Determine section
        for prefix in sorted(sections.keys(), key=len, reverse=True):
            if rid.startswith(prefix):
                rows_by_section.setdefault(prefix, []).append(row)
                break

section_order = ["beach", "preport", "port", "tut", "combat", "jungle", "premansion", "mansion", "pre_dc", "dc_enter", "dc_shield", "dc_grab"]

for prefix in section_order:
    title, context_text, trigger_text = sections[prefix]
    story.append(sec(title))
    story.append(ctx(context_text))
    story.append(Spacer(1, 2*mm))

    for row in rows_by_section.get(prefix, []):
        speaker = row["Speaker"].strip()
        text = row["Text"].strip()
        story.append(d(speaker, text))

    story.append(trg(trigger_text))
    story.append(hr())

# --- Ending ---
story.append(PageBreak())
story.append(sec("13. \u0424\u0418\u041d\u0410\u041b\u042c\u041d\u0410\u042f \u0421\u0426\u0415\u041d\u0410 \u2014 \u041a\u0438\u043d\u0435\u043c\u0430\u0442\u0438\u043a (\u043c\u0430\u043a\u0441. 45 \u0441\u0435\u043a\u0443\u043d\u0434)"))
story.append(ctx("\u0418\u0433\u0440\u043e\u043a \u0442\u0435\u0440\u044f\u0435\u0442 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435. \u041a\u0430\u043c\u0435\u0440\u0430 \u043f\u0435\u0440\u0435\u0445\u043e\u0434\u0438\u0442 \u0432 \u043a\u0438\u043d\u0435\u043c\u0430\u0442\u043e\u0433\u0440\u0430\u0444\u0438\u0447\u0435\u0441\u043a\u0438\u0439 \u0440\u0435\u0436\u0438\u043c."))
story.append(Spacer(1, 3*mm))

end_timing = {
    "end_01": "[0:00]", "end_02": "[0:03]", "end_03": "[0:06]", "end_04": "[0:08]",
    "end_05": "[0:12]", "end_06": "[0:16]", "end_07": "[0:18]", "end_08": "[0:21]",
    "end_09": "[0:25]", "end_10": "[0:27]", "end_11": "[0:31]", "end_12": "[0:35]",
    "end_13": "[0:38]", "end_14": "[0:40]"
}

end_stages = {
    "end_01": "\u042d\u043a\u0440\u0430\u043d \u0434\u0451\u0440\u0433\u0430\u0435\u0442\u0441\u044f. \u0418\u0433\u0440\u043e\u043a \u0442\u0435\u0440\u044f\u0435\u0442 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435",
    "end_02": "\u041f\u0430\u0443\u0437\u0430. \u0422\u0438\u0448\u0438\u043d\u0430",
    "end_05": "\u041f\u0430\u0443\u0437\u0430 3 \u0441\u0435\u043a\u0443\u043d\u0434\u044b. \u041c\u0443\u0437\u044b\u043a\u0430 \u0441\u0442\u0438\u0445\u0430\u0435\u0442",
    "end_11": "\u041f\u0430\u0443\u0437\u0430 3 \u0441\u0435\u043a\u0443\u043d\u0434\u044b",
}

for row in rows_by_section.get("end", []):
    rid = row["ID"]
    if rid in end_timing:
        story.append(ts(end_timing[rid]))
    if rid in end_stages:
        story.append(stage(end_stages[rid]))
    story.append(d(row["Speaker"].strip(), row["Text"].strip()))
    story.append(Spacer(1, 2*mm))

story.append(ts("[0:42]"))
story.append(stage("SMASH CUT \u2014 \u0427\u0451\u0440\u043d\u044b\u0439 \u044d\u043a\u0440\u0430\u043d"))
story.append(Spacer(1, 2*mm))
story.append(ts("[0:43]"))
story.append(stage("\u0422\u0430\u0439\u0442\u043b: POLARITY"))
story.append(Spacer(1, 2*mm))
story.append(ts("[0:45]"))
story.append(stage("\u041a\u043e\u043d\u0435\u0446 \u0434\u0435\u043c\u043e"))
story.append(hr())

# --- Notes ---
story.append(PageBreak())
story.append(sec("\u0420\u0415\u0416\u0418\u0421\u0421\u0401\u0420\u0421\u041a\u0418\u0415 \u0417\u0410\u041c\u0415\u0422\u041a\u0418"))

story.append(nh("\u0424\u0438\u043d\u0430\u043b\u044c\u043d\u0430\u044f \u0441\u0446\u0435\u043d\u0430"))
story.append(note("\u0421\u0430\u043c\u0430\u044f \u0434\u043b\u0438\u043d\u043d\u0430\u044f \u043f\u0430\u0443\u0437\u0430 \u043c\u0435\u0436\u0434\u0443 \u00ab\u043e \u043d\u0435\u0442\u00bb \u0438 \u00ab\u044d\u0442\u043e \u043d\u0435 \u043f\u0440\u043e\u0441\u0442\u043e \u0445\u0440\u0430\u043d\u0438\u043b\u0438\u0449\u0435 \u0440\u0430\u043c\u0430\u00bb"))
story.append(note("\u0413\u043e\u043b\u043e\u0441 z3r0day \u043c\u0435\u043d\u044f\u0435\u0442\u0441\u044f \u2014 \u0438\u0437 \u0440\u0430\u0441\u0441\u043b\u0430\u0431\u043b\u0435\u043d\u043d\u043e\u0433\u043e \u0445\u0430\u043a\u0435\u0440\u0430 \u0432 \u0440\u0435\u0430\u043b\u044c\u043d\u043e \u0438\u0441\u043f\u0443\u0433\u0430\u043d\u043d\u043e\u0433\u043e \u0447\u0435\u043b\u043e\u0432\u0435\u043a\u0430"))
story.append(note("\u00ab\u043d\u0430\u043c \u043a\u0440\u0430\u043d\u0442\u044b \u0431\u0440\u043e\u00bb \u2014 \u0442\u0438\u0445\u043e, \u0431\u0435\u0437 \u0438\u0440\u043e\u043d\u0438\u0438. Brainrot-\u0442\u0435\u0440\u043c\u0438\u043d \u0441\u043a\u0430\u0437\u0430\u043d\u043d\u044b\u0439 \u0441\u0435\u0440\u044c\u0451\u0437\u043d\u043e"))
story.append(note("SMASH CUT \u0440\u0435\u0437\u043a\u0438\u0439 \u2014 \u043d\u0438\u043a\u0430\u043a\u0438\u0445 fade-out"))

story.append(nh("\u0421\u0438\u0441\u0442\u0435\u043c\u0430 \u0434\u0438\u0430\u043b\u043e\u0433\u043e\u0432"))
story.append(note("z3r0day \u2014 \u0433\u043e\u043b\u043e\u0441 \u043f\u043e \u0440\u0430\u0446\u0438\u0438 (distorted, comms filter)"))
story.append(note("ramless_ \u2014 \u0433\u043e\u043b\u043e\u0441 \u043e\u0442 \u043f\u0435\u0440\u0432\u043e\u0433\u043e \u043b\u0438\u0446\u0430 (\u0447\u0438\u0441\u0442\u044b\u0439, \u0431\u0435\u0437 \u0444\u0438\u043b\u044c\u0442\u0440\u0430)"))
story.append(note("\u0421\u0443\u0431\u0442\u0438\u0442\u0440\u044b \u0432 \u0441\u0442\u0438\u043b\u0435 \u0447\u0430\u0442\u0430 (JetBrains Mono)"))

story.append(nh("\u0425\u0440\u043e\u043d\u043e\u043c\u0435\u0442\u0440\u0430\u0436"))
table_data = [
    ["\u0421\u0435\u043a\u0446\u0438\u044f", "\u0412\u0440\u0435\u043c\u044f"],
    ["\u041f\u043b\u044f\u0436", "30\u201340 \u0441\u0435\u043a"],
    ["\u041f\u043e\u0440\u0442 (\u0432\u0445\u043e\u0434 + \u043a\u043e\u0441\u0442\u044e\u043c)", "20\u201330 \u0441\u0435\u043a"],
    ["\u0422\u0443\u0442\u043e\u0440\u0438\u0430\u043b \u0434\u0432\u0438\u0436\u0435\u043d\u0438\u044f", "1\u20132 \u043c\u0438\u043d"],
    ["\u041f\u0435\u0440\u0432\u044b\u0439 \u0431\u043e\u0439", "1\u20132 \u043c\u0438\u043d"],
    ["\u0414\u0436\u0443\u043d\u0433\u043b\u0438", "15\u201320 \u0441\u0435\u043a"],
    ["\u041e\u0441\u043e\u0431\u043d\u044f\u043a", "5\u20138 \u043c\u0438\u043d"],
    ["\u0414\u0430\u0442\u0430-\u0446\u0435\u043d\u0442\u0440", "5\u20138 \u043c\u0438\u043d"],
    ["\u0424\u0438\u043d\u0430\u043b\u044c\u043d\u0430\u044f \u0441\u0446\u0435\u043d\u0430", "45 \u0441\u0435\u043a"],
    ["\u0418\u0422\u041e\u0413\u041e \u0414\u0415\u041c\u041e", "~15\u201320 \u043c\u0438\u043d"],
]
t = Table(table_data, colWidths=[55*mm, 55*mm])
t.setStyle(TableStyle([
    ("BACKGROUND", (0, 0), (-1, 0), ACCENT),
    ("TEXTCOLOR", (0, 0), (-1, 0), WHITE),
    ("FONTNAME", (0, 0), (-1, 0), "Rubik-Bold"),
    ("FONTSIZE", (0, 0), (-1, 0), 11),
    ("TEXTCOLOR", (0, 1), (-1, -2), DARK),
    ("FONTNAME", (0, 1), (-1, -1), "Rubik"),
    ("FONTSIZE", (0, 1), (-1, -1), 10),
    ("BACKGROUND", (0, -1), (-1, -1), ACCENT),
    ("TEXTCOLOR", (0, -1), (-1, -1), WHITE),
    ("FONTNAME", (0, -1), (-1, -1), "Rubik-Bold"),
    ("GRID", (0, 0), (-1, -1), 0.5, HR_CLR),
    ("TOPPADDING", (0, 0), (-1, -1), 4),
    ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ("LEFTPADDING", (0, 0), (-1, -1), 8),
    ("ALIGN", (1, 0), (1, -1), "CENTER"),
    ("ROWBACKGROUNDS", (0, 1), (-1, -2), [WHITE, HexColor("#F5F5F5")]),
]))
story.append(t)

doc = SimpleDocTemplate(OUTPUT, pagesize=A4, topMargin=20*mm, bottomMargin=20*mm, leftMargin=20*mm, rightMargin=20*mm,
                        title="POLARITY \u2014 \u0421\u0446\u0435\u043d\u0430\u0440\u0438\u0439 \u0434\u0438\u0430\u043b\u043e\u0433\u043e\u0432", author="Polarity Team")
doc.build(story)
print(f"Created {OUTPUT}")
