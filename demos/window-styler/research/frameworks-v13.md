# v13 桌面框架研究记录

所有生产适配器继续按框架能力、CodeView 和原控件结构选择。窗口标题、应用名、安装路径仅用于检查报告，不参与后端分派。

## Chromium Views 多 ABI

当前 Edge 与 Chrome 使用同一个 `views-inline.cpp`。`views-profiles.h` 描述框架字段和入口指令，注册 264 / 265 分别筛选经过核验的 CodeView：

| CodeView / age | 已验证的本机框架构建 |
| --- | --- |
| `8DE675E4-DC25-455F-4C4C-44205044422E` / 1 | Edge 157.0.4341.24 x64 |
| `C4935A66-4D7D-C7AC-4C4C-44205044422E` / 1 | Chromium 150.0.7871.125 x64 |

新 profile 的符号来源为 Chromium 公共符号服务器。保留的 `out/live-v13/chromium-identity.json`、`chromium-symbols.json`、`chromium-framework-symbols.json` 与反汇编记录构成研究证据。Profile 不匹配时拒绝，不能把当前 RVA 用于其他编译版本。

新 ABI 的 View 子列表采用 begin/end/capacity。布局、可见性切换及 `WM_GETMINMAXINFO` 进入时还原原几何，退出时应用变换。最小尺寸测量也读取 View 几何；省略事务会使每次调整尺寸积累预留宽度。首次最大化时，原本隐藏的还原 View 可为零高度；仅在变换阶段从原按钮父行取得高度，保存的原矩形仍保持为空，供原测量与撤销使用。绘制失效使用原 `View::SchedulePaint`。

原生鼠标检查记录在 `chrome-desktop-input5`、`edge-regression5`；独立新建窗口首次切换记录在 `chromium-fixture-test5`。此前失败记录继续保留。

全屏暂时隐藏或替换标题控件时保留休眠会话，不在无 WS_CAPTION 的阶段重建按钮组。退出全屏后由原样式/布局通知触发验证。两种 profile 各完成两轮 F11，核验全屏无三灯像素/窗口动作、恢复后三灯像素与原输入命中一致。

## 尚未启用的框架

| 框架 | 结构证据 | 缺口 |
| --- | --- | --- |
| WinUI 3 / Windows App SDK | DesktopChildSiteBridge、ReunionWindowingCaptionControls | 原控件修改、标题按钮与生命周期尚未完成 |
| MFC / BCG Ribbon | BCGPRibbonBar 框架窗口类 | 静态链接的自绘标题布局入口尚未验证 |
| Office NetUI | NetUIHWND，未加载系统 UIRibbon.dll | 与系统 Ribbon ABI 不兼容，不能套用既有后端 |
| TXGuiFoundation | 框架顶层窗口类 | 原自绘控件树和布局入口尚未验证 |

框架证据在最小化状态也有效。`strict_direct_policy` 的独立进程测试覆盖正常/最小化、四类身份、拒绝原生兜底及无修改元数据。

## WinUI 3 实验故障与隔离

2026-11-01 09:44，实验探针通过系统 `Windows.UI.Xaml.dll` 的 diagnostics 连接 Windows App SDK 2 目标，造成照片及记事本退出。系统 Application Error 1000 记录异常 `e0464645`，故障模块为 `Microsoft.Internal.FrameworkUdk.System.dll`。这是本轮实际故障，不能计为适配成功。事件记录保存在 `out/live-v13/winui3-experimental-failures.json`。

已停用该入口：`winui3-probe/probe.cpp` 的旧导出直接返回 `ERROR_NOT_SUPPORTED`。研究加载器在任何远程分配或载入之前要求目标存在自有测试窗口标记 `Macdows.WindowStyler.Sample`；目标内根枚举也再次检查。当前用户窗口会被拒绝。发行包不包含研究探针目录。

后续仅在独立 WinUI 3 测试程序验证过另一条根枚举入口：Microsoft.UI.Xaml.dll CodeView `4ACDBE85-D668-192E-B8B6-A9334AC91B79` / 1，`DXamlCore::GetCurrent` RVA `0x3a24b8`，`DiagnosticsInterop::GetXamlIslandRoots` RVA `0x731804`。原 GUI 线程返回一个 DesktopWindowXamlSource 及其 WindowChrome / 原 Grid 树。它只证明根读取成功，不证明生产窗口适配、稳定性或其他版本兼容。未注册为正式后端。
