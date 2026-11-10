# v36 WinUI 独立标题岛屿实验验证

日期：2026-11-10。研究模块修订为 `2bb04a8bc701`，研究引导修订为 `e24a9aea7b76`。正式主运行时仍为 `0a931b9107b1`。本轮通过原 GUI 线程、原生消息、鼠标输入和脚本截图验证，没有使用 computer use 或子代理。实际窗口截图、标题、对象标识和私有脚本均只保存在忽略的 `out/live-v36`。

## 实现范围

新增独立 `WinUI3NativeIslandFixture`、`WinUI3NativeIslandProbe` 和限时 `WinUI3NativeIslandHost`，仅存在于 research/framework-probe 构建中。研究 adapter 288 要求精确 Microsoft.UI.Xaml CodeView `4ACDBE85-D668-192E-B8B6-A9334AC91B79` age 1，并在原 GUI 线程校验 144 DPI、LTR、非最大化/最小化、原生标题及唯一完整客户区岛屿。没有按应用名称或路径选择目标，没有添加软件黑白名单；多个岛屿的记事本不满足此入口。

Host 始终使用独立标题岛屿。通过 AppWindow/OverlappedPresenter 公共接口保留边框、移除原生标题，在新获得的顶部客户区创建第二个 DesktopWindowXamlSource 绘制真实 XAML Button 三灯。原 Content 保留，不包裹、卸载或修改 Margin/Row；原岛屿保留尺寸与屏幕位置。最大化时扣除不可见缩放边框。

原标题按钮查询报告隐藏、不可用、离屏且矩形为空；前置输入过滤器阻止旧非客户区按钮消息。三灯通过原 HWND 系统命令操作，并检查原能力。标题跟随原根 ActualTheme 和 Background；撤销时注销观察。

撤销先关闭自建标题岛屿，再恢复原生 presenter。TitleBar.ResetToDefault 恢复原标题绘制，随后恢复保存的非空配色及有差异的图标、高度、主题属性，恢复原输入区域与岛屿位置。最小化时仅恢复原岛屿位置，保留 viewport 尺寸。初始化时延后重入释放，恢复时防止重复释放；失败不 ACK、不丢弃会话，保留定时器重试。宿主更换原根时不覆盖其新 Content。

## 最终构建与自有窗口回归

x64 Release、Debug 两种配置均构建成功。以下结果均对应最终模块，旧版本的成功记录不替代本轮结果。

| 配置 | 深浅色窗口回归 | 外部注入与生命周期回归 |
| --- | --- | --- |
| Release | 60/60 | 56/56 |
| Debug | 60/60 | 56/56 |

窗口回归覆盖深浅色、原正文几何和像素、原标题文字/配色恢复、旧按钮禁用、缩放、最大化、反复接管撤销及三灯真实鼠标点击。外部注入覆盖正常退出、控制器进程异常退出、最小化时控制器退出且不改变最小化状态/viewport、根替换后保留新根、宿主关闭后的会话清理，以及不发送原生主题通知的应用内部亮暗切换。

证据目录为 `native-title-{Release,Debug}-lifecycle-final` 和 `native-lease-{Release,Debug}-lifecycle-final`。对应 fixture SHA256：

- Release：`427C2C271F6D181B4F88FD52E17F6B43968CF9B8D1A95F22F04DF667E38CFF0C`
- Debug：`DEF1F167106CE7D1C2EAB689388952D9F716A3B732647A608E128C1E00BA3412`

研究 DLL SHA256：

- Release：`59FD59603494689BFFF96FA101F37D9419C6EAF851AA08B8EBCA018C05EEF37E`
- Debug：`25FB3DD355BB9A3A1049ABB6AA417F2639051BDC03D3F96D72D0791C2BEDEDC1`

## 真实空白画图

在自己创建、核验进程路径和空白标题的独立画图窗口中，Release/Debug 各完成正常退出及控制器进程被终止两种测试。没有关闭或重启用户已有应用，没有修改系统或画图主题。

四轮均确认外框保持、客户区仅向上扩展、原正文像素不变，三灯各出现 212 个精确色像素，原按钮查询隐藏禁用，旧 NC 三按钮输入无效。正常研究控制器退出码为 0；异常退出轮主动终止的研究控制器退出码为 -1，目标随后自动恢复。四轮撤销后的外框、客户区、能力和 owner 均恢复；完整窗口 PNG 与接管前逐字节一致，因此包含原标题文字、图标、按钮和正文的全窗像素差异为 0。截图已查看，工具栏、画布和状态栏没有重叠或位移。测试所建空白画图均正常关闭。

最终证据：

- `paint-title-Release-lifecycle-normal-final`
- `paint-title-Release-lifecycle-crash-final`
- `paint-title-Debug-lifecycle-normal-unoccluded-r2`
- `paint-title-Debug-lifecycle-crash-final`

DWM 属性 20、33、38 成功读取，接管和恢复前后保持。34、35、36 返回 E_INVALIDARG，不能作为颜色属性验证成功的证据。实际画图仅验证当前深色空白状态，不替代真实应用的完整主题、文档和编辑状态矩阵。

## 失败实验与对照

修改原根 Margin 曾令真实画图画布高度改变 1 像素，正文产生 11,780 个差异像素；该方式不用于实际窗口。无注入对照中，原尺寸 WM_SIZE 和 frame-changed 不改变像素，先增加高度再恢复同样产生 11,780 个差异。因此没有重设原基线来掩盖接管重排。

早期恢复正文相同，但原标题文字和图标消失，整窗有 1,314 个差异像素；重绘、激活和 frame-changed 均未解决。无条件设置默认图标/标题高度的一版还发生恢复失败，已撤回。最终改用 ResetToDefault 后仅设置有差异的属性；最终四轮全窗零差异包含对此问题的验证。失败实验窗口经独立身份核验后正常关闭，恢复失败不计为成功。

Debug 最初的 `paint-title-Debug-lifecycle-normal-final` 撤销截图右下角被 QQ 托盘提示遮挡，正文有 6,061 个差异像素。该轮保留为失败证据。私有截图脚本改为将鼠标暂放自有窗口空白标题区，结束后恢复指针；不裁剪或屏蔽差异。独立新窗口重测 `lifecycle-normal-unoccluded-r2` 通过。

## 交付与边界

新增源码、脚本和记录加入 VS 显式清单；项目与 filters 均为 211 项，无重复、通配符、缺失或清单差异，两个 PowerShell 脚本语法解析通过，`git diff --check` 通过。独立研究注册表不会进入正式 adapters.ini；正式控制器的自动接管策略未改变。正式运行包仍为 `out/WindowChanger-v34-0a931b9107b1.zip`，2,438,465 字节，SHA256 再次核验为 `CD602B4C9072A53AC17D8C953D8DCFBF2070C72ED10221F8E833358D81ABB029`。此前独立解包核验见 v34 记录；本轮没有把实验 DLL 塞入该包，也没有创建 GitHub Release。

全机覆盖仍未完成。多岛屿、更多 ABI、全部 DPI、跨屏、窄窗口、键盘焦点、宿主重建、接管期间宿主改写标题属性、真实应用完整深浅主题矩阵及初始化泵消息期间的销毁故障注入仍待验证。有限空白画图测试不足以启用正式通用 WinUI 后端。`allInstalledSoftwareVerified`、`bothThemesVerified`、`allWindowTypesVerified` 均仍为 false。
