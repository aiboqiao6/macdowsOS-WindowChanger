# macdowsOS WindowChanger

仓库：[aiboqiao6/macdowsOS-WindowChanger](https://github.com/aiboqiao6/macdowsOS-WindowChanger)，默认分支 `master`。

v32 默认自动接管所有受支持窗口：不设软件黑白名单，不按软件是否单独测试过来筛选，检测到支持即尝试注入。保留精简界面、托盘驻留、单实例、当前用户登录自启和自动接管持久化。使用方式见[运行说明](demos/window-styler/release-v32.md)。新版完整运行包为 `demos/window-styler/out/WindowChanger-v32.zip`；包含 x64 主程序、x86 辅助程序和匹配的 VC++ 可再发行运行库。

使用 Visual Studio 2022 打开根目录的 `macdowsOS WindowChanger.sln`，选择 `Debug/Release` 和 `x64/x86`，按 Ctrl+Shift+B 编译，F5 启动 `WindowStyler.exe`。程序要求管理员权限，调试时请以管理员身份启动 VS。

需要安装 VS 的「使用 C++ 的桌面开发」、Windows 10/11 SDK、C++ CMake 工具，以及 .NET 8 或更新的 SDK 和 .NET 8 桌面目标包。

解决方案通过 CMake 生成原生 MSVC 工程并编译所有实际模块。原 `macdowsOS ButtonChanger` 解决方案也可继续使用。默认不要求 Qt；Qt 后端需要匹配的 Qt 开发目录。WinUI 3 仍只对自有测试程序启用，其他框架的适配范围见 [项目说明](demos/window-styler/README.md)。

MFC/BCG、Office NetUI、TXGui 的已验证有限 ABI profile 已接入主控制器。TXGui 已完成原树左侧布局、原 D2D 三灯、原按钮动作及持续/异常恢复整合；使用与限制见[原树三灯说明](demos/window-styler/original-frameworks.md)和 [v30 记录](demos/window-styler/research/frameworks-v30.md)。Office 目前限起始页，不代表任意窗口或任意版本兼容。跨架构请使用包含 `x86` 目录的完整运行包。

```powershell
# 编译并运行现有集成测试
.\build.ps1 -Configuration Release -Test
.\build.ps1 -Architecture Win32 -Configuration Release -Test
.\build.ps1 -Configuration Debug

# 生成包含 x64 控制器和 x86 辅助程序的完整运行包
.\demos\window-styler\package-original-frameworks.ps1

# 生成可单独打开、逐模块调试的原生 VS 解决方案
.\build.ps1 -GenerateOnly
# demos/window-styler/out/vs-x64/MacdowsWindowStyler.sln

# 可选 Qt 与 WPF 示例
.\build.ps1 -BuildDirectory '.\demos\window-styler\out\vs-optional-x64' -QtRoot 'C:\Qt\6.11.1\msvc2022_64' -WpfSamples -Test

# 清理构建产物（保留源码、截图和历史研究结果）
.\build.ps1 -Action Clean

# 代码格式清理；自动备份本次修改前的 C++ 文件到 out/code-cleanup
.\scripts\cleanup-code.ps1
.\scripts\cleanup-code.ps1 -Check
```

新构建位于 `demos/window-styler/out/vs-x64` 和 `out/vs-Win32` 下，各自包含 `Debug`、`Release` 输出。旧目录中复制来的 CMake 缓存包含旧绝对路径，因此使用独立的新目录。

Debug 保留调试符号和关闭优化。XAML/WinUI 3 两个后端因需要与系统框架交换 C++ 容器，单独使用 release CRT 和 `_ITERATOR_DEBUG_LEVEL=0`，保持容器布局及分配器一致。

本机四种基础配置与可选 Qt/WPF 构建均已通过编译和现有回归；详见[验证记录](VERIFICATION.md)。
