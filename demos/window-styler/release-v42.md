# 适用于 Windows 的仿 macOS 通用窗口样式注入框架

解压完整文件夹，以管理员身份运行 `WindowStyler.exe`，保留旁边的 `x86` 目录和运行库。

默认自动接管所有检测为支持的窗口，不按软件名称或是否单独测试过筛选，不设黑白名单。标题按钮和工具栏由对应框架的原布局处理。未知 ABI、未知结构、空间不足或窗口不具备所需能力时拒绝接管。

关闭主界面后继续驻留托盘；从托盘退出会还原本次接管的窗口。开机自启可在界面或托盘开启，使用当前用户的最高权限交互登录任务。手动全部还原会暂停自动接管，重新开启后再次检测。

已接入 Qt、WPF、Windows XAML Islands、ApplicationFrame/UWP、Chromium Views、Electron、MFC/BCG、Office NetUI、TXGui、Avalonia 及有限的 WinUI 3 原生标题布局。各后端受其结构、ABI、DPI 和布局条件约束。包内实际启用的后端见 `runtime-features.json`；CI 的 core-only 产物缺少本机完整候选中的可选 SDK 后端。

当前源码为 Office `16.0.14334.21048` 新增独立 x64 profile，保留旧 Office profile。新版 Word、Excel、PowerPoint 的研究验证覆盖当前 144 DPI、LTR、黑白主题和自建空白文档。正式候选接管、原按钮隐藏禁用及撤销验证继续记录在 `verification-v47.md`；此前的完整原树、物理按钮和主题证据见 `verification-v46.md`、`verification-v45.md`。

全部已安装软件、全部文档和全部窗口类型的兼容仍未完成。WinUI 标准 TextBox 历史上的三个灰度差异像素已在从未注入的原生对照中原样复现；Debug/Release 黑白主题、正常撤销和异常退出四场景分别完成 896 项，完整规则和历史失败见 `verification-v46.md`。不能用后台事务通过代替真实绘制验证。

源码从根目录 `macdowsOS WindowChanger.sln` 打开，支持 x64/x86、Debug/Release 和 F5；调试时请以管理员身份运行 VS。原树后端范围及工具栏避让方式见 `original-frameworks.md`。测试记录属于源码；账号相关截图、软件库存和私有验证数据不随程序发布。
