# MFC/BCG、Office NetUI、TXGui：框架对象与原树入口

2026-11-01，本轮推进框架入口研究，三类尚未完成可发布的三灯布局后端。没有将候选对象、DLL 加载、只读枚举或对象类型识别计为实际适配成功。正式 `adapters.ini` 未增加这三类实验注册，也未启动桌面自动接管。

| 框架 | 新验证的入口 | 本轮结果与限制 |
| --- | --- | --- |
| MFC/BCG | GetRuntimeClass、CRuntimeClass 继承链、独立 HWND 交叉关联 | 找到实际 Frame/Ribbon/StatusBar CWnd 对象；发现 8 个 CBCGPRibbonCaptionButton 候选；尚未确认活动标题按钮的布局、绘制、恢复入口 |
| Office NetUI | 虚表第 25 项的不可变类信息 getter，含 Element 动态类信息分支 | 四个 NetUIHWND 均识别；限额内取得 800 个框架对象；是有界对象引用图，不是已验证的完整可修改视觉树 |
| TXGui | Common 平台 Core → GF 窗口服务 → IGFPopupWin → GFSpy 子节点枚举 | 在原 GUI 线程读取 460 个框架节点及类型、几何、Padding、Margin、可见性；11 项只读检查通过；尚无三灯绘制和原操作按钮语义验证 |

## MFC/BCG

样本为已运行的高权限 x64 BCG Ribbon 窗口。普通权限读取返回错误 5；通过系统 UAC 启动只读脚本后完成检查，没有结束或重启目标。

`mfc-objects.py` 只读取已提交的私有可写区域，搜索目标 Frame/Ribbon/StatusBar 的精确 HWND 值。候选须通过不可变 GetRuntimeClass 入口、CRuntimeClass 名称/大小以及 CWnd → CCmdTarget → CObject 继承关系校验。

三个独立窗口一致关联到 CWnd 内的 `0x40` HWND 字段。额外两个 Tooltip 对象的 `0xa0` 字段只是引用 Ribbon/StatusBar，未当作窗口对象。后续只检查运行时类声明的 BCG 基类大小，避免读取宿主派生类新增字段。128 个 BCG 类型对象达到本轮引用图预算，不能声称已穷尽对象图。

主框架的继承链为 CMainFrame → CBCGPFrameWnd → CFrameWnd → CWnd；选择依据是标准 MFC/BCG 基类和 HWND 一致性，宿主派生类名称仅记录。对象包含 Ribbon Category、ButtonsGroup、CaptionButton、GraphicsResource 等。全部操作为外部读取，未执行目标代码、写入对象、改写 vtable 或调用布局函数。

记录：`out/live-v15/mfc-runtime-objects.json`、`mfc-runtime-scan.log`。当前尚缺原 CaptionButton 命中语义、实际活动实例、布局事务、绘制回调和撤销生命周期。

## Office NetUI

读取实际已加载模块 CodeView 后才启用解码：

| 模块 | CodeView / age |
| --- | --- |
| mso40uiwin32client.dll | `2D264595-7EA8-4318-8AA1-897691F9D8EE` / 2 |
| mso98win32client.dll | `7DA1C564-0BEE-44F1-833B-5036838211D4` / 2 |
| mso.dll | `609D7177-47D3-4508-B59D-AB1FD4FB1624` / 2 |

Office 没有可复用的 C++ RTTI，本轮改用框架自身的类信息。虚表第 25 项的常量 getter 返回类信息记录；Element 的另一个 getter 在动态类信息字段非空时返回该字段，否则返回默认记录。`office-metadata.py` 解码这两种明确指令，不在目标进程执行它们。

类信息记录中 `+0x20` 是 UTF-16 框架类型名称，`+0x18` 是基类信息；已核对 FullpageUIWindow → HWNDElement、AppFrameHelper → SimpleButton。四个实际 NetUIHWND 的对象均已识别。

报告中的边标为对象字段引用，尚未把任意引用认作视觉父子关系。800 个对象已达到预算，报告 `budgetReached=true`。未调用系统 UIRibbon 的 RVA，也没有按 Office 业务控件名称修改窗口。布局属性设置、活动标题选择、spacer、绘制和恢复仍待验证。

记录：`out/live-v15/office-metadata.json`。

## TXGui / GF

只接受 GF.dll CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`、age 24。框架 GetWindowByHWnd 包装函数在服务不存在时会继续解引用，研究探针未直接调用这个包装函数。

实际验证路径：核验 Common.dll 的 GetPlatformCore 导出与 GF 导入槽一致，取得平台 Core；按 GF 原包装函数的调用约定取得窗口服务，检查 HRESULT 和空指针；在原 GUI 线程按目标 HWND 取得 IGFPopupWin，再用导出的 GetFrameCount、GetFrameByIndex 枚举。服务 IID 为 `6C1F3073-19C0-47F5-AAC8-8F30381D1B73`。

枚举限制深度、节点与每层数量，并平衡 COM 引用。根据 GFSpy 自身的类型识别与 getter 调用，核对 Rect、ScreenRect、Padding、Margin、Hidden、Visible、Enabled。没有按业务名称选择标题。

本次读取 460 个节点，包含 92 个 IGFButton、42 个 IGFStackPanel、一个 IGFStandardWin 和一个 IGFSysButton。受枚举深度及节点预算限制，这不是任意 GF 应用的完整树保证。

GetTypeInfo 返回 TYPE_E_LIBNOTREGISTERED。GF.dll 内置 TYPELIB 资源，但离线 LoadTypeLibEx(REGKIND_NONE) 读到的是可访问性接口，而非 IGFFrame 私有类型库；没有注册类型库或改写进程全局类型信息。

研究注册仅位于独立 `framework-probe-Win32/probe/Release/adapters.ini`，编号 267。Probe 总是拒绝 Attach，不发布 Owner/Inline 会话。`test-framework-readonly.ps1` 验证新鲜报告、根发现、枚举成功、窗口响应、几何/样式/最小化状态、子 HWND 数量和会话属性清理，共 11/11 通过。

记录：`out/live-v15/txgui-contract/result.json`、`tree.json`；离线类型库为 `out/live-v15/gf-type-library.json`。标准注入引导的小型 Hook DLL 可能按项目既有设计保持映射至目标自然退出，但未保留研究探针的布局回调或三灯会话。

尚缺完整窗口操作语义：当前可见标题操作区使用普通 IGFButton，不能凭坐标或应用特定名称将其认作关闭/最小化/最大化。原框架三灯绘制、事件绑定、原布局腾位及精确撤销也尚未验证。

## 复现

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S demos/window-styler/research/framework-probe -B demos/window-styler/out/framework-probe-Win32 -G 'Visual Studio 17 2022' -A Win32
& $cmake --build demos/window-styler/out/framework-probe-Win32 --config Release --target FrameworkResearchHost FrameworkTypeLibrary

# HWND 必须重新从当前窗口清单获得，不应复用历史报告中的句柄。
.\demos\window-styler\research\test-framework-readonly.ps1 -Hwnd <TXGui-HWND>
python demos/window-styler/research/office-metadata.py --hwnd <Office-HWND> --max-objects 800 --out office-metadata.json

# 目标权限较高时在管理员终端运行；只读取并生成结构报告。
.\demos\window-styler\research\read-mfc-elevated.ps1 -Hwnd <BCG-HWND>
```
