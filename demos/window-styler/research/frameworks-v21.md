# v21：TXGui 原动作意图与原输入、Office 能力变更撤销

2026-11-02。未使用 computer-use 工具。TXGui 已完成本机窗口的原鼠标消息路径最大化、还原、最小化有限验证；**左侧三灯布局和绘制仍未完成**。Office 起始页持续原型增加宿主改变按钮能力时的撤销检查。MFC/BCG 保持此前原型，正式控制器未启用这些实验。

## TXGui：从原提示到原窗口操作

GFSpy 的原帧信息实现确认了 `toolTip` 的原属性 getter，IGFButton 通过自己的虚函数实现读取。先在自有进程的真实 Button 上验证 Unicode、空字符串、读取和恢复，再只读取实际顶部候选按钮的原提示。当前三枚分别为“关闭”“最大化”“最小化”。

动作意图匹配完整的中英文标准短语，不使用应用名、业务控件名、左右序号或宿主回调 RVA。未知提示、业务字符串前后缀、缺失值、过长值及带内嵌 NUL 的 BSTR 拒绝。**提示只证明意图，不能单独证明回调效果或关闭策略。**

新增独立实验 273，保留原控件、原事件订阅和原根 HWND。只有三个候选共用原父节点、位于有效顶行、提示互不冲突、各有一个原点击订阅且共用原监听器时，才进行有限输入验证。每次输入前重新检查提示、启用状态、原事件绑定和原生命中身份；没有直接调用宿主回调，没有主动发送最大化/最小化系统命令。

测试只向原 HWND 发送鼠标消息，按次验证最大化、还原、最小化；**从未输入关闭按钮，也没有使用硬件鼠标**。结束后恢复原窗口位置和状态。没有改写原布局属性、事件订阅或绘制，没有增加 HWND、外部覆盖层、标题带或跟随定时器，也不会留下持续会话。

最终 Release 连续两轮均 **25/25**，外层 **29/29**；Debug 一轮 **25/25**，外层 **24/24**：

- 原命中查询与目标来源身份一致，原输入释放捕获。
- 原窗口实际进入最大化、还原及最小化状态。
- 三枚原按钮的几何、提示意图和事件订阅恢复。
- 全部 460 个原节点的身份、父子关系及子节点顺序一致；没有把后台业务控件的动态数值当作固定布局。
- 原样式、扩展样式、外框/客户区、DPI、正常位置、最小化状态、DWM、圆角恢复，Owner 为 0。

最终证据：`out/live-v21/txgui-actions-restored-release/`、`txgui-actions-restored-debug/` 中的 `transaction-*.json`、`host-*.log`、`result.json` 和前后原树/窗口清单。报告明确记录 `closeInput=false`、`layoutModified=false`、`persistentSession=false`。较早的 `txgui-actions-final-*` 等阶段记录保留，不替代最终恢复路径的复核。

该实验最终返回不支持“持续附加”，因此日志中的 `Attach accepted: 0` 是预期行为；成功依据是新生成的原生报告和验证器退出码，而不是将其伪装成正式布局后端。

只读检查两种配置均 **25/25**，包括 v20 的事件容器检查及原提示 getter/三种意图证据。真实 SysButton 夹具两种配置均 **208/208**，包含原有事件/布局事务和新提示测试。最初通用属性接口返回成功但未改变 Button 的覆盖属性，产生 9 项回读失败，仅发生在自有夹具；记录 `tooltip-fixture-release.log` 保留。改用原 Button 属性 setter 后回读及恢复通过，最终记录为 `fixture-final-Release/Debug.log`。

新增源码：`framework-probe/txgui-caption.h`、`txgui-actions.h`、`test-txgui-actions.ps1`。实验 273 只注册于独立研究构建；正式 `adapters.ini` 不包含它。关闭/托盘策略、硬件鼠标、跨 DPI、更多本地化提示、持续布局及三灯绘制仍待验证。

## Office：宿主改变能力时恢复最新请求

新增真实窗口边界测试：持续挂载后临时请求“允许最小化、禁止最大化”。原型检测能力视图改变后安全撤销，撤销保留宿主的最新能力请求，不能强行恢复旧能力；验证器随后恢复测试前完整样式。测试有作用域恢复保护，没有关闭用户窗口。

最终 Release 两轮均 **60/60**、外层 **38/38**；Debug 一轮 **60/60**、外层 **34/34**。覆盖此前持续绘制/状态切换、输入捕获取消、按住时撤销或控制器退出、嵌套失效、主题通知、本轮能力改变及验证器最终位置/最小化状态恢复。1,896 个原 Node 身份、顺序、原 Helper 几何和窗口状态均恢复。

最终证据：`office-restored-release/`、`office-restored-debug/`；早期 59 项记录仍保留。Office 仍限本机 x64、144 DPI、LTR、FullpageUIWindow 起始页；编辑窗口、实际树重建/销毁、跨屏 DPI、系统菜单键盘路径和 UIA 矩形仍未验证。原有限布局实验 271 在共享恢复逻辑更新后复核 124/124、外层 34/34，见 `office-bounded-restored/`。

## 验证器收尾修复

首次全局核对发现 TXGui 未保持开始时的最小化状态（2/3 窗口状态匹配），初次 `initial-state-comparison-failed.json`、`initial-final-inventory.json` 保留。研究验证器原来的前台恢复路径可能激活已最小化窗口；某些宿主在收到激活后会重新显示自己。

新增共享 `research-placement.h`：恢复最小化窗口使用 `SW_SHOWMINNOACTIVE`，不激活已最小化的前台引用；恢复后等待并回读窗口状态与正常位置，失败不能返回测试成功。TXGui 两个研究入口及 Office 两个布局验证器统一使用它。没有关闭、重启用户应用来处理收尾。

自有 Win32 窗口验证最小化、正常位置、析构恢复、重复完成及“激活时重新显示”的策略边界，x64/x86、Release/Debug 均 **10/10**，见 `ctest-restoration-*`。它是验证器测试，不冒充 GF/Office 实际控件验证。随后完整重跑 TXGui 和 Office，最终全局核对 **3/3**，无残留测试进程或 Owner 会话。

## 构建、回归与最终状态

TXGui x86、Office x64 的 Release/Debug 实验编译通过。最终选定研究 CTest：x86 两种配置均 4/4（样式、位置恢复、原 Frame、原 SysButton），x64 两种配置均 2/2（样式、位置恢复）；原 Frame 50 项和各配置 16 项样式契约继续通过。

主程序 Release/x64 **11/11**、Release/x86 **7/7**，本轮完整回归记录为 `ctest-main-x64.log`、`ctest-main-Win32.log`；之后的修改仅为研究验证器，正式模块未改。研究测试与主程序完整回归按顺序执行，避免互相占用前台。主 C++ 52 项格式及九个本轮研究 C++ 文件/头文件格式、三个研究 PowerShell 脚本语法检查通过；VS 项目与筛选器 119 个显式文件项匹配，无缺失、重复和通配符。

最终 `final-inventory.json`、`final-state-comparison.json` 核对三个原应用及 HWND 仍存活，Owner 会话已清除，样式、位置和最小化状态与开始一致。已映射的历史实验 DLL 保留在进程中，不代表仍有实验会话；未通过关闭应用来卸载它们。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmake = Get-DevelopmentTool cmake
& $cmake --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiActionHost FrameworkResearchHost TxGuiFixture
.\demos\window-styler\research\test-txgui-actions.ps1 -Hwnd <当前TXGui-HWND>

& $cmake --build demos/window-styler/out/framework-probe-x64 --config Release --target OfficeLeaseHost
.\demos\window-styler\research\test-office-layout.ps1 -Lease
```
