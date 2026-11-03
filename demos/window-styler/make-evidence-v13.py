"""Assemble real desktop captures and preserve every initial application result."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
out = root / 'out/live-v13'
captures = out / 'final-desktop'

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

initial = read(out / 'baseline/report.json')
current = read(captures / 'report.json')
by_hwnd = {row['hwnd']: row for row in current['windows']}
failures = read(out / 'winui3-experimental-failures.json')
by_pid = {int(event['fields']['ProcessId'], 16): event for event in failures}
inventory = {row['hwnd']: row for row in read(out / 'inventory/inventory.json')['windows']}
scope = []
for original in initial['windows']:
    if original['status'] == 'excluded':
        continue
    hwnd = original['hwnd']
    event = by_pid.get(inventory[hwnd]['pid'])
    if event:
        scope.append(dict(hwnd=hwnd, title=original['title'], framework='WinUI 3 / Windows App SDK',
                          status='experimental-failure', reason='WinUI 3 diagnostics experiment caused exit; entry disabled.', event=event))
    elif hwnd in by_hwnd:
        row = by_hwnd[hwnd]
        scope.append({key: row.get(key) for key in ('hwnd', 'title', 'framework', 'status', 'reason', 'directory')})
    else:
        scope.append(dict(hwnd=hwnd, title=original['title'], framework=original['framework'],
                          status='unverified', reason='Initial window absent from final inventory; not counted as passed.'))

result = dict(initialApplicationCount=len(scope), captured=sum(r['status'] == 'captured' for r in scope),
              unsupported=sum(r['status'].startswith('unsupported') for r in scope),
              experimentalFailures=sum(r['status'] == 'experimental-failure' for r in scope),
              windows=scope, currentDesktopCounts={k: current[k] for k in ('captured', 'unsupported', 'failed', 'excluded')})
(out / 'scope-report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')

rows = [r for r in current['windows'] if r['status'] == 'captured']
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 24)
small = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 18)
sheet = Image.new('RGB', (1880, 80 + ((len(rows)+1)//2)*178), '#e9ebef')
draw = ImageDraw.Draw(sheet)
draw.text((25, 18), f'v13 · 实际桌面截图通过 {len(rows)} 个窗口（完整初始范围与故障见报告）', font=font, fill='#20242b')
for index, row in enumerate(rows):
    x, y = 20+(index % 2)*930, 75+(index // 2)*178
    draw.rounded_rectangle((x, y, x+910, y+162), radius=8, fill='white')
    name = row['title']
    if len(name) > 39:
        name = name[:38]+'…'
    draw.text((x+12, y+8), name, font=small, fill='#23252a')
    draw.text((x+12, y+36), row['framework'], font=small, fill='#565d68')
    with Image.open(captures/row['directory']/'after-title.png') as shot:
        shot.thumbnail((884, 92), Image.Resampling.LANCZOS)
        sheet.paste(shot, (x+12, y+65))
sheet.save(root/'images/window-desktop-v13.png')

# A second sheet retains full window edges and original application content.
whole = Image.new('RGB', (1920, 60+((len(rows)+2)//3)*390), '#e9ebef')
draw = ImageDraw.Draw(whole)
draw.text((20, 14), '实际完整窗口 · 原生屏幕采集后等比缩小', font=font, fill='#20242b')
for index, row in enumerate(rows):
    x, y = (index % 3)*640, 60+(index // 3)*390
    draw.text((x+10, y+4), row['framework']+' '+row['hwnd'], font=small, fill='#23252a')
    with Image.open(captures/row['directory']/'after.png') as shot:
        shot.thumbnail((620, 340), Image.Resampling.LANCZOS)
        whole.paste(shot, (x+10, y+36))
whole.save(out/'full-window-contact.png')
pages_dir = out/'taskmanager-views-replay-final'
if (pages_dir/'results.json').exists():
    page_checks = []
    for row in read(pages_dir/'results.json'):
        centers = []
        with Image.open(pages_dir/(row['page']+'.png')) as shot:
            title = shot.convert('RGB').crop((0, 0, 200, 120))
            for color in ((255, 95, 87), (255, 189, 46), (40, 200, 64)):
                points = [(x, y) for y in range(title.height) for x in range(title.width)
                          if all(abs(a-b) <= 2 for a, b in zip(title.getpixel((x, y)), color))]
                centers.append(dict(count=len(points), x=sum(p[0] for p in points)/len(points) if points else None,
                                    y=sum(p[1] for p in points)/len(points) if points else None))
            title.save(pages_dir/(row['page']+'-title.png'))
        visible = all(c['count'] >= 60 for c in centers)
        if visible:
            visible = all(abs(centers[i+1]['x']-centers[i]['x']-30) < 2 for i in (0, 1)) and max(c['y'] for c in centers)-min(c['y'] for c in centers) < 2
        page_checks.append(dict(page=row['page'], centers=centers,
                                passed=visible and row['attached'] and not row['systemButtons'] and row['originalButtons'] == 0))
    pages_result = dict(frameCount=len(page_checks), pageCount=(len(page_checks)-1)//2,
                        passed=len(page_checks) >= 5 and all(c['passed'] for c in page_checks), frames=page_checks)
    (pages_dir/'pixel-checks.json').write_text(json.dumps(pages_result, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Task Manager view pixels:', pages_result['passed'], pages_result['frameCount'])
print(json.dumps({k: v for k, v in result.items() if k != 'windows'}, ensure_ascii=False, indent=2))
