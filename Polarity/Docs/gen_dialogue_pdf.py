"""Generate DemoDialogue.pdf from script data."""

import os
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

# Register Rajdhani fonts
pdfmetrics.registerFont(TTFont("Rajdhani", os.path.join(SCRIPT_DIR, "Rajdhani-Regular.ttf")))
pdfmetrics.registerFont(TTFont("Rajdhani-Bold", os.path.join(SCRIPT_DIR, "Rajdhani-Bold.ttf")))
pdfmetrics.registerFont(TTFont("Rajdhani-SemiBold", os.path.join(SCRIPT_DIR, "Rajdhani-SemiBold.ttf")))

# Colors — clean, professional, readable
WHITE = HexColor("#FFFFFF")
LIGHT_GREY = HexColor("#D0D0D0")
MID_GREY = HexColor("#888888")
DIM_GREY = HexColor("#555555")
DARK = HexColor("#222222")
ACCENT = HexColor("#2D7DD2")       # calm blue — section headings
Z3R0_CLR = HexColor("#1B9E77")     # teal green — z3r0day
RAMLESS_CLR = HexColor("#D95F02")  # warm orange — ramless_
STAGE_CLR = HexColor("#7570B3")    # muted purple — stage directions
TRIGGER_CLR = HexColor("#E7298A")  # soft magenta — triggers
NOTE_HEAD = HexColor("#2D7DD2")    # blue — note headings
NOTE_CLR = HexColor("#444444")     # dark grey — note body
TS_CLR = HexColor("#666666")       # grey — timestamps
TABLE_HEAD = HexColor("#2D7DD2")
TABLE_TOTAL = HexColor("#D95F02")
HR_CLR = HexColor("#CCCCCC")
RED = HexColor("#CC3333")

styles = getSampleStyleSheet()

# Styles
title_style = ParagraphStyle(
    "CustomTitle", parent=styles["Title"],
    fontSize=32, textColor=DARK, spaceAfter=6*mm,
    fontName="Rajdhani-Bold"
)
subtitle_style = ParagraphStyle(
    "Subtitle", parent=styles["Normal"],
    fontSize=16, textColor=MID_GREY, fontName="Rajdhani",
    alignment=TA_CENTER
)
conf_style = ParagraphStyle(
    "Conf", parent=styles["Normal"],
    fontSize=10, textColor=RED, fontName="Rajdhani-Bold",
    alignment=TA_CENTER
)
section_style = ParagraphStyle(
    "Section", parent=styles["Heading1"],
    fontSize=17, textColor=ACCENT, spaceBefore=10*mm, spaceAfter=4*mm,
    fontName="Rajdhani-Bold"
)
context_style = ParagraphStyle(
    "Context", parent=styles["Normal"],
    fontSize=10, textColor=MID_GREY, spaceAfter=3*mm,
    fontName="Rajdhani", leftIndent=4*mm
)
trigger_style = ParagraphStyle(
    "Trigger", parent=styles["Normal"],
    fontSize=9, textColor=TRIGGER_CLR, spaceBefore=3*mm, spaceAfter=2*mm,
    fontName="Rajdhani-SemiBold", leftIndent=4*mm
)
z3r0_style = ParagraphStyle(
    "Z3r0", parent=styles["Normal"],
    fontSize=11, textColor=Z3R0_CLR, spaceAfter=1.5*mm,
    fontName="Rajdhani-SemiBold", leftIndent=12*mm
)
ramless_style = ParagraphStyle(
    "Ramless", parent=styles["Normal"],
    fontSize=11, textColor=RAMLESS_CLR, spaceAfter=1.5*mm,
    fontName="Rajdhani-SemiBold", leftIndent=12*mm
)
stage_dir_style = ParagraphStyle(
    "StageDir", parent=styles["Normal"],
    fontSize=10, textColor=STAGE_CLR, spaceAfter=2*mm,
    fontName="Rajdhani", leftIndent=8*mm
)
note_heading_style = ParagraphStyle(
    "NoteHeading", parent=styles["Heading2"],
    fontSize=13, textColor=NOTE_HEAD, spaceBefore=6*mm, spaceAfter=3*mm,
    fontName="Rajdhani-Bold"
)
note_body_style = ParagraphStyle(
    "NoteBody", parent=styles["Normal"],
    fontSize=10, textColor=NOTE_CLR, spaceAfter=1.5*mm,
    fontName="Rajdhani", leftIndent=6*mm, bulletIndent=3*mm
)
timestamp_style = ParagraphStyle(
    "Timestamp", parent=styles["Normal"],
    fontSize=10, textColor=TS_CLR, spaceAfter=1.5*mm,
    fontName="Rajdhani-SemiBold", leftIndent=8*mm
)


def dialogue(speaker, text):
    if speaker == "z3r0day":
        return Paragraph(f'<b>z3r0day:</b>  \u201c{text}\u201d', z3r0_style)
    else:
        return Paragraph(f'<b>ramless_:</b>  \u201c{text}\u201d', ramless_style)


def stage(text):
    return Paragraph(f"[{text}]", stage_dir_style)


def context(text):
    return Paragraph(text, context_style)


def trigger(text):
    return Paragraph(f"Trigger: {text}", trigger_style)


def section(text):
    return Paragraph(text, section_style)


def note_h(text):
    return Paragraph(text, note_heading_style)


def note(text):
    return Paragraph(f"\u2022 {text}", note_body_style)


def ts(text):
    return Paragraph(text, timestamp_style)


def hr():
    return HRFlowable(width="100%", thickness=0.5, color=HR_CLR, spaceAfter=4*mm, spaceBefore=4*mm)


story = []

# Title page
story.append(Spacer(1, 60*mm))
story.append(Paragraph("POLARITY", title_style))
story.append(Paragraph("Demo Dialogue Script", subtitle_style))
story.append(Spacer(1, 10*mm))
story.append(Paragraph("CONFIDENTIAL", conf_style))
story.append(PageBreak())

# --- Section 1 ---
story.append(section("1. BEACH \u2014 Arrival + Tutorial Setup"))
story.append(context("Player spawns on a tropical island beach. Disoriented. z3r0day\u2019s voice comes through comms."))
story.append(Spacer(1, 2*mm))
story.append(dialogue("z3r0day", "yo. you good?"))
story.append(dialogue("z3r0day", "okay so... slight change of plans"))
story.append(dialogue("z3r0day", "i may have not told you the whole thing"))
story.append(dialogue("ramless_", "dude. where am i"))
story.append(dialogue("z3r0day", "pocket dimension. private islands. rich people stuff"))
story.append(dialogue("z3r0day", "look i didn\u2019t wanna freak you out before you agreed"))
story.append(dialogue("ramless_", "AGREED? you said join a call!"))
story.append(dialogue("z3r0day", "technically you did join. just... physically"))
story.append(dialogue("ramless_", "..."))
story.append(dialogue("z3r0day", "look there\u2019s a port building behind you. i stashed something there"))
story.append(dialogue("z3r0day", "go through the back entrance. there\u2019s a suit"))
story.append(dialogue("ramless_", "a suit"))
story.append(dialogue("z3r0day", "trust me. you\u2019ll need it"))
story.append(trigger("Auto-trigger after player spawn, lines appear with delays."))
story.append(hr())

# --- Section 2 ---
story.append(section("2. PORT BUILDING \u2014 Back Entrance + Suit Discovery"))
story.append(context("Player finds back entrance to port service area. Dark corridors, cables, hacker\u2019s improvised equipment."))
story.append(Spacer(1, 2*mm))
story.append(stage("Player finds back entrance"))
story.append(dialogue("z3r0day", "through here. watch your head"))
story.append(Spacer(1, 2*mm))
story.append(stage("Player finds the suit in the stash"))
story.append(dialogue("z3r0day", "put it on"))
story.append(Spacer(1, 2*mm))
story.append(stage("Player equips suit \u2014 dramatic moment, lighting, SFX"))
story.append(dialogue("z3r0day", "okay now the real talk"))
story.append(dialogue("z3r0day", "this place is guarded by AI. they call them clankers"))
story.append(dialogue("z3r0day", "trained on the worst content imaginable"))
story.append(dialogue("z3r0day", "they\u2019re... not smart. but there\u2019s a lot of them"))
story.append(dialogue("ramless_", "zero i swear to god"))
story.append(trigger("Proximity trigger at entrance + interaction with suit."))
story.append(hr())

# --- Section 3 ---
story.append(section("3. PORT BUILDING \u2014 Movement Tutorial"))
story.append(context("Main port building. Bright, open. Tight corridors ideal for wallrun training."))
story.append(Spacer(1, 2*mm))
story.append(dialogue("z3r0day", "just try the wallrun. see that wall?"))
story.append(stage("TUTORIAL: wallrun prompt \u2014 player performs wallrun"))
story.append(dialogue("z3r0day", "nice. now try the dash"))
story.append(stage("TUTORIAL: dash prompt \u2014 player performs dash"))
story.append(dialogue("z3r0day", "alright you\u2019re not completely hopeless"))
story.append(dialogue("ramless_", "why am i doing this again"))
story.append(dialogue("z3r0day", "ram. lots of it. more than you\u2019ve ever seen"))
story.append(dialogue("ramless_", "..."))
story.append(dialogue("ramless_", "please zero i need this. my pc is kinda ramless"))
story.append(dialogue("z3r0day", "that\u2019s the spirit"))
story.append(trigger("Each line after successful tutorial prompt completion."))
story.append(hr())

# --- Section 4 ---
story.append(section("4. PORT BUILDING \u2014 First Combat"))
story.append(context("Player enters main port hall. First encounter with clankers."))
story.append(Spacer(1, 2*mm))
story.append(stage("Clankers appear"))
story.append(dialogue("z3r0day", "heads up. clankers"))
story.append(dialogue("z3r0day", "use the polarity system. match their charge"))
story.append(stage("TUTORIAL: combat + polarity \u2014 player destroys clankers"))
story.append(dialogue("z3r0day", "see? brainrot machines. their last thoughts are memes"))
story.append(dialogue("ramless_", "did that one just say bombardiro crocodilo"))
story.append(dialogue("z3r0day", "told you. worst content imaginable"))
story.append(trigger("Spawn trigger on hall entry + lines after first kill."))
story.append(hr())

# --- Section 5 ---
story.append(section("5. JUNGLE PATH \u2014 Approach to Mansion"))
story.append(context("Player exits port. Neon-lit wooden boardwalk leads through jungle to mansion on the hill."))
story.append(Spacer(1, 2*mm))
story.append(dialogue("z3r0day", "mansion\u2019s up the hill. that\u2019s where the good stuff is"))
story.append(dialogue("z3r0day", "first RAM cache should be inside"))
story.append(dialogue("ramless_", "it better be worth getting teleported to another dimension"))
story.append(dialogue("z3r0day", "oh it is"))
story.append(trigger("Proximity trigger at port exit."))
story.append(hr())

# --- Section 6 ---
story.append(section("6. MANSION \u2014 Raid + First Upgrade"))
story.append(context("Three-story villa. Combat on terraces and interiors. RAM vault in basement."))
story.append(Spacer(1, 2*mm))
story.append(stage("Player enters mansion"))
story.append(dialogue("z3r0day", "clear the floors. RAM vault should be in the basement"))
story.append(stage("Player clears floors, finds upgrade"))
story.append(dialogue("z3r0day", "nice. that\u2019s a suit upgrade. install it"))
story.append(stage("Player receives upgrade"))
story.append(dialogue("z3r0day", "feeling it?"))
story.append(dialogue("ramless_", "okay this is actually sick"))
story.append(dialogue("z3r0day", "told you. now there\u2019s a datacenter on the other side of the island"))
story.append(dialogue("z3r0day", "that\u2019s the real target"))
story.append(trigger("Proximity + interaction triggers at entrance and upgrade."))
story.append(hr())

# --- Section 7 ---
story.append(section("7. DATACENTER \u2014 Final Push"))
story.append(context("Datacenter \u2014 large technical building. Server racks, neon lighting, heavy combat."))
story.append(Spacer(1, 2*mm))
story.append(stage("Player enters datacenter"))
story.append(dialogue("z3r0day", "main hall. destroy the nodes, grab the core RAM unit from the center"))
story.append(dialogue("z3r0day", "that thing has enough data to make this whole trip worth it"))
story.append(stage("Player clears hall, approaches central RAM unit"))
story.append(dialogue("ramless_", "got it. downloading ram dot exe"))
story.append(dialogue("z3r0day", "hilarious. sending it to me now"))
story.append(trigger("Proximity trigger at entrance + interaction with RAM unit."))
story.append(hr())

# --- Section 8 ---
story.append(PageBreak())
story.append(section("8. ENDING SEQUENCE \u2014 Cinematic (max 45 seconds)"))
story.append(context("Player loses control. Camera goes cinematic. Music fades to ambient."))
story.append(Spacer(1, 3*mm))

story.append(ts("[0:00]"))
story.append(stage("Screen glitches slightly. Player loses control"))
story.append(dialogue("z3r0day", "..."))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:03]"))
story.append(stage("Pause. Silence"))
story.append(dialogue("z3r0day", "okay that was fast"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:06]"))
story.append(dialogue("ramless_", "what"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:08]"))
story.append(dialogue("z3r0day", "i\u2019m in. decrypting now"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:12]"))
story.append(stage("3 second pause. Music drops to quiet ambient"))
story.append(dialogue("z3r0day", "oh no"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:16]"))
story.append(dialogue("ramless_", "what. what is it"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:18]"))
story.append(dialogue("z3r0day", "this isn\u2019t just RAM storage"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:21]"))
story.append(dialogue("z3r0day", "these are files. real files. names. dates. everything"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:25]"))
story.append(dialogue("ramless_", "files about what"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:27]"))
story.append(dialogue("z3r0day", "about THEM. the owners. what they\u2019ve been doing"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:31]"))
story.append(stage("3 second pause"))
story.append(dialogue("z3r0day", "we know too much now"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:35]"))
story.append(dialogue("z3r0day", "both of us"))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:38]"))
story.append(dialogue("ramless_", "zero..."))
story.append(Spacer(1, 2*mm))

story.append(ts("[0:40]"))
story.append(dialogue("z3r0day", "we\u2019re cooked bro"))
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

# --- Director's Notes ---
story.append(PageBreak())
story.append(section("DIRECTOR\u2019S NOTES"))

story.append(note_h("Ending Sequence"))
story.append(note("Longest pause between \u201coh no\u201d and \u201cthis isn\u2019t just RAM storage\u201d \u2014 tension builds through silence"))
story.append(note("z3r0day\u2019s tone shifts in the finale \u2014 from relaxed hacker to genuinely scared"))
story.append(note("\u201cwe\u2019re cooked bro\u201d \u2014 final line, delivered quietly, no irony. A brainrot term said dead serious"))
story.append(note("SMASH CUT must be abrupt \u2014 no fade-out. Black screen instantly"))
story.append(note("Title card appears after 1 second of silence on black"))

story.append(note_h("Dialogue System"))
story.append(note("All lines triggered via Level Sequence or Blueprint triggers"))
story.append(note("z3r0day \u2014 voice over comms only (slightly distorted, comms filter)"))
story.append(note("ramless_ \u2014 first-person voice (clean, no filter)"))
story.append(note("Subtitles displayed in chat style (JetBrains Mono font, same colors)"))

story.append(note_h("Sound Design Key Moments"))
story.append(note("Teleportation to beach \u2014 sharp sonic impact + silence + waves"))
story.append(note("Equipping suit \u2014 power-up sound, energy hum"))
story.append(note("First clanker kill \u2014 brainrot text pop + vine boom"))
story.append(note("\u201coh no\u201d \u2014 music cuts completely within 1 second"))
story.append(note("SMASH CUT \u2014 all audio cuts abruptly"))

# --- Timing Table ---
story.append(note_h("Timing Estimates"))

table_data = [
    ["Section", "Est. Duration"],
    ["Beach arrival", "30\u201340 sec"],
    ["Port back entrance", "20\u201330 sec"],
    ["Movement tutorial", "1\u20132 min (player-dependent)"],
    ["First combat", "1\u20132 min"],
    ["Jungle path", "15\u201320 sec"],
    ["Mansion raid", "5\u20138 min"],
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

# Build
doc = SimpleDocTemplate(
    OUTPUT,
    pagesize=A4,
    topMargin=20*mm,
    bottomMargin=20*mm,
    leftMargin=20*mm,
    rightMargin=20*mm,
    title="POLARITY \u2014 Demo Dialogue Script",
    author="Polarity Team"
)
doc.build(story)
print(f"Created {OUTPUT}")
