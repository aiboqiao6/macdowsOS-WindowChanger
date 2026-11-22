# macdowsOS WindowChanger v43（验证候选）

当前为验证候选版本。WinUI 标准 TextBox 历史上的三个圆角灰度差异像素已在从未注入的原生对照中原样复现；Debug/Release 深浅色正常和异常退出四场景分别完成 896 项。底层像素变体机制尚未定位，历史失败保留，详见 `verification-v46.md`。全部已安装软件、全部窗口类型及实际亮暗主题覆盖仍未完成。

本机完整候选 `WindowChanger-v43-9751c3cbb38a-candidate.zip` 的运行时为 `9751c3cbb38a`；这不是 CI core-only 产物的运行时编号。新增 WinUI 原生标题后端的初始内容源探测防护：先排除关闭 bridge，再读取 Content，并处理空 XamlRoot。当前候选已完成包内实际管理员生命周期 138 项、自动接管及正常/异常恢复 60 项、四配置完整 CTest 84 项和 Debug 边界/焦点/配色 107 项。管理员组覆盖托盘、最高权限交互登录任务注册及 TaskScheduler.Run，没有实际重启或重新登录。历史三个像素的变体已在未注入原生对照复现，当前 Debug/Release 文本四场景矩阵完成；WinUI 其他布局和全机适配仍未完成。新增后台生命周期入口明确跳过视觉检查，不用后台通过替代截图验收。详见源码中的 `verification-v46.md`、`verification-v44.md`。

适用于 Windows 的仿 macOS 通用窗口样式注入框架。解压后以管理员身份运行 `WindowStyler.exe`，保留旁边的 `x86` 目录。默认自动接管所有检测为支持的窗口，不按软件名或是否单独测试过筛选，没有黑白名单。关闭主界面后驻留托盘；开机自启需在界面或托盘主动开启。手动全部还原会暂停自动接管，重新开启后再次检测。

本版把已验证的 WinUI 3 单岛屿原生标题实现接入正式自动探测。通过公共 AppWindow API 移除原生标题，在腾出的标题空间中创建 XAML 三灯；保留原正文、尺寸和屏幕位置。原按钮隐藏、禁用且标题查询矩形为空；布局空间不足、原根改变或控制器退出时撤销。后端仍要求匹配 Microsoft.UI.Xaml CodeView、x64、144 DPI、LTR、带原生边框及标题，并且恰好一个填满客户区的岛屿。未知 ABI、多岛屿和自定义标题布局不满足此入口。

本轮补齐独立标题岛屿已被宿主关闭时的安全查询和恢复。`b6ad8edbc843` 完整候选包于 2026-11-22 完成三组管理员生命周期验证，共 138 项；包内实际控制器的 WinUI 接管及正常/异常恢复共 60 项、Release 边界/焦点/配色组 107 项通过。Release 文本首次在完整窗口像素比较失败，最接近原生对照有三个灰度像素每通道相差 1；后续四场景分别复测共 896 项通过，Release 根/立即源重建/延迟源重建共 832 项通过。但原像素失败保留，波动原因未确定，生产代码没有因此修改。Debug、实际软件与其余矩阵仍待完成。没有实际重启或重新登录系统。旧候选包的结果不会移用到新包，详细范围与失败记录见源码中的 `verification-v43.md` 和 `verification-v42.md`。

真实应用证据覆盖本机画图 11.2605.91.0 当前深色 PNG 文档的绘制、保存、撤销/重做、最大化/还原及正常/异常恢复；自有标准 TextBox、主题与宿主重建契约另行验证。它不等于全部 WinUI 应用或画图全部工具均已适配。

保留 Qt、WPF、Windows XAML Islands、ApplicationFrame/UWP、Chromium Views、Electron、MFC/BCG、Office NetUI、TXGui 和 Avalonia 的既有有限 ABI/布局后端。适配按实际结构与能力选择；不能把软件库存或 DLL 载入算作适配通过。微信内嵌 Qt、Picker、其他 WinUI 布局、更多业务窗口/ABI/DPI/主题及 IME 仍有缺口，全部已安装软件的兼容尚未完成。范围与证据见源码中的 `verification-v42.md`、`verification-v34.md` 和 `original-frameworks.md`。

实际打包后端以 `runtime-features.json` 和两种架构的 `adapters.ini` 为准。CI 的 `core-only` 编译包缺少本机 SDK 构建的 Qt 与正式 WinUI 3 后端，不能代替本机完整运行包。

2026-11-23：当前电脑的 Office `16.0.14334.21048` 已超出本包旧版 Office profile。新版 Word、Excel、PowerPoint 在独立研究后端的 Debug/Release 后台事务和当前黑白主题真实按钮验证已推进，Word/Excel 交互后的节点重建另有原生对照；未加入本包默认自动接管。详情见源码中的 `verification-v46.md`、`verification-v45.md`。

包内包含 x64/x86 的 VC++ 可再发行运行库，不含测试程序、软件库存、截图或私有诊断。源码与 GPLv3 授权见 [GitHub 仓库](https://github.com/aiboqiao6/macdowsOS-WindowChanger) 及 `LICENSE.txt`；MinHook 授权见 `third_party/minhook-LICENSE.txt`。
