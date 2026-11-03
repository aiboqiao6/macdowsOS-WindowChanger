# v20：TXGui 原点击链、Office 按下中撤销

2026-11-02。未使用 computer-use 工具。TXGui 的实际顶部按钮现已读到原点击订阅及宿主回调来源，但仍未完成通用操作语义选择和三灯布局。Office 起始页原型补上按下/捕获期间撤销的处理。MFC/BCG 维持 v16 原型，正式控制器范围不变。

## TXGui：原事件容器与点击来源

沿用 GF 的已核验 x86 CodeView profile。新只读解析器仅接受原 IGFButton 类及其确切事件容器布局，检查两棵原事件树的父子/尾部关系、大小、顺序、深度、节点预算，以及监听器范围和回调地址可执行性。只读解析不调用回调、不订阅事件、不改变框架引用计数。

先在自有进程的真实 SysButton/三个原 Button 上验证。独立创建的默认 Button 事件表实际为空，不能当成已经注册原点击：测试器通过原事件接口，在自有对象上注册受控数字事件、短/长命名事件和监听器，校验读取内容，再由原 API 撤销。还验证了超过 128 项预算时拒绝遍历。没有向这些事件发送通知。

增强后真实 SysButton 契约 Release/Debug 均 **157/157**，包括原有布局/遮罩事务和新增事件检查。最初错误地预期 detached 对象已有点击订阅的失败记录保留在 `txgui-event-fixture-release.log`；最终记录为 `event-fixture-final-Release/Debug.log`，不将前者计为通过。

实际窗口的 460 个节点中，**92 个 IGFButton 事件容器全部完整，两次读取一致**，共读取到 79 项数字订阅。沿用 v19 的 21 次原生命中查询，三枚真实顶部按钮各自有一个 `0x1003` 点击订阅，指向同一个原监听器。此数字事件与原 `onClick` 分派在 GF 代码中对应，而非根据按钮位置猜测。

增强只读状态检查 Release/Debug 均 **22/22**。证据：`out/live-v20/txgui-events-final-release/`、`txgui-events-final-debug/` 的 `tree.json`、`result.json`。

新增独立的只读 `txgui-callbacks.py`：从原命中身份和事件订阅出发，按原生事件桥的精确 CodeView/入口字节验证，读取其“来源身份 + 事件编号”回调表。未知桥保持不透明，不调用未知接口。三枚顶部按钮均解析到三个不同宿主处理器，结果为 `txgui-callback-chain.json`、`txgui-callback-chain-debug.json`。

回调来源记录包含映像身份、RVA、可达基本块和实际引用的导入；不越过 return 去分析邻接函数，边界/预算自检 **4/4**。这里只报告调用来源：这一层已进入宿主代码，**不能把宿主回调 RVA 当成通用框架按钮标识**。结果明确保留 `targetCodeExecuted=false`、`roleSelectionImplemented=false`、`roleAssigned=false`，没有新增基于应用名、业务控件名或宿主回调地址的布局后端。原关闭/托盘策略、通用操作语义与三灯布局仍待完成。

## Office：输入取消先于节点撤销

v19 原型在创建的按钮上按下时，原 NetUI 会持有鼠标捕获。现在同时跟踪顶层转交和原根 HWND 的直接按下消息；撤销前先向原框架发送取消，必要时释放仅属于该根 HWND 的捕获，然后才移除、销毁实验节点。不合成 button-up，不借取消执行窗口命令。原根直接输入也先校验树身份。

最终 Release 连续两轮均 **55/55**，外层 **38/38**；Debug 一轮 **55/55**，外层 **34/34**。除 v19 的持续绘制、状态切换和恢复外，新增覆盖：

- 原按钮按下后确实由 NetUI 捕获；取消后捕获释放、没有窗口操作，按钮随后仍可正常最大化/还原。
- 直接给原根 HWND 输入后，在按钮仍按住时显式撤销，清理记录确认取消过输入，未残留捕获。
- 原按钮仍按住时销毁自有控制器 HWND，或终止自有控制器进程，均自动取消捕获并恢复。
- 嵌套诊断失效请求推迟到最外层回调退出后清理。它验证延迟清理分支，不冒充实际原树销毁测试。
- 接收 `WM_THEMECHANGED` 通知时保守撤销并恢复；没有改变 Windows 主题或用户设置，也不代表实际主题切换已验证。

原 1,896 个 Node 的身份、父子顺序和原 Helper 几何完全一致；窗口样式、位置、最小化状态、DWM、客户区、临时属性和子 HWND 数均恢复。最后一次控制器退出测试的报告为 `canceledInput=1`、清理错误/跳过属性均为 0。

证据：`out/live-v20/office-held-final-release/`、`office-held-final-debug/` 的 `host-*.log`、`transaction-*.json`、`result.json`、前后树/窗口清单和绘制 BMP。早期 54 项版本记录保留，不计作最终 55 项版本。

调试构建曾遇到已固定映射的实验 DLL 无法覆盖（LNK1168）。持续实验的内容摘要现在也包含验证器源码及研究引导版本，生成独立实验修订文件；未覆盖映射中的 DLL，也未关闭/重启用户 Office。正式产品的模块身份保护没有变化。

Office 仍限本机 x64、144 DPI、LTR、FullpageUIWindow 起始页。硬件鼠标、关闭用户窗口、实际原树重建/销毁、编辑窗口、跨屏 DPI、键盘系统菜单及 UIA 矩形仍未验证。

## 编译与最终状态

Office x64、TXGui x86 的 Debug/Release 编译通过。选定研究 CTest：x86 两种配置均 **3/3**（样式、原 Frame、原 SysButton），x64 样式两种配置均 **1/1**，各含 16 项样式内部检查；原 Frame 50 项事务继续通过。

主程序 Release/x64 **11/11**；Release/x86 首轮 6/7，`cross_process_style` 的侧边/底边像素断言失败，完整重跑 **7/7**。原日志 `ctest-main-Win32.log` 与重跑 `ctest-main-Win32-retry.log` 均保留，未修改主程序或用重跑掩盖首次结果。

主 C++ 52 项格式、五个研究 C++ 文件/头文件格式、PowerShell 语法和 Python 编译检查通过。VS 项目和筛选器为 113 个匹配的显式文件项，路径存在、无重复或通配符。最终 `final-state-comparison.json` 核对三个原应用/窗口仍存活、Owner 清空且原几何和最小化状态一致。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmake = Get-DevelopmentTool cmake
& $cmake --build demos/window-styler/out/framework-probe-x64 --config Release --target OfficeLeaseHost
.\demos\window-styler\research\test-office-layout.ps1 -Lease -OutputDirectory demos/window-styler/out/live-v20/office-local

& $cmake --build demos/window-styler/out/framework-probe-Win32 --config Release --target FrameworkResearchHost TxGuiFixture
.\demos\window-styler\research\test-framework-readonly.ps1 -Hwnd <当前TXGui-HWND> -Active -OutputDirectory demos/window-styler/out/live-v20/txgui-local
$env:PYTHONPATH = 'demos/window-styler/out/research-deps'
python demos/window-styler/research/txgui-callbacks.py --hwnd <当前TXGui-HWND> --report demos/window-styler/out/live-v20/txgui-local/tree.json --out demos/window-styler/out/live-v20/callbacks-local.json
```
