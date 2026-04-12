"""Generate DemoDialogue.pdf from CSV data."""

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
OUTPUT = os.path.join(SCRIPT_DIR, "DemoDialogue.pdf")

pdfmetrics.registerFont(TTFont("Rajdhani", os.path.join(SCRIPT_DIR, "Rajdhani-Regular.ttf")))
pdfmetrics.registerFont(TTFont("Rajdhani-Bold", os.path.join(SCRIPT_DIR, "Rajdhani-Bold.ttf")))
pdfmetrics.registerFont(TTFont("Rajdhani-SemiBold", os.path.join(SCRIPT_DIR, "Rajdhani-SemiBold.ttf")))

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

title_style = ParagraphStyle("CustomTitle", parent=styles["Title"], fontSize=32, textColor=DARK, spaceAfter=6*mm, fontName="Rajdhani-Bold")
subtitle_style = ParagraphStyle("Subtitle", parent=styles["Normal"], fontSize=16, textColor=MID_GREY, fontName="Rajdhani", alignment=TA_CENTER)
conf_style = ParagraphStyle("Conf", parent=styles["Normal"], fontSize=10, textColor=RED, fontName="Rajdhani-Bold", alignment=TA_CENTER)
section_style = ParagraphStyle("Section", parent=styles["Heading1"], fontSize=17, textColor=ACCENT, spaceBefore=10*mm, spaceAfter=4*mm, fontName="Rajdhani-Bold")
context_style = ParagraphStyle("Context", parent=styles["Normal"], fontSize=10, textColor=MID_GREY, spaceAfter=3*mm, fontName="Rajdhani", leftIndent=4*mm)
trigger_style = ParagraphStyle("Trigger", parent=styles["Normal"], fontSize=9, textColor=TRIGGER_CLR, spaceBefore=3*mm, spaceAfter=2*mm, fontName="Rajdhani-SemiBold", leftIndent=4*mm)
z3r0_style = ParagraphStyle("Z3r0", parent=styles["Normal"], fontSize=11, textColor=Z3R0_CLR, spaceAfter=1.5*mm, fontName="Rajdhani-SemiBold", leftIndent=12*mm)
ramless_style = ParagraphStyle("Ramless", parent=styles["Normal"], fontSize=11, textColor=RAMLESS_CLR, spaceAfter=1.5*mm, fontName="Rajdhani-SemiBold", leftIndent=12*mm)
stage_dir_style = ParagraphStyle("StageDir", parent=styles["Normal"], fontSize=10, textColor=STAGE_CLR, spaceAfter=2*mm, fontName="Rajdhani", leftIndent=8*mm)
note_heading_style = ParagraphStyle("NoteHeading", parent=styles["Heading2"], fontSize=13, textColor=NOTE_HEAD, spaceBefore=6*mm, spaceAfter=3*mm, fontName="Rajdhani-Bold")
note_body_style = ParagraphStyle("NoteBody", parent=styles["Normal"], fontSize=10, textColor=NOTE_CLR, spaceAfter=1.5*mm, fontName="Rajdhani", leftIndent=6*mm)
timestamp_style = ParagraphStyle("Timestamp", parent=styles["Normal"], fontSize=10, textColor=TS_CLR, spaceAfter=1.5*mm, fontName="Rajdhani-SemiBold", leftIndent=8*mm)


def d(speaker, text):
    s = z3r0_style if speaker == "z3r0day" else ramless_style
    return Paragraph(f'<b>{speaker}:</b>  \u201c{text}\u201d', s)

def stage(t): return Paragraph(f"[{t}]", stage_dir_style)
def ctx(t): return Paragraph(t, context_style)
def trg(t): return Paragraph(f"Trigger: {t}", trigger_style)
def sec(t): return Paragraph(t, section_style)
def nh(t): return Paragraph(t, note_heading_style)
def note(t): return Paragraph(f"\u2022 {t}", note_body_style)
def ts(t): return Paragraph(t, timestamp_style)
def hr(): return HRFlowable(width="100%", thickness=0.5, color=HR_CLR, spaceAfter=4*mm, spaceBefore=4*mm)

story = []

# Title
story.append(Spacer(1, 60*mm))
story.append(Paragraph("POLARITY", title_style))
story.append(Paragraph("Demo Dialogue Script", subtitle_style))
story.append(Spacer(1, 10*mm))
story.append(Paragraph("CONFIDENTIAL", conf_style))
story.append(PageBreak())

sections = {
    "beach": ("1. BEACH \u2014 Arrival", "Player spawns on a tropical island beach. Disoriented. z3r0day\u2019s voice through comms.", "Auto-trigger after player spawn."),
    "preport": ("2. PORT \u2014 Back Entrance + Suit", "Player finds back entrance to port service area. Finds the suit.", "Proximity trigger at entrance + interaction with suit."),
    "port": ("3. PORT \u2014 The Real Talk", "After equipping the suit. z3r0day explains about clankers.", "After suit equip."),
    "tut": ("4. TUTORIAL \u2014 Movement", "Double jump, air dash, wallrun training.", "After each tutorial prompt completion."),
    "combat": ("5. FIRST COMBAT", "First encounter with clankers. Combat training.", "Spawn trigger on hall entry."),
    "jungle": ("6. JUNGLE PATH \u2014 Approach to Mansion", "Neon-lit boardwalk through jungle to mansion.", "Proximity trigger at port exit."),
    "premansion": ("7. MANSION \u2014 Entry", "Player enters the mansion.", "Proximity trigger at entrance."),
    "mansion": ("8. MANSION \u2014 Upgrade", "Player finds suit upgrade.", "Interaction trigger at upgrade."),
    "pre_dc": ("9. TRANSITION TO DATACENTER", "Player walks from mansion to datacenter. Callback to earlier motivation.", "Proximity trigger between mansion and datacenter."),
    "dc_enter": ("10. DATACENTER \u2014 Entry", "Player enters the datacenter.", "Proximity trigger at entrance."),
    "dc_shield": ("11. DATACENTER \u2014 Shield Down", "Shield drops after destroying enough racks.", "Shield drop event."),
    "dc_grab": ("12. DATACENTER \u2014 RAM Grab", "Player grabs the RAM unit.", "Interaction with RAM unit."),
}

# Read CSV
rows_by_section = {}
with open(os.path.join(SCRIPT_DIR, "DemoDialogue.csv"), encoding="utf-8") as f:
    reader = csv.DictReader(f)
    for row in reader:
        rid = row["ID"]
        for prefix in sorted(sections.keys(), key=len, reverse=True):
            if rid.startswith(prefix):
                rows_by_section.setdefault(prefix, []).append(row)
                break
        else:
            rows_by_section.setdefault("end", []).append(row)

section_order = ["beach", "preport", "port", "tut", "combat", "jungle", "premansion", "mansion", "pre_dc", "dc_enter", "dc_shield", "dc_grab"]

for prefix in section_order:
    title, context_text, trigger_text = sections[prefix]
    story.append(sec(title))
    story.append(ctx(context_text))
    story.append(Spacer(1, 2*mm))
    for row in rows_by_section.get(prefix, []):
        story.append(d(row["Speaker"].strip(), row["Text"].strip()))
    story.append(trg(trigger_text))
    story.append(hr())

# Ending
story.append(PageBreak())
story.append(sec("13. ENDING SEQUENCE \u2014 Cinematic (max 45 seconds)"))
story.append(ctx("Player loses control. Camera goes cinematic. Music fades to ambient."))
story.append(Spacer(1, 3*mm))

end_timing = {
    "end_01": "[0:00]", "end_02": "[0:03]", "end_03": "[0:06]", "end_04": "[0:08]",
    "end_05": "[0:12]", "end_06": "[0:16]", "end_07": "[0:18]", "end_08": "[0:21]",
    "end_09": "[0:25]", "end_10": "[0:27]", "end_11": "[0:31]", "end_12": "[0:35]",
    "end_13": "[0:38]", "end_14": "[0:40]"
}
end_stages = {
    "end_01": "Screen glitches slightly. Player loses control",
    "end_02": "Pause. Silence",
    "end_05": "3 second pause. Music drops to quiet ambient",
    "end_11": "3 second pause",
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
story.append(stage("SMASH CUT TO BLACK"))
story.append(Spacer(1, 2*mm))
story.append(ts("[0:43]"))
story.append(stage("Title card: POLARITY"))
story.append(Spacer(1, 2*mm))
story.append(ts("[0:45]"))
story.append(stage("End of demo"))
story.append(hr())

# Notes
story.append(PageBreak())
story.append(sec("DIRECTOR\u2019S NOTES"))

story.append(nh("Ending Sequence"))
story.append(note("Longest pause between \u201coh no\u201d and \u201cthis isn\u2019t just RAM storage\u201d \u2014 tension builds through silence"))
story.append(note("z3r0day\u2019s tone shifts in the finale \u2014 from relaxed hacker to genuinely scared"))
story.append(note("\u201cwe\u2019re cooked bro\u201d \u2014 final line, delivered quietly, no irony. A brainrot term said dead serious"))
story.append(note("SMASH CUT must be abrupt \u2014 no fade-out. Black screen instantly"))
story.append(note("Title card appears after 1 second of silence on black"))

story.append(nh("Dialogue System"))
story.append(note("All lines triggered via Level Sequence or Blueprint triggers"))
story.append(note("z3r0day \u2014 voice over comms only (slightly distorted, comms filter)"))
story.append(note("ramless_ \u2014 first-person voice (clean, no filter)"))
story.append(note("Subtitles displayed in chat style (JetBrains Mono font, same colors)"))

story.append(nh("Sound Design Key Moments"))
story.append(note("Teleportation to beach \u2014 sharp sonic impact + silence + waves"))
story.append(note("Equipping suit \u2014 power-up sound, energy hum"))
story.append(note("First clanker kill \u2014 brainrot text pop + vine boom"))
story.append(note("\u201coh no\u201d \u2014 music cuts completely within 1 second"))
story.append(note("SMASH CUT \u2014 all audio cuts abruptly"))

story.append(nh("Timing Estimates"))
table_data = [
    ["Section", "Est. Duration"],
    ["Beach arrival", "30\u201340 sec"],
    ["Port (entrance + suit)", "20\u201330 sec"],
    ["Movement tutorial", "1\u20132 min"],
    ["First combat", "1\u20132 min"],
    ["Jungle path", "15\u201320 sec"],
    ["Mansion raid", "5\u20138 min"],
    ["Pre-datacenter", "15\u201320 sec"],
    ["Datacenter", "5\u20138 min"],
    ["Ending cinematic", "45 sec"],
    ["TOTAL DEMO", "~15\u201320 min"],
]
t = Table(table_data, colWidths=[55*mm, 55*mm])
t.setStyle(TableStyle([
    ("BACKGROUND", (0, 0), (-1, 0), ACCENT),
    ("TEXTCOLOR", (0, 0), (-1, 0), WHITE),
    ("FONTNAME", (0, 0), (-1, 0), "Rajdhani-Bold"),
    ("FONTSIZE", (0, 0), (-1, 0), 11),
    ("TEXTCOLOR", (0, 1), (-1, -2), DARK),
    ("FONTNAME", (0, 1), (-1, -1), "Rajdhani"),
    ("FONTSIZE", (0, 1), (-1, -1), 10),
    ("BACKGROUND", (0, -1), (-1, -1), ACCENT),
    ("TEXTCOLOR", (0, -1), (-1, -1), WHITE),
    ("FONTNAME", (0, -1), (-1, -1), "Rajdhani-Bold"),
    ("GRID", (0, 0), (-1, -1), 0.5, HR_CLR),
    ("TOPPADDING", (0, 0), (-1, -1), 4),
    ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ("LEFTPADDING", (0, 0), (-1, -1), 8),
    ("ALIGN", (1, 0), (1, -1), "CENTER"),
    ("ROWBACKGROUNDS", (0, 1), (-1, -2), [WHITE, HexColor("#F5F5F5")]),
]))
story.append(t)

doc = SimpleDocTemplate(OUTPUT, pagesize=A4, topMargin=20*mm, bottomMargin=20*mm, leftMargin=20*mm, rightMargin=20*mm,
                        title="POLARITY \u2014 Demo Dialogue Script", author="Polarity Team")
doc.build(story)
print(f"Created {OUTPUT}")
