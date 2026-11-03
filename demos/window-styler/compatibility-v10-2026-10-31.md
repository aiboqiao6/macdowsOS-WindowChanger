# v10：窗口刷新后原按钮重现的框架修复

日期：2026-10-31。本轮以任务管理器切视图后原按钮重现为入口，检查宿主重写样式、隐藏属性和标题区留白时的行为。不使用 Computer Use；截图来自 Win32/GDI 屏幕复制，导航由 UI Automation，三灯由原生鼠标或 Win32 消息验证。

## 发现与修复

1. 开始检查时，旧版任务管理器同时显示左上三灯与右上原操作按钮，实际 WS_SYSMENU 已重新置位，Query 仍返回成功。截图保留在 `out/live-v10/views-v9`。
2. 旧版重新附加后，仅切换八个导航页没有稳定再次触发。进一步回放宿主写回原窗口样式：旧版 17 帧中 16 帧原按钮可见，新版同样回放的 17 帧为 0。这是可重复的机制对照，不把页面导航单独当作稳定复现。
3. XAML/DWM 与 NetUI 原先只虚拟化 STYLESTRUCT 通知副本，没有修改真正提交给 user32 的结构。现在共用 `native-style-view.h`，在样式提交前清除真实 WS_SYSMENU；保存宿主最新菜单位意图，撤销时保留该意图和其他样式位。没有反复 SetWindowLong/FRAMECHANGED 修补循环。
4. XAML、WPF、Qt 原先一次性隐藏按钮、腾出标题空间。增加框架依赖属性回调、Qt Show/属性通知；宿主重新显示按钮或写回留白时同步维持抑制。撤销前移除回调，再还原保存的属性/绑定。仅作用于已识别的原标题控件。
5. 测试另发现 Shell 的系统最大化命令会进入无标题栏全屏，未注入时也相同；ShowWindow 标准最大化保留标题。Win32 与 NetUI 绿灯统一改为标准 HWND 状态转换，保留宿主 WM_SIZE 和布局处理。没有控制面板或资源管理器名称分支。

## 验证入口

- `test-taskmanager-views.ps1`：保存原选中页、位置、最小化状态和前台，逐页检查进程、性能、应用历史记录、启动应用、用户、详细信息、服务、设置；每页早期/稳定两次截图；最后还原。`-ReplayHostStyle` 回放宿主重写系统按钮样式。
- `screenshot-all.ps1 -HostRefresh`：逐窗口附加、原样式回写、主题通知、最大化/还原、撤销；记录真实截图、颜色、客户区、外框、子 HWND、原按钮指标和区域。
- `StylerTests`：纯/混合 XAML、WPF 两运行时、Qt Widgets/Quick 每个窗口 12 次宿主按钮/留白重写；原生窗口模拟 SC_MAXIMIZE 转无标题栏全屏；既有移动缩放、输入、守护恢复继续执行。
- NetUI：两扇临时测试目录窗口验证共享 IAT lease、独立撤销、最后还原全部导入地址；不结束用户 Explorer 进程。

证据：`out/live-v10/release-screenshots/report.md`、`out/live-v10/taskmanager-views/results.json`、`out/live-v10/replay-v10/results.json`、`out/live-v10/input-final/results.json`、`out/live-v10/netui-final`。本轮早期失败和调试数据保留在 out/live-v10，没有计入最终通过。

## 边界

通用注入基础和已验证框架后端已经实现；不能据此声称任意窗口、任意版本均兼容。Edge Chromium Views、系统设置 ApplicationFrame/UWP 仍未完成，此次没有把它们计为通过。

此次覆盖现有视图切换、原生样式和已识别属性重写。完整标题树替换、Electron 整页刷新、新加载模块、跨显示器 DPI 仍需独立验证。XAML/WPF 标题卸载时会撤销旧 lease，再由自动控制器探测新树；Qt 的完整标题树替换尚未完成专项验证。依赖属性未产生有效值变化的同值写入无法仅凭属性变化回调区分宿主意图。Electron 延续已有语义按钮 MutationObserver 和公开 overlay setter 保护，本轮没有扩展其整页重载能力。

Qt 仍按已验证的 6.11.1 ABI；XAML/NetUI 的内部框架入口继续执行 CodeView/指令检查，未知版本拒绝。管理员默认 UAC 沿用，不绕过受保护进程或安全桌面。
## 最终结果

最终内容版本 `204f30312c28`：x64 CTest 14/14；x86 CTest 6/6，包含本轮新增刷新与最大化行为断言。

当前 9 个应用窗口（任务管理器、Terminal、Visual Studio、WPF 调优工具、Codex、QQ、VMware、conhost、控制面板）全部通过逐窗口截图、样式回写、主题通知、黄绿灯及撤销。绿灯最大化/还原、黄灯最小化 9/9；真实截图 9/9。另开两扇自有测试目录的资源管理器窗口，NetUI 截图与黄绿灯 2/2、共享 IAT/独立撤销/最后还原检查 10/10，随后仅关闭这两扇自有目录窗口。

任务管理器八个导航页的初始/早期/稳定画面共 17 帧通过；宿主样式回放另 17 帧通过。旧版回放 16/17 帧原按钮可见，新版 0/17。源截图与真实样式同时记录，没有只依赖 Query。回到测试前选中页并恢复窗口原位置和最小化状态。

本轮也修正验证脚本：对原本没有 WS_SYSMENU 的自绘窗口，不把该位无条件加回；指针坐标区分客户区与非客户区；窗口动画后等待稳定几何；滚动条等待非空矩形再验证原命中；控制器自身不计为应用通过。早期失败记录保留，最终报告使用修正后的脚本和最终 DLL。
正式包管理员控制器验证 16/16：双架构接管、Qt/WPF/XAML 示例、暂停/恢复、命令行 lease、崩溃撤销均通过，证据 out/controller-direct-v10/checks.json。

已用正式包恢复默认管理员自动接管，控制器 PID 28528；当前 9/9 窗口附加成功，9/9 原位置和状态精确恢复。证据：out/live-v10/startup.json、running-inventory.json。

首次验证启动的控制器随后退出，未查到 Windows 应用崩溃事件；未据此认定为注入后端故障。直接提权启动正式应用后，启动脚本已退出，控制器在独立 45 秒检查的四次采样中均存活，并持续运行超过两分钟，未加入自动重启。证据：out/live-v10/controller-liveness.json。
