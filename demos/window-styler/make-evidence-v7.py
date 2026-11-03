"""Build a local evidence report and montage from actual screen captures."""
from pathlib import Path
from datetime import datetime
import json
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
captures = root / 'out/live-v7/release-screenshots'
report = json.loads((captures / 'report.json').read_text(encoding='utf-8-sig'))
inventory = json.loads((captures / 'inventory-screenshots.json').read_text(encoding='utf-8-sig'))
inputs = json.loads((root / 'out/live-v7/input-final/results.json').read_text(encoding='utf-8-sig'))
lookup = {w['hwnd']: w for w in inventory['windows']}
revision = (root / 'out/build-x64/runtime-Release.txt').read_text().splitlines()[0]

def introduced_solid_regions(before_file, after_file):
    """Catch a lost client surface even when frame geometry and lights pass."""
    with Image.open(before_file) as before, Image.open(after_file) as after:
        w, h = min(before.width, after.width), min(before.height, after.height)
        regions = {'left-frame': (2, 160, 9, h-16), 'right-frame': (w-9, 160, w-2, h-16), 'bottom-frame': (16, h-9, w-16, h-2), 'body': (16, 160, w-16, h-16),
                   'right': (max(16, w-46), 160, w-12, h-16),
                   'bottom': (16, max(160, h-46), w-16, h-12)}
        findings = []
        for name, box in regions.items():
            if box[2] <= box[0] or box[3] <= box[1]:
                continue
            fractions = []
            for source in (before, after):
                pixels = list(source.crop(box).convert('RGB').resize((32,32)).get_flattened_data())
                fractions.append((sum(max(p)<=10 for p in pixels)/1024,
                                  sum(min(p)>=245 for p in pixels)/1024))
            if any(fractions[1][i] >= .99 and fractions[0][i] < .90 for i in (0,1)):
                findings.append(name)
        return findings

for row in report['windows']:
    original = lookup[row['hwnd']]
    reason = row.get('reason','')
    if row['status'] == 'unsupported' and (original['geometry']['cloaked'] or any(s in reason for s in ('桌面、任务栏','工具 / 托盘','控制器自身'))):
        row['status'] = 'excluded'
    if row['status'] == 'captured':
        directory = captures / row['directory']
        findings = {state: introduced_solid_regions(directory/'before.png',directory/(state+'.png')) for state in ('after','stable','unmaximized')}
        row['checks']['introducedSolidRegions'] = findings
        if row['checks']['inlineKind'] != 6:
            findings = {state: [r for r in regions if not r.endswith('-frame')] for state, regions in findings.items()}
            row['checks']['introducedSolidRegions'] = findings
        if any(findings.values()):
            row['status'] = 'failed'
            row['reason'] = 'A previously rendered client region became a solid blank surface.'
        row['inputChecks'] = next((r for r in inputs if r['hwnd']==row['hwnd']),None)

report.update(captured=sum(w['status']=='captured' for w in report['windows']),
              failed=sum(w['status']=='failed' for w in report['windows']),
              unsupported=sum(w['status'].startswith('unsupported') for w in report['windows']),
              excluded=sum(w['status']=='excluded' for w in report['windows']),
              runtime=revision,
              contentChecks='Screenshots compared for introduced solid client regions; full application captures visually reviewed.')
(captures/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
summary=f"直接注入截图通过 {report['captured']}；失败 {report['failed']}；未适配 {report['unsupported']}；排除系统/后台窗口 {report['excluded']}。"
(captures/'summary.txt').write_text(summary,encoding='utf-8')
lines=['# v7 通用框架最终构建逐窗口检查',summary,'管理员权限；150% 缩放。真实桌面采集，没有使用 Computer Use。',f'运行时：`{revision}`。',
       '检查应用前、首次应用约 100ms、稳定后和还原后。检查三色像素、客户区尺寸、无新增子 HWND、无覆盖层和还原几何；另比较右侧、底部及主体是否变成新的纯色空白，并人工查看完整画面。',
       '原生非客户区切换可能改变外框厚度，要求客户区和撤销后的几何不变。原控件树适配要求外框也不变。红灯关闭只测试自有示例。']
for row in report['windows']:
    lines += [f"## {row['title']} ({row['hwnd']})",f"{row.get('framework','')} · {row['status']} · {row.get('reason','')}"]
    if row.get('directory'):
        directory=row['directory']
        if row['status'] in ('captured','failed'):
            c=row['checks']
            lines += [f"适配器 {c['adapter']}；附加确认 {c.get('attachAcknowledgementMs','?')}ms；客户区保持 {c['clientPreserved']}；还原几何 {c.get('restoredGeometry')}；新纯色空白区域 {c.get('introducedSolidRegions',{})}。",
                      f"按钮实测：`{json.dumps(row.get('inputChecks'),ensure_ascii=False)}`。",
                      f'[应用前]({directory}/before.png) · [首次应用]({directory}/after.png) · [稳定后]({directory}/stable.png) · [还原后]({directory}/restored.png)',
                      f'![原区域注入效果]({directory}/after-title.png)']
        else:
            lines += [f'[原始画面，未应用适配]({directory}/before.png)']
(captures/'report.md').write_text('\n\n'.join(lines)+'\n',encoding='utf-8')

font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',24)
small=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',18)
rows=[w for w in report['windows'] if w['status']=='captured']
width=1500
canvas=Image.new('RGB',(width,125+len(rows)*270),'#14171d')
draw=ImageDraw.Draw(canvas)
draw.text((20,15),'Window Injector v7 · 当前真实窗口截图',font=font,fill='white')
draw.text((20,55),f"通过 {report['captured']}；未适配 {report['unsupported']}（逐窗口结果见报告）；系统/后台排除 {report['excluded']}",font=small,fill='#ffce82')
y=96
labels={'0x6b0612':'Codex / Electron 原菜单行','0x309f2':'QQ / Electron 原操作按钮','0x4f07ae':'Visual Studio / WPF 原菜单区域','0x2b0f2e':'调优工具 / WPF 原标题区域','0x3d156c':'控制面板 / Win32 已有非客户区','0x2094a':'Windows Terminal / 原 XAML 标签行','0x9b062a':'服务端控制台 / conhost 已有非客户区（重开后补测）'}
for row in rows:
    draw.text((20,y),labels.get(row['hwnd'],row['title']),font=font,fill='#bdd4ff');y+=35
    for state,label in [('before','应用前'),('after','直接注入后')]:
        draw.text((20,y),label,font=small,fill='#d2d7df');y+=27
        with Image.open(captures/row['directory']/(state+'-title.png')) as shot:
            height=42 if row['hwnd']=='0x9b062a' else 74
            shot=shot.convert('RGB').crop((0,0,shot.width,min(height,shot.height)))
            if shot.width>width-40:shot=shot.resize((width-40,round(shot.height*(width-40)/shot.width)))
            canvas.paste(shot,(20,y));y+=shot.height+10
    y+=10
output=root/'images/window-injector-v7.png';output.parent.mkdir(exist_ok=True)
canvas.crop((0,0,width,y+10)).save(output)
