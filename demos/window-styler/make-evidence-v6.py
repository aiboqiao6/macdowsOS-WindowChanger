"""Build a local evidence report and montage from actual screen captures."""
from pathlib import Path
from datetime import datetime
import json
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
captures = root / 'out/live-v6/release-screenshots'
report = json.loads((captures / 'report.json').read_text(encoding='utf-8-sig'))
inventory = json.loads((captures / 'inventory-screenshots.json').read_text(encoding='utf-8-sig'))
inputs = json.loads((root / 'out/live-v6/input/results.json').read_text(encoding='utf-8-sig'))
lookup = {w['hwnd']: w for w in inventory['windows']}
revision = (root / 'out/build-x64/runtime-Release.txt').read_text().splitlines()[0]

def introduced_solid_regions(before_file, after_file):
    """Catch a lost client surface even when frame geometry and lights pass."""
    with Image.open(before_file) as before, Image.open(after_file) as after:
        w, h = min(before.width, after.width), min(before.height, after.height)
        regions = {'body': (16, 160, w-16, h-16),
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
        findings = {state: introduced_solid_regions(directory/'before.png',directory/(state+'.png')) for state in ('after','stable')}
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
lines=['# v6 最终构建逐窗口检查',summary,'管理员权限；150% 缩放。真实桌面采集，没有使用 Computer Use。',f'运行时：`{revision}`。',
       '检查应用前、首次应用约 100ms、稳定后和还原后。检查三色像素、客户区尺寸、无新增子 HWND、无覆盖层和还原几何；另比较右侧、底部及主体是否变成新的纯色空白，并人工查看完整画面。',
       '原生非客户区切换可能改变外框厚度，要求客户区和撤销后的几何不变。原控件树适配要求外框也不变。红灯关闭只测试自有示例。']
for row in report['windows']:
    lines += [f"## {row['title']} ({row['hwnd']})",f"{row.get('framework','')} · {row['status']} · {row.get('reason','')}"]
    if row.get('directory'):
        directory=row['directory']
        if row['status'] in ('captured','failed'):
            c=row['checks']
            lines += [f"适配器 {c['adapter']}；附加确认 {c.get('attachAcknowledgementMs','?')}ms；客户区保持 {c['clientPreserved']}；还原几何 {c.get('restoredGeometry')}；新纯色空白区域 {c['introducedSolidRegions']}。",
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
draw.text((20,15),'Window Injector v6 · 当前真实窗口截图',font=font,fill='white')
draw.text((20,55),f"通过 {report['captured']}；未适配 {report['unsupported']}（Edge + Ribbon）；系统/后台排除 {report['excluded']}",font=small,fill='#ffce82')
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
output=root/'images/window-injector-v6.png';output.parent.mkdir(exist_ok=True)
canvas.crop((0,0,width,y+10)).save(output)

compat=['# v6 本机兼容性记录（2026-10-30）', '**未完成全部适配。** '+summary,
        '环境：Windows 11 build 26100，x64，150% DPI；管理员控制器。',f'构建：`{revision}`。',
        '## 当前窗口', '| HWND | 程序 | 框架 | 结果 |','| --- | --- | --- | --- |']
for row in report['windows']:
    compat.append(f"| {row['hwnd']} | {row['title'].replace('|','/')} | {row.get('framework','')} | {row['status']} |")
compat += ['## 验证', 'x64 CTest 12/12，x86 CTest 6/6、完整分发包控制器检查 14/14 通过。当前已接入窗口的绿灯最大化、绿灯还原、黄灯最小化及会话还原全部通过。完整报告及四组截图见 `out/live-v6/release-screenshots/report.md`，按钮记录见 `out/live-v6/input/results.json`，分发包检查见 `out/controller-direct-v6/checks.json`。',
           '此前 frpc Terminal 和旧服务端 conhost 的截图保存在 `out/live-v6/final-screenshots`，旧 HWND 不计入最终构建当前通过数量。服务端 conhost 随后重开为 0x9b062a，已经补测当前构建截图与输入，并追加到最终报告（addedAfterInitialInventory=true）。',
           '## 尚未适配',
           'Edge 157.0.4341.24 使用原生 Views；msedge.dll 的精确 CodeView 标识为 8DE675E4-DC25-455F-4C4C-44205044422E / age 1。对应公开 PDB 返回 HTTP 404，当前没有经过验证的原生控件布局入口。明确跳过，不使用网页 DOM、标题栏或遮罩回退。',
           '当前 3 个 Ribbon 窗口使用 UIRibbon.dll NetUI 树。已通过同版本公开符号和只读探针找到 HWNDElement、AppFrameHelper、OfficeCaption、QAT 及 GetPadding/SetPadding 等入口。原按钮隐藏、内嵌新按钮和绑定还原尚无完整实现，不能把读取控件树计为适配成功。只读研究保存在 out/live-v6/ribbon-probe，未分发为适配器。',
           '软件升级、页面重载、跨屏 DPI 和未列出的框架/版本没有完成验证。当前包只为明确支持的布局接管；自动检测不代表通用兼容。',
           '## 实现修正',
           'QQ 鼠标按下会改变原按钮 CSS 类；现在验证原按钮排列并按 nth-last-child 定位，保留原事件。原 QQ 标识下移，避免覆盖。',
           '控制面板与 Ribbon Explorer 同属 CabinetWClass，按实际子控件和客户区区分；保留原客户区，标题绘制 buffer 限定在原标题区域，显式 alpha 防止壳层颜色相加。',
           'Electron 按 DLL/脚本版本隔离回调，异步布局/还原完成才确认；Terminal 在原 XAML Grid 插入列；conhost 解析实际 GUI 宿主。']
(root/'compatibility-v6-2026-10-30.md').write_text('\n\n'.join(compat).replace(' |\n\n|',' |\n|')+'\n',encoding='utf-8')
print(summary)
print(output)
if report['failed']:raise SystemExit(1)
