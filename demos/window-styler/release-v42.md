# macdowsOS WindowChanger v43（验证候选）

当前为验证候选版本。WinUI 标准 TextBox 的整窗像素对照曾间歇出现三个圆角灰度像素各相差 1，原因尚未关闭；新修订正在重新验证。全部已安装软件、全部窗口类型及实际亮暗主题覆盖仍未完成。

本包运行时为 `9751c3cbb38a`。新增 WinUI 原生标题后端的初始内容源探测防护：先排除关闭 bridge，再读取 Content，并处理空 XamlRoot。同一自有窗口关闭源、保留旧源引用、创建空源及重建原内容的深浅色契约通过；x64 Debug/Release 后台用例各 11/11，x86 各 3/3。新修订尚未完成包内管理员生命周期、托盘、自启或前台矩阵，下文 `b6ad8edbc843` 的历史结果不能作为本包验收。Debug 前台文本组因原生对照失去前台而中断，未计通过。

适用于 Windows 的仿 macOS 通用窗口样式注入框架。解压后以管理员身份运行 `WindowStyler.exe`，保留旁边的 `x86` 目录。默认自动接管所有检测为支持的窗口，不按软件名或是否单独测试过筛选，没有黑白名单。关闭主界面后驻留托盘；开机自启需在界面或托盘主动开启。手动全部还原会暂停自动接管，重新开启后再次检测。

本版把已验证的 WinUI 3 单岛屿原生标题实现接入正式自动探测。通过公共 AppWindow API 移除原生标题，在腾出的标题空间中创建 XAML 三灯；保留原正文、尺寸和屏幕位置。原按钮隐藏、禁用且标题查询矩形为空；布局空间不足、原根改变或控制器退出时撤销。后端仍要求匹配 Microsoft.UI.Xaml CodeView、x64、144 DPI、LTR、带原生边框及标题，并且恰好一个填满客户区的岛屿。未知 ABI、多岛屿和自定义标题布局不满足此入口。

本轮补齐独立标题岛屿已被宿主关闭时的安全查询和恢复。`b6ad8edbc843` 完整候选包于 2026-11-22 完成三组管理员生命周期验证，共 138 项；包内实际控制器的 WinUI 接管及正常/异常恢复共 60 项、Release 边界/焦点/配色组 107 项通过。Release 文本首次在完整窗口像素比较失败，最接近原生对照有三个灰度像素每通道相差 1；后续四场景分别复测共 896 项通过，Release 根/立即源重建/延迟源重建共 832 项通过。但原像素失败保留，波动原因未确定，生产代码没有因此修改。Debug、实际软件与其余矩阵仍待完成。没有实际重启或重新登录系统。旧候选包的结果不会移用到新包，详细范围与失败记录见源码中的 `verification-v43.md` 和 `verification-v42.md`。

真实应用证据覆盖本机画图 11.2605.91.0 当前深色 PNG 文档的绘制、保存、撤销/重做、最大化/还原及正常/异常恢复；自有标准 TextBox、主题与宿主重建契约另行验证。它不等于全部 WinUI 应用或画图全部工具均已适配。

保留 Qt、WPF、Windows XAML Islands、ApplicationFrame/UWP、Chromium Views、Electron、MFC/BCG、Office NetUI、TXGui 和 Avalonia 的既有有限 ABI/布局后端。适配按实际结构与能力选择；不能把软件库存或 DLL 载入算作适配通过。微信内嵌 Qt、Picker、其他 WinUI 布局、更多业务窗口/ABI/DPI/主题及 IME 仍有缺口，全部已安装软件的兼容尚未完成。范围与证据见源码中的 `verification-v42.md`、`verification-v34.md` 和 `original-frameworks.md`。

实际打包后端以 `runtime-features.json` 和两种架构的 `adapters.ini` 为准。CI 的 `core-only` 编译包缺少本机 SDK 构建的 Qt 与正式 WinUI 3 后端，不能代替本机完整运行包。

包内包含 x64/x86 的 VC++ 可再发行运行库，不含测试程序、软件库存、截图或私有诊断。源码与 GPLv3 授权见 [GitHub 仓库](https://github.com/aiboqiao6/macdowsOS-WindowChanger) 及 `LICENSE.txt`；MinHook 授权见 `third_party/minhook-LICENSE.txt`。
