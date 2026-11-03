# WinUI 3 / MFC-BCG / Office NetUI / TXGui 研究进度

本轮属于框架原型与兼容性研究。四类尚未全部适配。原先运行的 v13 管理员控制器继续负责已验证窗口，未替换为实验控制器，也未重启用户应用。

## 实际完成范围

| 框架 | 本轮结果 | 当前可用范围 |
| --- | --- | --- |
| WinUI 3 | 新的原树后端、通用注入链路、独立演示和真实输入验证 | 自有 fixture；未对用户应用启用 |
| MFC / BCG | 核对当前框架窗口、模块、磁盘映像和 HWND 对象入口 | 尚未完成布局后端 |
| Office NetUI | 确认现有 NetUI 根对象及实际实现模块、提取导出/CodeView | 尚未完成元素遍历、插入和恢复 |
| TXGuiFoundation | 补齐 32 位模块盘点，找到 GFSpy 框架导出并核验读取函数的机器码 | 尚未完成原树修改、绘制和生命周期 |

载入模块、找到导出、读取对象或识别窗口类均不计作适配成功。

## WinUI 3 原树原型

代码为 `winui3-inline.cpp`，注册编号 266。选择依据是框架 CodeView、公开接口、原首行几何与既有 Caption 输入区域；不按程序名、路径或业务控件名称选择。研究范围限制通过 `Macdows.WindowStyler.Sample` 属性实现，注册表和两个导出均检查。真实应用缺少此标记，后端返回不支持。

根发现只接受 Microsoft.UI.Xaml.dll CodeView `4ACDBE85-D668-192E-B8B6-A9334AC91B79`、age 1，并校验 `GetXamlIslandRoots` 入口字节。调用在目标 GUI 线程。没有使用导致上轮故障的系统 Windows.UI.Xaml diagnostics 连接。WindowId 必须对应目标 HWND，避免同一 GUI 线程上误选其他窗口的根。

附加要求窗口已扩展内容到原标题行、有唯一符合几何和原拖动区域的 Grid、使用 OverlappedPresenter、具备经过测试的 DPI 感知方式。后端在这个 Grid 中增加 84 DIP 布局列和三个 XAML Button，原有子元素列索引右移。宿主原 Padding、绑定、行高、主体 Grid 和正文位置保持原有控制权；不新增 HWND 或标题行。

通过公开 IOverlappedPresenter ABI 的实例 vtable 保存并约束 `SetBorderAndTitleBar`，宿主重新开启原按钮时在原调用中隐藏，保存最新请求供撤销恢复。没有修改共享 vtable。原输入区域通过 InputNonClientPointerSource 的 Passthrough API 为三灯开放；更新来自原布局事件。750 ms 定时器仅检查所有者存活，不跟随、移动或绘制目标窗口。

恢复移除三灯和布局列，撤销附加属性、输入区域及 Presenter 接口修改，并恢复原属性局部值。原标题树卸载时退出旧会话，后续附加重新发现新树。绑定了 Grid.Column 的布局和未知 ABI 均拒绝；当前原型还没有广泛应用布局、其他 DPI、RTL、全屏和不同 Windows App SDK 构建的生产验证。

最终构建 revision：`5902747afe39`。x64 CTest 16/16、x86 7/7 通过。`winui3_fixture_contract` 内有 92 项断言，包括：

- 普通首行、多列首行、Padding 绑定三种独立原始布局。
- 首次附加外框/客户区、正文位置和 HWND 数量不变；原内容在同行右移。
- 原生鼠标真实点击红/黄/绿灯；最大化、还原、最小化、关闭自有 fixture。
- 宿主 12 次重新写入 Padding、原按钮策略和列索引，不累积预留宽度。
- 主题属性切换、原首行从 48 DIP 改为 32 DIP；原 Padding 绑定在附加期间及撤销后继续工作。
- 控制器终止后恢复最新宿主状态；旧树替换后撤销，再附加新树。
- 无标记、没有合适首行、绑定列索引、DPI unaware 四种真实 WinUI 3 fixture 被拒绝，未产生会话或修改几何/HWND 树。

证据：`out/build-x64/winui3-contract/`、`out/live-v14/ctest-{x64,x86}-final.log`；截图拼图为 `images/winui3-framework-prototype-v14.png`。早期失败检查保留在 `out/live-v14/winui3-contract*` 和 `winui3-policy*`，不计作最终通过。

独立包为 `out/WinUI3-framework-demo.zip`，运行其中 `WinUI3Demo.exe`。原生演示宿主只查找自己新建进程中的标记窗口，没有全桌面接管、位置跟随或两分钟演示期限。包内实例的三灯再次通过真实点击，fixture 与演示宿主退出码均为 0；记录为 `out/live-v14/package-validation.json`。最初封包检查因未提前打开退出进程句柄而读到空退出码，保留为 `package-validation-first.json`；重新获取句柄后的独立重试通过。

## MFC / BCG

本机样本仍为 BCGPRibbonBar 与 BCGPRibbonStatusBar 框架窗口，静态链接在宿主映像。磁盘 PE 多个执行段没有原始代码数据，不能把磁盘偏移直接当作加载后实现。CodeView `73D590B1-7FA3-4F9D-9CE2-E2A6D801C11C`、age 1。已读取的顶层/BCG HWND 的额外数据和 UserData 不提供直接 CWnd / Ribbon 根。

缺口是稳定获取框架 Ribbon/Frame 对象、识别原系统操作语义、调用其布局及绘制/失效入口，并恢复原对象生命周期。未写对象偏移，未切换回普通 Win32 标题样式。

## Office NetUI

实际 NetUIHWND 在 offset 0 保存对象；当前根 vtable 分别位于 mso.dll 和 mso40uiwin32client.dll，不能套用系统 UIRibbon.dll 的对象模型。UI 实现库 CodeView `2D264595-7EA8-4318-8AA1-897691F9D8EE`、age 2；MSO 为 `609D7177-47D3-4508-B59D-AB1FD4FB1624`、age 2。

UI 库有 7,865 个导出槽，但仅 4 个具名导出，均不提供标题布局 API。当前公共符号请求返回 404；实际根没有可直接解析的 RTTI。尚未验证子元素枚举、布局 spacer、原按钮策略及撤销，不调用已知系统 Ribbon RVA。

## TXGuiFoundation

以 32 位进程视角重新盘点后，发现实际 GF.dll。CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`、age **24（十进制）**，符号键后缀为 `18`（十六进制）。已修正 `pe_inspect.py` 对 age 大于 9 的符号键格式。

GF.dll 的 241 个具名导出包含框架 API：

- `GFSpyFuncHelper::GetWindowByHWnd(HWND, IGFPopupWin**)`
- `GFSpyFuncHelper::GetFrameCount(IGFFrame*, unsigned*)`
- `GFSpyFuncHelper::GetFrameByIndex(IGFFrame*, int, IGFFrame**)`
- `GFSpyFuncHelper::GetFrameInfo` / `SetFrameAttribute`
- `Util::GF::RawCreateGFElementByXtml` / `RawCreateGFObjectByName`

反汇编确认 GetFrameCount / GetFrameByIndex 使用 IGFFrame 虚函数和正确 x86 调用约定。GetWindowByHWnd 依赖内部服务；不能在未核验服务及 IGFPopupWin → IGFFrame 接口关系前直接调用，更不能猜测对象布局。当前没有对用户进程执行这些入口，也没有伪装绘制三灯。

## 复现

本机 SDK 生成输入可用 `prepare-winui3-sdk.ps1 -PlanOnly` 查看。该计划检查已通过；新生成过程没有在本轮覆盖现有构建头文件。启用 CMake 的 `STYLER_WINUI3_EXPERIMENT` 并具备 `out/winui3-sdk` 投影后，可构建 WinUI3Demo / WinUI3InlineTarget，再运行 `test-winui3-inline.ps1`。

只读研究工具 `read-framework-objects.py` 提取 HWND 对象、vtable 和模块归属；`inspect-framework-library.py` 提取 CodeView、导出及 RTTI 字符串。原始报告均位于 `out/live-v14/`，不会据此自动启用后端。
