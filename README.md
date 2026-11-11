本项目隶属于 [macdowsOS](https://github.com/aiboqiao6/macdowsOS)

# macdowsOS WindowChanger

适用于 Windows 的仿 macOS 通用窗口样式注入框架。以框架能力和原控件树识别窗口，替换标题按钮并为工具栏保留空间；默认自动接管检测为支持的窗口，没有软件黑白名单。

完整运行包解压后，以管理员身份运行 `WindowStyler.exe`，并保留旁边的 `x86` 目录。关闭主界面后驻留托盘；可在界面或托盘开启开机自启。“暂停并全部还原”会同时暂停自动接管，重新开启后再次检测。原窗口按钮在接管期间隐藏并禁用，退出或控制器异常终止后恢复原布局。

源码从 [macdowsOS WindowChanger.sln](macdowsOS%20WindowChanger.sln) 打开，支持 x64/x86、Debug/Release；F5 调试需要以管理员身份启动 Visual Studio。完整 WinUI/Qt 构建还需准备相应 SDK，具体方法见 [构建与后端说明](demos/window-styler/README.md)。

已有 Win32、Qt、WPF、Windows XAML、Chromium Views、Electron、MFC/BCG、Office NetUI、TXGui 和 Avalonia 等后端，适用范围受实际 ABI、布局、权限和架构约束。全部已安装软件、全部窗口类型和亮暗主题的覆盖尚未完成；库存或探测成功不等于适配通过。当前范围及未关闭的问题见 [v42 验证记录](demos/window-styler/verification-v42.md)，运行包内容见 [运行说明](demos/window-styler/release-v42.md)。

<img width="2734" height="1367" alt="image" src="https://github.com/user-attachments/assets/359680b9-0f09-4319-9a85-27072e7f1087" />
