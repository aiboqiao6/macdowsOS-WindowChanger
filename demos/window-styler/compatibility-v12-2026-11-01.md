# v12：Chromium Views 原窗口内嵌

日期：2026-11-01。内容版本 `b0395f7de258`。未使用 Computer Use；未关闭或重启用户 Edge。

## 实现与边界

新增框架后端 264（InlineKind 9）。注册器按标准窗口类、已加载模块的 CodeView 身份筛选，再在目标 GUI 线程验证 HWNDMessageHandler → DesktopWindowTreeHostWin → Widget → RootView、弱引用、对象存活、父子关系和函数指令。使用标准 HTMINBUTTON / HTMAXBUTTON / HTCLOSE 命中识别原按钮组，不按 Edge 名称、程序路径、业务控件名或应用对象字段选择。

移动原按钮容器和按钮，在原 gfx::Canvas / cc::PaintCanvas 中绘制三灯，原顶部内容行右移 72 DIP。保留原按钮动作和可访问性，没有新 HWND、遮罩、标题行或逐帧位置跟随。750ms 定时器只检查所有者存活。

原布局驱动采用事务：开始布局前恢复原 View 几何，框架按原坐标完成布局，退出后统一应用修改。框架不会读到变换后的按钮容器宽度来计算标签区，解决最大化后标签消失和撤销后按钮错位。View 失效、离开原树或所有者退出时恢复原回调；不匹配 ABI 或结构时拒绝。

当前验证身份为 `8DE675E4-DC25-455F-4C4C-44205044422E` age 1，来自当前运行的 Edge 157.0.4341.24 x64。该约束对应经过验证的框架 ABI，不代表任意 Chromium 版本通用。其他编译版本需要增加并验证框架 ABI profile；不能沿用现有地址。

## 本机实测

当前 HWND `0x59047c`，150% 缩放、DPI 144，水平标签模式。

| 检查 | 结果 |
| --- | --- |
| 首次附加确认 | 400ms；原绘制三色 FF5F57 / FFBD2E / 28C840 |
| 首次、稳定、主题通知、原样式回写、最大化、还原 | 真实截图通过，标签仍可见 |
| 客户区、外框、样式、子 HWND | 附加不改变几何及样式，无新增子 HWND |
| 黄灯最小化、绿灯最大化/还原 | 原生鼠标通过；不使用 UIA Invoke 替代输入 |
| 4 次尺寸变化 | 原标签 UIA RuntimeId 和可见性保持 |
| 同会话撤销/重新应用 2 次 | 通过，右侧原按钮未重现 |
| 终止测试所有者 | 自动撤销，原按钮命中、标签、几何恢复，Edge 存活 |
| x64 / x86 CTest 回归 | 15/15、7/7 通过 |
| 正式包 Edge 输入与恢复 | 全项通过，`out/live-v12/package-verified/result.json` |
| 正式包原生引导和 x86 WPF | 全部通过，`out/live-v12/package-checks.json` |

引导修复后的真实鼠标脚本完整通过（`bootstrap-input`，附加确认 510ms）；之前两个新会话也通过（`input-diagnostics`、`input-verified`）。脚本在点击前检查 Ready、WindowFromPoint 根窗口、标准窗口动作、前台窗口和实际光标位置，且只点击黄绿灯。红灯关闭未在用户应用上测试。

正式包也完成了相同的 Edge 原生鼠标、重排、重复附加和所有者退出恢复检查，结果 `passed:true`。

证据：`out/live-v12/edge-release/result.json`、`out/live-v12/bootstrap-input/result.json`、`out/live-v12/ctest-x64-bootstrap.log`。截图在相同目录；`images/window-injector-v12.png` 为真实标题区裁切。

## 失败记录与未验证范围

早期构建在独立会话与分发目录复验时出现线程 Hook 错误 126，尚未进入 Views 后端。只读路径检查和全新测试窗口也复现，不能归因于某个 Edge 布局；仅复用已加载 DLL 来源路径未解决错误。进程内注册/撤销线程 Hook 的独立探针通过。

正式注入器增加通用引导：跨进程 SetWindowsHookEx 仅在返回 ERROR_MOD_NOT_FOUND 时，载入本次已核验的 Hook DLL，再由目标进程注册短期线程 Hook，发送标准附加消息到原 GUI 线程，最后移除临时 Hook。先验证同一用户、权限、位数、原请求与所有者窗口；权限拒绝不转入此路径。LoadLibraryW 使用目标已有系统模块加导出偏移解析，不假定两个进程的 ASLR 地址相同。所有工作线程有等待上限；超时不终止目标线程或释放仍可能使用的参数。新回归强制走引导路径，覆盖两个 HWND、重复附加/撤销、请求清理和几何恢复。原 API 返回 126 的底层原因仍未定位；现在由经过实测的引导路径处理。

两次早期鼠标检查分别未完成最小化和最大化（`input-retry`、`input-settled`）。脚本之后增加逐次前台激活、实际光标到位校验及抬键后等待，再完成上述检查。失败日志与结果保留，不计为通过。
当前未验证垂直标签、全屏、跨屏 DPI、真实系统主题切换、新建多窗口、完整框架树替换、其他 Chromium 编译版本及 x86 Views。现代 WinUI 3 尚未完成。

引导修复前 x86 CTest 2/6，通过日志与失败日志均保留。修复后 x86 CTest 7/7（`out/live-v12/ctest-x86-bootstrap.log`）；正式包的 x64/x86 强制引导、x86 WPF 原布局测试均通过（`out/live-v12/package-checks.json`）。这是对应原生/WPF 示例的分发验证，未扩大到 x86 Views。

## 复验

```powershell
.\demos\window-styler\screenshot-check.ps1 -Hwnd 0x59047c -Name edge-release -OutputDirectory demos/window-styler/out/live-v12 -HostRefresh
.\demos\window-styler\test-views-inline.ps1 -Hwnd 0x59047c -OutputDirectory demos/window-styler/out/live-v12/bootstrap-input
```

先暂停已有自动控制器，避免两个会话接管同一窗口。测试保存并恢复目标窗口的初始 placement；不要在用户窗口上执行早期研究探针。

管理员自动控制器已启动 v12（PID 29704）。当前 Edge 已确认后端 264 / InlineKind 9；启动记录 `out/live-v12/startup.json`。其他最小化窗口按原策略在恢复后探测，本轮没有把未检查的窗口计为通过。
