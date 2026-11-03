"""Derive v8 evidence from native desktop captures, preserving failed results."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
captures = root / 'out/live-v8/release-screenshots'
report = json.loads((captures / 'report.json').read_text(encoding='utf-8-sig'))
inputs_file = root / 'out/live-v8/input-final/results.json'
inputs = json.loads(inputs_file.read_text(encoding='utf-8-sig')) if inputs_file.exists() else []
runtime = (root / 'out/build-x64/runtime-Release.txt').read_text().splitlines()[0]
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 23)
small = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 17)
images = root / 'images'
images.mkdir(exist_ok=True)


def visible_box(frame, size):
    box = frame['regionBox'] if frame['regionType'] > 0 else frame['visibleFrame']
    dx, dy = min(0, frame['rect'][0]), min(0, frame['rect'][1])
    result = (max(0, box[0]+dx), max(0, box[1]+dy),
              min(size[0], box[2]+dx), min(size[1], box[3]+dy))
    return result if result[2] > result[0] and result[3] > result[1] else (0, 0, *size)


def stats(image, box):
    if box[2] <= box[0] or box[3] <= box[1]:
        return {'black': 0, 'white': 0}
    pixels = list(image.crop(box).convert('RGB').resize((48, 48)).get_flattened_data())
    return {'black': round(sum(max(p) <= 10 for p in pixels)/len(pixels), 4),
            'white': round(sum(min(p) >= 240 for p in pixels)/len(pixels), 4)}


def samples(path, frame):
    with Image.open(path) as shot:
        l, t, r, b = visible_box(frame, shot.size)
        boxes = {
            'top-frame': (l+80, t+1, r-80, t+3),
            'left-frame': (l+1, t+70, l+3, b-25),
            'right-frame': (r-3, t+70, r-1, b-25),
            'bottom-frame': (l+25, b-3, r-25, b-1),
            'client-body': (l+20, t+160, r-20, b-20),
            'client-top': (l+20, t+50, r-20, t+85),
            'client-right': (r-45, t+160, r-12, b-20),
            'client-bottom': (l+20, b-45, r-20, b-12),
        }
        dx, dy = min(0, frame['rect'][0]), min(0, frame['rect'][1])
        for index, bar in enumerate(frame.get('scrollbars', [])):
            boxes[f'native-scrollbar-{index}'] = (max(0,bar[0]+dx), max(0,bar[1]+dy),
                                                 min(shot.width,bar[2]+dx), min(shot.height,bar[3]+dy))
        return {name: stats(shot, box) for name, box in boxes.items()}


for row in report['windows']:
    if row['status'] != 'captured':
        continue
    directory = captures / row['directory']
    checks = row['checks']
    baseline = samples(directory/'before.png', checks['frames']['before'])
    measures, findings = {}, {}
    for phase in ('after', 'stable', 'unmaximized', 'restored'):
        measures[phase] = samples(directory/(phase+'.png'), checks['frames'][phase])
        findings[phase] = [name for name, values in measures[phase].items()
                           if name in baseline and
                           (any(values[color] >= .99 and baseline[name][color] < .85 for color in ('black', 'white'))
                            or (name.startswith('native-scrollbar') and values['white'] > .5 and baseline[name]['white'] < .1))]
    checks['edgeAndClientPixels'] = {'before': baseline, **measures}
    checks['introducedSolidRegions'] = findings
    row['inputChecks'] = next((entry for entry in inputs if entry['hwnd'] == row['hwnd']), None)
    if any(findings.values()):
        row['status'] = 'failed'
        row['reason'] = 'A previously rendered visible frame/client area became a solid blank surface.'

report.update(captured=sum(row['status'] == 'captured' for row in report['windows']),
              failed=sum(row['status'] == 'failed' for row in report['windows']),
              unsupported=sum(row['status'].startswith('unsupported') for row in report['windows']),
              excluded=sum(row['status'] == 'excluded' for row in report['windows']), runtime=runtime)
(captures/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
summary = f"直接注入截图通过 {report['captured']}；失败 {report['failed']}；未适配 {report['unsupported']}；排除系统/后台窗口 {report['excluded']}。"
(captures/'summary.txt').write_text(summary, encoding='utf-8')
lines = ['# v8 逐窗口截图验证', summary, f'运行时：`{runtime}`。',
         'Windows 11 build 26100，150% DPI，管理员权限。使用原生屏幕复制，没有使用 Computer Use。',
         '检查首次应用、稳定显示、最大化、还原大小与撤销。检查三灯像素、原按钮隐藏、客户区、外框、区域形状、DWM 状态及撤销残留。',
         '四边像素检查采用各窗口实际可见边界：Win32 使用 regionBox，保留 DWM 的后端使用 extended frame bounds；不把透明缩放边距当作白边。不因后端类型跳过边框检查。纯色检测只定位明显缺失的表面，还需查看完整截图与四角放大图。']
for row in report['windows']:
    lines += [f"## {row['title']} ({row['hwnd']})",
              f"{row.get('framework', '')} · {row['status']} · {row.get('reason', '')}"]
    if not row.get('directory'):
        continue
    directory = row['directory']
    if row['status'] in ('captured', 'failed'):
        c = row['checks']
        lines += [f"附加确认 {c.get('attachAcknowledgementMs')}ms；客户区保持 {c['clientPreserved']}；外框保持 {c['outerPreserved']}；形状检查 {c['shapeChecks']}；撤销边框恢复 {c['frameRestored']}；撤销像素清除 {c['buttonPixelsCleared']}。",
                  f"新增纯色区域：`{json.dumps(c.get('introducedSolidRegions'), ensure_ascii=False)}`。",
                  f"输入验证：`{json.dumps(row.get('inputChecks'), ensure_ascii=False)}`。",
                  ' · '.join(f'[{label}]({directory}/{phase}.png)' for phase, label in
                             [('before', '应用前'), ('after', '首次显示'), ('stable', '稳定后'),
                              ('maximized', '最大化'), ('unmaximized', '还原大小'), ('restored', '撤销')]),
                  f'![四角放大]({directory}/corners.png)']
        # A contact sheet retains full images, with a separate native-resolution
        # corner panel so fit-to-page scaling cannot hide frame artifacts.
        sheet = Image.new('RGB', (1260, 1220), '#14171d')
        draw = ImageDraw.Draw(sheet)
        draw.text((16, 10), row['hwnd']+' · '+row['framework'], font=font, fill='white')
        corner_sheet = Image.new('RGB', (1000, 615), '#14171d')
        cd = ImageDraw.Draw(corner_sheet)
        for index, (phase, label) in enumerate([('before','应用前'), ('after','首次显示'), ('unmaximized','还原大小'), ('restored','撤销')]):
            with Image.open(captures/directory/(phase+'.png')) as shot:
                thumb = shot.convert('RGB'); thumb.thumbnail((610, 540))
                x, y = 15+(index % 2)*625, 50+(index//2)*575
                draw.text((x, y), label, font=small, fill='#bdd4ff')
                sheet.paste(thumb, (x, y+28))
                l, t, r, b = visible_box(c['frames'][phase], shot.size)
                cd.text((10, index*150+5), label, font=small, fill='white')
                boxes = [(l,t,l+28,t+28), (r-28,t,r,t+28), (l,b-28,l+28,b), (r-28,b-28,r,b)]
                for j, box in enumerate(boxes):
                    corner_sheet.paste(shot.crop(box).resize((112,112), Image.Resampling.NEAREST), (145+j*210,index*150+28))
        sheet.save(captures/directory/'contact.png')
        corner_sheet.save(captures/directory/'corners.png')
    else:
        lines += [f'[未接管的原始窗口]({directory}/before.png)']
(captures/'report.md').write_text('\n\n'.join(lines)+'\n', encoding='utf-8')

rows = [row for row in report['windows'] if row['status'] == 'captured']
canvas = Image.new('RGB', (1500, 110+len(rows)*270), '#14171d')
draw = ImageDraw.Draw(canvas)
draw.text((20, 15), 'Window Injector v8 · 当前窗口真实截图', font=font, fill='white')
draw.text((20, 55), summary, font=small, fill='#ffce82')
y = 100
for row in rows:
    label = row['framework']+' · '+row['title']
    draw.text((20,y), label[:92], font=small, fill='#bdd4ff'); y += 30
    for phase, label in [('before','应用前'), ('after','注入后')]:
        draw.text((20,y), label, font=small, fill='#d2d7df'); y += 25
        with Image.open(captures/row['directory']/(phase+'-title.png')) as shot:
            strip = shot.convert('RGB').crop((0,0,shot.width, min(55 if row['framework'] == 'Console host' else 65, shot.height)))
            strip.thumbnail((1450, 65))
            canvas.paste(strip, (20,y)); y += strip.height+7
    y += 10
canvas.crop((0,0,1500,y+10)).save(images/'window-injector-v8.png')

selected = [next((row for row in rows if row['framework'] == framework), None)
            for framework in ('Win32 Shell', 'NetUI / Ribbon')]
frame_sheet = Image.new('RGB', (1280, 1290), '#14171d')
fd = ImageDraw.Draw(frame_sheet)
fd.text((20, 12), 'v8 · Win32 四边与资源管理器系统圆角', font=font, fill='white')
for index, row in enumerate(selected):
    if row is None:
        continue
    top = 55+index*610
    fd.text((20,top), row['framework']+' · 首次应用 / 还原大小', font=small, fill='#bdd4ff')
    for column, phase in enumerate(('after','unmaximized')):
        with Image.open(captures/row['directory']/(phase+'.png')) as shot:
            shot = shot.convert('RGB'); shot.thumbnail((615, 425))
            frame_sheet.paste(shot, (20+column*630, top+28))
    with Image.open(captures/row['directory']/'corners.png') as corner:
        frame_sheet.paste(corner.crop((0,150,1000,300)), (20,top+445))
frame_sheet.save(images/'window-frame-v8.png')
print(summary)
if report['failed']:
    raise SystemExit(1)
