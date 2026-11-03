# v29：TXGui 已有标题 Frame 的原布局扩展与撤销

2026-11-03。未使用 computer-use。已通过原 padding/margin 和原重排接口，将真实**已有**标题 Frame 从 `[232,0,1226,76]` 短时扩展为 `[0,0,1226,76]`，保持原内容和原标题按钮的实际位置及像素不变，再精确撤销。**未增加标题高度、未增加标题 HWND、未移动三灯到左侧，也未启用正式后端。**

## 原布局 API 和自有夹具

新增 `txgui-frame-layout.h`、`txgui-frame-layout-contract.h` 和 `TxGuiFixture --title-contract`。只接受 GF CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`、age 24，核验原接口 wrapper、native getter/setter 和 ordinary Frame 原布局实现；父关系和 GUI 线程必须匹配。通过公开接口按值传入 RECT，不写原对象私有字段。

新增状态记录包括公共 rect/margin/padding，以及原 `0x2f8` getter 返回的 48 字节布局记录。该记录包含几何、测量结果、原锚定和 auto/dirty 状态，**不是 48 字节都可当作“不随重排变化的约束”**。扩展期间只接受预期的局部 x 起点变化，其余字段逐项保持；撤销后要求整条记录及公共几何精确相同。

在自有原 Frame 父容器和原 title、content、caption 子对象中复现真实几何：标题左 margin 232、height 76，原 content 宽 569，原 caption 宽 183、右锚定。扩展时将 title 左 margin 从 232 改为 0，并给 title 等量左 padding；原框架自行重排，原 content 局部 x 从 0 变为 232、caption 从 811 变为 1043，绝对位置不变。

最终 Release/Debug 各 **18/18**，覆盖 32 次往返、异常 scope 回滚、重复撤销、异父/空父拒绝、零 margin 拒绝、原 GF frame count 恢复及无 HWND 增加。证据 `out/live-v29/title-final-*.log`。

## 早期失败与修正

- 通用 `height`、`width`、`horizontalAlignment`、`anchor` 属性虽然返回成功，夹具回读却未形成有效原布局，最初 4 项失败，见 `title-initial.log`、`title-policy-initial.log`。
- 曾把原 `0x204` 当尺寸 setter；其实际语义是位置，回读出现错误起点，见 `title-size-initial.log`。根据原 wrapper 追踪，改用已核验的 `0x1ec` 原尺寸接口。
- 泛用 anchor 属性没有生效；改为原 `0x21c` BSTR 锚定 setter 后，原布局才复现了真实 Frame 范围，见 `title-anchor-initial.log`、`title-anchor-typed.log`。
- 真实窗口第一次策略比较仍失败，但原像素和撤销均通过。诊断 `title-live-policy.json` 显示，原生 48 字节记录的 x 单位为物理像素，公共 rect 为 DIP；本机 144 DPI 下预期偏移是 **348**，不是 232。已修正单位比较，不忽略原字段；最终仍要求全部原记录精确撤销。所有早期失败保留。

上述接口和调用约束的静态证据见 `original-title-layout-abi.txt`。RVA 仅用于严格框架 ABI 匹配，不按宿主业务名称或原操作回调地址选择后端。

## 真实有限事务 277

新增研究专用 `TxGuiTitleProbe`、`TxGuiTitleHost`、`test-txgui-title.ps1`。按原语义标题按钮、共同父容器及原顶部 Frame 关系选择已有 title，不按应用名/业务控件名选择。

事务前完整读取原树，并保存 title 和每个直接子节点的布局记录。先确认可读取、稳定的局部原屏幕像素，才执行扩展；没有原按钮输入、没有重挂父节点、没有自定义绘制。由普通 `RedrawWindow` 触发原 renderer，未手工构造 Frame 绘制通知。

真实 Release 两轮 / Debug 一轮均为原生 **18/18**，外层分别 **28/28**、**23/23**，见 `title-final-live-Release/`、`title-final-live-Debug/`：

- 原已有标题范围 `[232,0,1226,76] → [0,0,1226,76] → [232,0,1226,76]`，没有新增行高。
- 完整 **1,075** 节点的身份、父关系、子序、原绑定及原按钮过滤器保持；title 直接子项只改变局部 x 起点，屏幕实际位置不变，其他原节点的几何/状态保持。
- 原 title、原直接子项的全部 48 字节布局记录、margin/padding 和公共矩形撤销后精确恢复。
- 左侧原 header 内容区域 **432×102**、右侧原标题按钮区域 **213×84**，扩展前、扩展中、撤销后均逐像素相同。只保存这两个局部区域，不保存桌面或应用正文。
- 原顶层 HWND、client、style/exStyle、DPI、普通位置和最小化状态保持，无 Owner 会话残留。

本轮以开头读取到的现有正常窗口位置 `[838,932,2677,2132]` 为基线，没有将上一轮用户窗口的新状态强制改回更早的最小化位置。

## 像素预检被拒及圆角边界

最初真实实验在写入前被像素预检拒绝，`title-live-initial.*`、`title-live-Release/`、`title-live-Debug/` 保留，`titleExpanded:false`。最初沟通时根据先前环境怀疑任务管理器遮挡；后续点位证据确认不能将本次左侧预检拒绝直接归因于该窗口。

后续点位核对确认：左侧截图的原始起点位于目标圆角之外，那里实际上命中下面的 VS；右侧已经可读。证据 `capture-point-audit.json`。截图改为向内收进 8 DIP，避开圆角外像素，未为了抓图移动其他窗口或截图桌面背景。最终记录明确输出 `pixelPreflight:true`，上述最终三轮并未跳过像素检查。

新增外层脚本将“唯一失败是像素预检，且未发生扩展”报告为 `blocked:true` / 退出码 2，而不是成功；其他失败仍为错误。此前两轮阻挡验证的 20 项只读/恢复检查通过不等于标题扩展成功。

## 共享代码与范围

原完整树快照和比较提取到 `txgui-live-state.h`，局部原像素回读提取到 `txgui-caption-pixels.h`，供已有 275/276 与新实验 277 共用；CMake 摘要纳入新依赖，模块继续使用新摘要名，不覆盖用户进程已映射历史 DLL。

研究 x86 Release/Debug 各 **13/13**，新增原 title 扩展契约；x64 共享契约各 **3/3**，主程序两种架构的 `strict_direct_policy` 均通过。共享 Host 编译宏增加 277 后，x86 原研究 Host/模块和 x64 Office 研究 Host 已重建。正式控制器注册范围不变，MFC/BCG 与 Office 适配器逻辑未改，本轮不把其历史实际应用结果算作新验证。

提取共享代码后的真实绘制 275 两种配置各 **48/48**、外层 **25/25**；真实顺序 276 的 Release 一轮 **10/10**、外层 **26/26**。本轮没有重新跑主程序全部 UI 回归，只运行上述研究和正式严格分派策略检查，不将 v28 的完整回归结果算作本轮执行。

最终三个原用户窗口 HWND/PID、完整几何/状态、Owner 0 与本轮初值核对 **3/3**，TXGui 保持本轮开始时的正常新位置；无残留测试进程。主 52 项和本轮研究 C++ 格式、PowerShell 语法通过；VS 项目和筛选器各 **153** 个存在、唯一、匹配的显式文件项，无通配符。证据 `final-state-comparison.json`、`final-test-processes.json`、`vs-items.json`。

**仍未完成**：当前补偿 padding 使原内容起点不变，它本身不是可以直接塞入三灯的裁剪空间。下一步需验证原 header 的真实迁移、84 DIP 三灯组和原顶部内容让位的组合，以及原祖先裁剪/命中。没有把本轮“原标题 Frame 已扩展”当作“左侧三灯已可用”。持续附加、原动作/绘制整合和异常退出恢复也尚未完成。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiFixture TxGuiTitleHost FrameworkResearchHost
& demos/window-styler/out/framework-probe-Win32/Release/TxGuiFixture.exe 'C:\Program Files (x86)\Tencent\QQMusic\GF.dll' --title-contract
.\demos\window-styler\research\test-txgui-title.ps1 -Hwnd 0xYOUR_HWND
```
