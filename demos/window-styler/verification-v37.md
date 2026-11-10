# v37 WinUI 标题边界、焦点与宿主属性验证

日期：2026-11-11。研究模块修订 `8795b2aadb67`，研究引导修订 `3bd5347ef4f5`。正式主运行时仍为 `0a931b9107b1`。本轮使用原 GUI 线程、原生消息、实际键盘/鼠标输入和脚本截图，没有使用 computer use 或子代理。窗口截图、进程标识、失败实验和私有检查只保存在忽略的 `out/live-v37`；真实画图证据仍位于 `out/live-v36` 的独立 v37 目录。

## 实现范围

延续 [v36 独立标题岛屿](verification-v36.md)，不改正式自动接管注册表，不添加应用黑白名单。研究入口仍要求精确 WinUI ABI、144 DPI、LTR、正常窗口、原生标题和唯一完整客户区岛屿。

客户区宽度低于 84 DIP 时拒绝附加，当前对应 126 原生像素；接管后缩窄或高度不足时自动撤销。自有边界 fixture 通过前置 WM_WINDOWPOSCHANGING 放开原生最小尺寸限制，让窗口真正到达 125/126px；没有修改 WM_NCCALCSIZE、原按钮能力或标题样式。126px 时三灯各有 212 个精确色像素，没有被标题岛屿裁剪；缩至 125px 后撤销，保留宿主请求的尺寸。

三灯设置 IsTabStop(false) 和 AllowFocusOnInteraction(false)，使 Tab 和鼠标标题操作保留原内容焦点。自有交互 fixture 使用显式模板的两只 XAML Button，一只捕获 CharacterReceived，一只记录 Click；验证真实字符输入、Tab/Shift+Tab、Enter、绿灯最大化/还原后继续输入，以及撤销后的焦点。它不是 TextBox，不代表文本编辑、选择、剪贴板或输入法验证。

撤销开始时比较当前 TitleBar 属性和接管完成时的快照，按颜色值保留宿主后来修改的 12 项配色，包括清为 null，同时保留图标、高度、主题更新。恢复失败重试不重新采样自身恢复过程。ResetToDefault 后，仅对有差异的选项临时开启扩展标题模式以设置，再取消扩展，恢复非空配色。非默认高度不能在原生标题模式下直接设置。

## 最终构建与自有窗口回归

x64 Release、Debug 均构建成功。最终代码的六组检查全部通过，每种配置 223 项，总计 446 项。

| 配置 | 新增边界/焦点/标题属性 | 深浅色窗口回归 | 外部注入与生命周期 |
| --- | --- | --- | --- |
| Release | 107/107 | 60/60 | 56/56 |
| Debug | 107/107 | 60/60 | 56/56 |

新增回归包括原有自定义配色的保留、接管期间清空和替换全部配色、宿主更新图标/高度/主题、接管前已有非默认选项。后两类与独立、未注入的新原生窗口进行完整 PNG 逐字节比较；原自定义配色的撤销前后也作完整窗口比较。没有裁剪或屏蔽差异。

原有 60 项回归覆盖自有深浅色、正文几何与像素、原标题文字与配色恢复、旧按钮隐藏禁用、缩放、最大化及真实三灯点击。56 项回归覆盖外部控制器正常退出、控制器异常退出、最小化中撤销、根替换、宿主关闭及应用内部主题切换。

证据目录：`native-boundaries-Release-final-r9`、`native-boundaries-Debug-final`、`native-title-{Release,Debug}-final`、`native-lease-{Release,Debug}-final`。最终检查同时核对 checks.json 项数和每项结果、result.json 与现有 fixture/host 哈希；汇总为私有 `final-evidence.json`。

最终 fixture SHA256：

- Release：`0328BF9A3B53105158D218010A5A0EF35331F32F2207F36560B879CC045B2775`
- Debug：`E9DE4640FF7A83F531158534B4C4CC1A583B984072A70F7BBC4E2B7F24B1C41D`

最终研究 DLL SHA256：

- Release：`10EA2AA3134AC2A46CAB46910F7617B1D5C85CA73E0626F640FF86FD00C2B3C3`
- Debug：`9E62848C44E1F436F8D222E5E365CBF2234AB86730B783B03949F0D1609FEE22`

## 真实空白画图回归

Release/Debug 各执行正常退出和控制器异常退出，四轮均通过。每轮只启动自己创建、核验路径和空白标题的画图窗口；没有关闭或重启用户已有应用，也没有改变系统或画图主题。四轮测试所建窗口均正常退出，无研究控制器或 fixture 残留。

原外框保持，客户区仅向上扩展，原正文像素不变。三灯各有 212 个精确色像素，原按钮查询隐藏禁用，旧 NC 三按钮输入无效。正常控制器退出码为 0；主动终止控制器轮退出码为 -1，目标自动恢复。撤销后的外框、客户区、能力和 owner 均恢复，完整窗口 PNG 与接管前逐字节一致，原客户区 PNG 也一致。已查看截图，工具栏、画布和状态栏没有重叠或位移。

最终证据位于 `out/live-v36/paint-title-{Release,Debug}-v37-{normal,crash}-final`。汇总检查要求所有预期布尔字段存在且为 true（changedTheme 必须为 false），检查异常恢复字段、退出码、全窗/正文 PNG 哈希及错误记录，不能只凭脚本退出码计为通过。DWM 20、33、38 成功读取并保持；34、35、36 返回 E_INVALIDARG，不作为颜色验证成功证据。实际画图仅覆盖当前深色空白状态。

## 失败实验与修正

早期窄窗口轮 `native-boundaries-Release-r1` 至 `r5` 受到原生标题最小客户区宽度约束，未真正达到 125px，不计为边界通过。尝试 WM_GETMINMAXINFO 和 PreferredMinimumWidth 未解除该限制。随后修改 WM_NCCALCSIZE 右留白的版本虽达到宽度，但 presenter 切换后几何不一致，126px 附加被拒绝（r6、native-narrow-Release-r7），已撤回。最终仅在自有 fixture 放开 WM_WINDOWPOSCHANGING 尺寸约束。

`native-palette-Release-r1` 的前三组颜色检查通过，但恢复非默认标题选项失败，记录为 native-title-palette / HRESULT 8007139f。finally 关闭 fixture 后的 restored=1 不代表恢复成功。修复临时扩展标题设置路径后，r2 及最终完整回归通过，并新增独立原生渲染对照。

早期 keyboard r1/r2 尝试 TextBox，但默认资源未加载、根布局尺寸为零；r3 尝试资源字典后 fixture 未启动，均不计为键盘验证。最终改用显式模板的原 XAML Button，r4 通过；完整边界 r8 为 89 项，增加可读配色及独立原生对照后的最终 r9 为 107 项。上述失败目录保留，不重设基线或屏蔽截图像素。

## 交付与尚未完成的范围

新增脚本及本记录加入 VS 显式清单，项目与 filters 均为 213 项，无重复、通配符、缺失或清单差异。PowerShell 语法解析与 git diff --check 通过。用户原有 controller.cpp 两处 Beta 标题修改及根目录 debug.log 不纳入本次提交。

本轮代码仍属于独立研究，未启用正式通用 WinUI 后端。正式运行包保留 `out/WindowChanger-v34-0a931b9107b1.zip`，大小 2,438,465 字节，SHA256 再核验为 `CD602B4C9072A53AC17D8C953D8DCFBF2070C72ED10221F8E833358D81ABB029`；没有把研究 DLL 放入正式包，没有创建 GitHub Release。

全机软件覆盖、多岛屿、更多 ABI/DPI、跨屏、真实应用完整深浅主题、文本编辑/输入法、宿主重建及初始化销毁故障注入仍未完成。自有窗口边界与空白画图回归不足以替代这些验证。allInstalledSoftwareVerified、bothThemesVerified、allWindowTypesVerified 仍为 false。
