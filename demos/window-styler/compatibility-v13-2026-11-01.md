# v13：桌面窗口逐项检查

日期：2026-11-01。内容版本 `931e1cda276e`。未使用 Computer Use。

本轮增加同一个 Chromium Views 后端的第二个框架 ABI profile，并修复首次最大化后还原按钮无高度、绿灯消失及尺寸测量累积问题。Chrome 和 Edge 的真实鼠标检查均通过，原控件、标签和几何在撤销后恢复。通过完整回归：x64 15/15、x86 7/7。

**全部桌面窗口适配仍未完成。** 本轮初始范围是 17 个应用窗口。最新逐窗口报告和截图位于 `out/live-v13/final-desktop/report.md`；初始范围与实验故障汇总位于 `out/live-v13/scope-report.json`。实验中退出的两个窗口继续保留在范围内，不能从分母中删除。

当前桌面 15 个应用窗口：12 个截图通过、3 个未适配；另外排除 5 个系统/后台/控制器窗口。初始 17 个窗口：12 个通过、3 个未适配、2 个实验故障。

| 框架 | 本轮逐窗截图通过 |
| --- | --- |
| Electron | Codex/ChatGPT、QQ |
| Chromium Views | Chrome、Edge |
| WPF | Visual Studio、调优工具 |
| Windows XAML Islands | Terminal、任务管理器 |
| ApplicationFrame/UWP | 系统设置 |
| Win32 / Shell / Console | VMware、控制面板、控制台 |

## Chromium 实测

| 目标 | 首次确认 | 结果与证据 |
| --- | --- | --- |
| 本机 Chrome，DPI 144 | 359ms | `out/live-v13/chrome-desktop-input5/result.json`，passed=true |
| 本机 Edge，DPI 144 | 477ms | `out/live-v13/edge-regression5/result.json`，passed=true |
| 独立新建 Chromium 窗口 | 363ms | `out/live-v13/chromium-fixture-test5/result.json`，passed=true |

检查包含首次三灯像素、右侧原按钮命中消失、黄灯最小化、绿灯最大化/还原、四次尺寸变化、主题/样式通知、两次撤销再附加、所有者终止后恢复。原标签 RuntimeId、子 HWND 数量及几何纳入断言。用户应用红灯未点击；关闭动作在自有 fixture 中验证。

额外 F11 检查曾发现全屏暂时移除原控件树会触发错误撤销。已让同一框架后端在无 WS_CAPTION 的全屏期间保留休眠会话，恢复标题行后重新验证布局。Chrome 与 Edge 各两次全屏进入/退出均通过，截图和原按钮命中断言见 `chrome-modes-repaired`、`edge-modes-repaired`；早期失败记录保留。

任务管理器 8 个导航页面、首次及切页后早期/稳定共 17 帧检查通过，另在每次切页时回写原系统菜单样式，原按钮均保持隐藏。原 UIA 顶层搜索在样式改变后会失去 Island 导航项；检查脚本改为从框架子 HWND 枚举同一语义项，并拒绝把零页遍历计为通过。记录和三灯像素检查见 `out/live-v13/taskmanager-views-replay-final/`。

![Chrome 原标签行内嵌真实截图](images/window-injector-v13.png)

## 尚未适配

WinUI 3、MFC/BCG Ribbon、Office NetUI、TXGuiFoundation 尚无已验证的正式布局后端。已修正它们在最小化时误入普通 Win32 后端的问题。通用适配器注册机制继续保留，未知框架/ABI 明确拒绝。

本轮 WinUI 3 diagnostics 实验导致照片、记事本退出。两次 Application Error 1000 均为 `e0464645`，已保存原事件并停用该实验入口。研究加载器现在只接受有自有测试标记的窗口；已在独立程序读取原控件树，尚未完成生产适配。详见 [框架研究与故障记录](research/frameworks-v13.md)。

所有结果来自原生 GDI 截图、Win32 消息、UIA 只读检查或原生鼠标输入；无跟随窗口。发行包默认管理员权限，只自动接管通过生产后端检查的应用窗口。
