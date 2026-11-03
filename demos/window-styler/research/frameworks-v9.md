# 框架入口研究（2026-10-31）

要求：进程内直接修改原窗口和原控件树，不按应用名、进程路径、业务控件名、宿主对象偏移分派；不新增跟随/遮罩 HWND，不增加标题行。管理员权限沿用控制器默认清单。

## Windows XAML Islands

本机 Terminal 与任务管理器都有 DesktopWindowXamlSource；二者共用 xaml-inline.cpp。Framework ABI 只允许 Windows.UI.Xaml.dll 的 CodeView `A5E5662D-B273-6140-B633-BC2D198B5A28` age 1，并验证入口指令。已载入框架的 `DirectUI::DXamlCore::GetCurrent`（RVA 0xe5fc4）确认 GUI 线程；`Diagnostics::DiagnosticsInterop::GetXamlIslandRoots`（0x86307c）枚举已有 source，随后用公开 IDesktopWindowXamlSourceNative 校验 HWND 与线程。

公开 IXamlDiagnostics 在本机只能发现少量内部诊断节点，不能据此得到应用原树。GetUiLayer 返回诊断覆盖层，不能拿来修改原窗口。公开 diagnostics 仍作为其他框架版本的后备，取不到原树时拒绝。

纯 XAML 根据关闭、最小化、最大化/还原的可访问语义、共同 Grid 和标题区几何识别。混合布局还验证真实 DWM caption bounds、窗口标题 TextBlock 与顶部 Island 尺寸。不会读取 Taskmgr/Terminal 对象内存或按其业务名称选控件。

纯 XAML 保存原标题 Grid 的最小高度与绑定，保留隐藏原按钮前的行高，防止最高的原按钮 Collapsed 后正文上跳；撤销恢复原依赖属性。动态改变原标题行高、跨屏 DPI 尚未验证。

混合布局研究中发现两类真实问题：插入列会破坏宿主按列索引更新的代码；替换 UserControl.Content 会触发 Unloaded，导致宿主释放仍在使用的标题控件。本机曾发生一次任务管理器 Windows.UI.Xaml.dll 0xc000027b 崩溃（16:57:48），失败版本已撤回。现方案保留父子关系与原列，仅为原 Grid 设置标题区 inset，三灯在这块空间内渲染。fixture 加入 Unloaded 生命周期检测，避免仅凭截图通过。

Margin 会将三灯放在原 Grid 的命中范围以外。尝试 subclass InputSite 改变命中测试触发 InputHost 0xc0000409（17:14:57），同样已撤回。现方案使用原 Grid.Padding，在保留原父子关系和列索引的同时把三灯留在 Grid 命中范围内；不挂钩 InputSite 的输入过程。

混合宿主还可能有位于 Island 上方的原生拖动窗口。根据同 GUI 线程、可见、覆盖三灯位置且三个采样点均返回 HTCAPTION 发现，排除 Island 及其输入子窗口，然后仅处理三灯区域内的鼠标。原窗口拖动和其他输入仍交还原过程。fixture 自带一个普通原生拖动窗口，验证该能力，不依赖任何产品窗口类。

合成表面的 GDI GetPixel 可能读到黑色，不能用于三灯背景。当前使用原树不透明 Brush 或框架 ThemeResource；后者随 XAML 主题资源更新。真实主题切换和跨屏 DPI 仍需额外验证。

## Chromium Views

本机 Edge 使用原生 Views；没有 Electron Node-API/V8 renderer 能力，不能复用 Electron DOM 后端。模块 `msedge.dll` CodeView `8DE675E4-DC25-455F-4C4C-44205044422E` age 1，当前 157.0.4341.24。

微软符号服务器对应 msedge.dll.pdb 路径（含大小写 key、pd_ 变体）返回 404。尚未取得可验证的 Widget → NonClientView → 原标题布局入口。UIA 能查看操作按钮，不提供修改原 Views 布局的 API，不能把 UIA 调用等同于完成注入。当前 Probe 拒绝，保持原样。

下一步需要可验证的 Views 框架 ABI：取得原 Widget/View 生命周期及布局管理器，通过窗口操作语义/标准 View 元数据识别操作组并在原布局预留空间，验证命中、焦点、可访问性与撤销。不得使用 BrowserFrameView、某个产品的标签栏对象偏移或强制恢复 Windows 标题栏作为完成方案。

## UWP / ApplicationFrame

本机设置为 ApplicationFrameHost 外壳 + 另一进程的 CoreWindow。Windows.UI.Xaml Islands 的 source 枚举不适用于该外壳。原标题包含返回操作、标题文本及系统按钮；只隐藏外壳按钮无法完成原内容腾位。

已取得 ApplicationFrame.dll 框架公开 PDB：CodeView `364A364E-553D-E50F-96EA-7A71CEA2E676` age 1。只读检查确认 ApplicationFrameTitleBarWindow 与 ApplicationFrameInputSinkWindow 的额外窗口字节指向同一 CTitleBar；实例首虚表 RVA 0x76a40。尚未对其进行写入，也未注册一个假成功后端。

可继续研究的框架方法（RVA）：

| 方法 | RVA | 用途 |
| --- | --- | --- |
| CWRLImpWndProc<CTitleBar>::s_WndProcBase | 0x2df70 | 框架窗口实例与生命周期 |
| CTitleBar::GetVisibleButtons / SetVisibleButtons | 0x39570 / 0xe110 | 原操作按钮可见性 |
| CTitleBar::GetTitleBarRects | 0x33170 | 原标题及输入布局 |
| CTitleBar::GetFrameWindow / GetInputSinkWindow | 0x42350 / 0x5a420 | 原框架窗口与输入 sink |
| CTitleBar::_GetDCompProvider | 0x1aad0 | 原合成设备提供者 |
| CTitleBar::_InitializeDCompVariables | 0x1adf0 | 原 visual tree 结构 |
| CTitleBar::_ComputeLayoutRects / _ComputeHitTestRects | 0x24b40 / 0x25b04 | 布局与输入协调 |
| CTitleBar::_PlaceVisuals / _PaintTitle | 0x247d0 / 0x21a70 | 原合成布局与标题绘制 |

这些是框架内部 ABI，必须先校验模块、虚表、函数与接口身份，再验证原 visual、跨进程保留区域及撤销。不能凭静态符号位置直接假设 COM 接口或实例字段可写。此框架当前未适配，设置截图仅作原始基线。

研究原始资料在 out/live-v9：applicationframe-symbols.json、applicationframe-methods.txt、applicationframe-asm.txt、frame-objects.json、pdb-publics.py。公开符号来自微软符号服务器；Windows XAML 实现签名同时核对 microsoft-ui-xaml diagnostics 源码。
