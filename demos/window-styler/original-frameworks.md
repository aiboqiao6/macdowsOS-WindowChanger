# MFC/BCG、Office NetUI、TXGui 原树三灯

当前已将三个已验证 profile 接入主控制器。不是任意版本、任意 Office 窗口的通用兼容保证；未知 ABI、未知树结构、空间不足明确拒绝，不使用覆盖窗口或原生标题栏兜底。

## 使用

以管理员身份启动完整包根目录的 `WindowStyler.exe`，默认自动接管检测为支持的窗口，不设软件黑白名单；也可手动选择并应用。撤销会暂停自动接管。保留旁边的 `x86` 目录，x64 控制器通过它处理 TXGui 的 32 位进程。当前 TXGui profile 不接受从最小化或最大化状态开始附加，恢复显示后再尝试。

命令行也可使用 `WindowInjector.exe --apply 0x当前HWND`；可加 `--duration 20` 自动结束。独立命令 `WindowInjector.exe --detach 0x当前HWND` 撤销。控制器退出、owner 消失或控制器异常终止会恢复原布局。

源码工程仍从根目录 `macdowsOS WindowChanger.sln` 打开，支持 x64/x86、Debug/Release 和 F5。调试请以管理员身份运行 VS。四种配置分别构建到 `out/vs-x64`、`out/vs-Win32`；跨架构使用打包输出，不要只复制一个 EXE。

```powershell
.\build.ps1 -Configuration Release
.\build.ps1 -Architecture Win32 -Configuration Release
.\demos\window-styler\package-original-frameworks.ps1
# demos/window-styler/out/WindowChanger-v34/WindowStyler.exe
```

## 当前边界

| 后端 | 已验证 profile |
| --- | --- |
| MFC/BCG x64 | 原 BCG Ribbon 标题按钮，CodeView `73D590B1-7FA3-4F9D-9CE2-E2A6D801C11C` age 1；原 runtime class、命令、绘制和布局 ABI 校验 |
| Office NetUI x64 | 本机 Office 模块三重 CodeView 校验，144 DPI、LTR；`FullpageUIWindow` 起始页，以及匹配双层标题容器和快捷工具栏/标题分组的 `HWNDElement` 编辑窗口；已验证空白 Word、Excel，其他结构或标题空间不足拒绝 |
| TXGui x86 | GF CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63` age 24，144 DPI、LTR、完整树和标题/header 几何匹配；空间不足拒绝 |

TXGui 迁移的是三枚原 Button，保留原点击绑定与原 HWND 输入路径；没有直接调用宿主回调。原 D2D 内容阶段画三灯，普通重排处理窗口缩放；定时器只检测所有者/树失效，不跟随布局。关闭按钮不在自动验证中点击，实际关闭或托盘行为沿用宿主原按钮策略。

Debug/Release 引导 DLL 已区分名称，可在不重启目标应用的情况下先后验证；旧的驻留 DLL 不表示仍有活动会话。v33 允许 Office/TXGui 在原树仍满足契约时处理主题/设置通知并重绘；原树失效或 DPI 改变仍退出恢复。通知刷新测试不等于真实亮暗主题切换验证，不承诺未知皮肤和版本持续兼容。

详细原理、测试结果、失败记录和范围见源码中的 `research/frameworks-v30.md`。

v34 的编辑标题使用原容器内边距为三灯保留 126 个物理像素，由原布局引擎重排快捷工具栏、标题、搜索及账户区。原按钮和 glyph 隐藏，按钮设为原生 Boolean false，移除旧 caption part/hit；Windows 标题栏无障碍查询也报告隐藏、不可用和空矩形，旧非客户区按钮消息不再操作窗口。撤销恢复原属性与节点顺序；窗口过窄时恢复原布局。验证范围及待完成项见 `verification-v34.md`。
