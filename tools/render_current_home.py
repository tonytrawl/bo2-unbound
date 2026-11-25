"""Deterministic 1280x720 preview of the current Ui::home implementation."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "previews" / "current-wii-u-home.png"
W, H = 1280, 720

WHITE = (255, 255, 255, 255)
MUTED = (102, 114, 122, 255)
ORANGE = (255, 92, 0, 255)
GREEN = (46, 174, 119, 255)
DARK = (10, 12, 14, 255)
PANEL = (18, 21, 23, 248)
PANEL_RAISED = (27, 31, 34, 255)
PANEL_BORDER = (77, 84, 89, 255)
LIGHT_TEXT = (205, 211, 215, 255)

REGULAR = ROOT / "content" / "fonts" / "NotoSans-Regular.ttf"
BOLD = ROOT / "content" / "fonts" / "NotoSans-Bold.ttf"
F18 = ImageFont.truetype(str(REGULAR), 18)
F20 = ImageFont.truetype(str(BOLD), 20)
F22 = ImageFont.truetype(str(REGULAR), 22)
F28 = ImageFont.truetype(str(BOLD), 28)
F38 = ImageFont.truetype(str(BOLD), 38)

canvas = Image.new("RGBA", (W, H), (8, 10, 11, 255))
d = ImageDraw.Draw(canvas)


def rr(box, radius, fill):
    d.rounded_rectangle(box, radius=radius, fill=fill)


def measured(text, font):
    box = d.textbbox((0, 0), text, font=font)
    return box[2] - box[0], box[3] - box[1]


def clipped_text(text, xy, font, fill, max_width=0, centered=False):
    shown = text
    if max_width:
        while len(shown) > 4 and measured(shown, font)[0] > max_width:
            shown = shown[:-1]
        if shown != text:
            shown += "..."
    x, y = xy
    if centered:
        x -= measured(shown, font)[0] // 2
    d.text((x, y), shown, font=font, fill=fill)


def wrapped(text, box, font, fill):
    x, y, width, height = box
    words = text.replace("\n", " \n ").split(" ")
    line = ""
    cursor_y = y
    line_h = font.size + 7
    for word in words:
        if word == "\n":
            if line:
                d.text((x, cursor_y), line.rstrip(), font=font, fill=fill)
            line, cursor_y = "", cursor_y + line_h
            continue
        candidate = word if not line else line + " " + word
        if measured(candidate, font)[0] > width and line:
            d.text((x, cursor_y), line, font=font, fill=fill)
            line, cursor_y = word, cursor_y + line_h
        else:
            line = candidate
        if cursor_y + line_h > y + height:
            break
    if line and cursor_y + line_h <= y + height:
        d.text((x, cursor_y), line, font=font, fill=fill)


def pill(value, x, y, fill):
    width = measured(value, F20)[0] + 28
    rr((x, y, x + width, y + 36), 18, fill)
    clipped_text(value, (x + 14, y + 4), F20, WHITE)


def backdrop():
    d.rectangle((0, 0, W, H), fill=(8, 10, 11, 255))
    for x in range(0, W, 80):
        d.line((x, 102, x, H), fill=(34, 39, 42, 120))
    for y in range(102, H, 64):
        d.line((0, y, W, y), fill=(34, 39, 42, 120))
    for x in range(-500, W, 180):
        d.line((x, 658, x + 560, 102), fill=(255, 92, 0, 22))


def header():
    d.rectangle((0, 0, W, 102), fill=DARK)
    d.rectangle((0, 98, W, 102), fill=ORANGE)
    logo = Image.open(ROOT / "content" / "assets" / "unbound_short.png").convert("RGBA")
    logo = logo.resize((220, 53), Image.Resampling.LANCZOS)
    canvas.alpha_composite(logo, (24, 10))
    clipped_text("0.0.09-beta • r9", (134, 70), F18, ORANGE, 238, centered=True)
    d.line((277, 18, 277, 84), fill=PANEL_BORDER)
    clipped_text("Unbound HQ", (300, 36), F18, LIGHT_TEXT, 900)


def action_card(x, eyebrow, title, detail, selected, accent, enabled=True):
    y, width, height = 438, 291, 190
    border = accent if selected and enabled else (MUTED if selected else PANEL_BORDER)
    extra = 3 if selected else 0
    outer = (x - extra, y - extra, x + width + extra, y + height + extra)
    rr(outer, 12, border)
    rr((outer[0] + 2, outer[1] + 2, outer[2] - 2, outer[3] - 2), 10,
       (49, 25, 14, 255) if selected and enabled else PANEL)
    rr((x, y, x + 7, y + height), 3, accent if enabled else MUTED)
    clipped_text(eyebrow, (x + 25, y + 18), F20, accent if enabled else MUTED, width - 46)
    clipped_text(title, (x + 25, y + 48), F28, WHITE if enabled else MUTED, width - 46)
    wrapped(detail, (x + 25, y + 90, width - 46, 78), F18, LIGHT_TEXT if enabled else MUTED)


def footer():
    actions = (("A", "Select"), ("D-PAD", "Navigate"), ("+", "Exit"))
    d.rectangle((0, 658, W, 720), fill=(15, 18, 20, 248))
    column = W // len(actions)
    for i, (key, label) in enumerate(actions):
        key_width = measured(key, F20)[0]
        label_width = measured(label, F18)[0]
        badge_width = max(38, key_width + 20)
        label_room = max(0, column - badge_width - 34)
        content_width = badge_width + 12 + min(label_room, label_width)
        start = i * column + max(14, (column - content_width) // 2)
        rr((start, 672, start + badge_width, 706), 10, (0, 159, 207, 255))
        clipped_text(key, (start + badge_width // 2, 675), F20, WHITE, centered=True)
        clipped_text(label, (start + badge_width + 12, 676), F18, WHITE, label_room)


backdrop()
header()

# Exact Ui::home geometry. Initial focus is the Latest Update panel (homeAction == 0).
news = (28, 121, 773, 415)
rr(news, 14, ORANGE)
rr((31, 124, 770, 412), 12, PANEL)
art = Image.open(ROOT / "content" / "assets" / "fixed_logo.png").convert("RGBA")
art = art.resize((178, 178), Image.Resampling.LANCZOS)
canvas.alpha_composite(art, (50, 150))
pill("LATEST UPDATE", 255, 146, ORANGE)
clipped_text("Unbound dispatch", (255, 194), F38, WHITE, 475)
preview = "The latest Unbound dispatch is ready. Press A to open the complete update notes."
wrapped(preview, (255, 246, 475, 104), F22, LIGHT_TEXT)
clipped_text("A  READ FULL UPDATE", (255, 372), F20, ORANGE)

rr((793, 121, 1252, 415), 14, PANEL_BORDER)
rr((796, 124, 1249, 412), 12, PANEL)
clipped_text("Your Black Ops II", (820, 142), F28, WHITE)
d.line((820, 178, 1223, 178), fill=PANEL_BORDER)
rows = (
    ("GAME", "USB storage", GREEN),
    ("UPDATE", "USB storage", WHITE),
    ("REGION", "USA", WHITE),
    ("LANGUAGE", "english", WHITE),
    ("UNBOUND", "0.0.05P1.0", ORANGE),
)
for (label, value, color), y in zip(rows, (198, 235, 272, 309, 346)):
    clipped_text(label, (820, y), F18, MUTED, 125)
    clipped_text(value, (948, y - 2), F20, color, 275)

action_card(28, "UNBOUND CORE", "You're up to date", "Update Unbound core files.", False, ORANGE, True)
action_card(336, "AROMA PLUGINS", "Workshop", "Install and manage verified plugins.", False, (0, 159, 207, 255), True)
action_card(644, "LOCAL FILES", "Mod Manager", "Copy or remove local mods.", False, (0, 159, 207, 255), True)
action_card(952, "PLAY", "Launch Game", "Black Ops II via USB storage.", False, GREEN, True)
footer()

OUT.parent.mkdir(parents=True, exist_ok=True)
canvas.convert("RGB").save(OUT, quality=95)
print(OUT)
