"""Assemble actual desktop captures; no generated application visuals."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
captures = root / 'out/live-v5/final-screenshots'
report = json.loads((captures / 'report.json').read_text(encoding='utf-8-sig'))
rows = [r for r in report['windows'] if r['status'] == 'captured']
font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 23)
small = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 17)
canvas = Image.new('RGB', (1510, 720), '#14171d')
draw = ImageDraw.Draw(canvas)
draw.text((20, 12), 'Actual desktop applications / original title-row injection', font=font, fill='white')
y = 52
for row in rows:
    title = 'Visual Studio / WPF / CLR v4' if 'Visual Studio' in row['title'] else 'Universal x86 Tuning Utility / WPF / CoreCLR .NET 8'
    draw.text((20, y), title, font=font, fill='#bdd4ff')
    y += 35
    for state, label in [('before', 'Before'), ('after', 'After: original buttons collapsed; original content shifted right')]:
        draw.text((20, y), label, font=small, fill='#c1c7d2')
        y += 25
        shot = Image.open(captures / row['directory'] / (state + '-title.png')).convert('RGB')
        shot = shot.crop((0, 0, shot.width, min(110, shot.height)))
        canvas.paste(shot, (20, y))
        y += shot.height + 12
    y += 10
output = root / 'images/window-injector-v5.png'
output.parent.mkdir(exist_ok=True)
canvas.crop((0, 0, canvas.width, min(y + 12, canvas.height))).save(output)
print(output)
