# v7 通用注入框架：本机兼容记录（2026-10-31）

真实应用窗口：9 个通过，3 个未适配，0 个验证失败。另排除 6 个系统/后台/辅助窗口。

环境：Windows 11 build 26100，x64，150% DPI；管理员控制器；全程没有使用 Computer Use。

运行时：`MacdowsStyleHook64-2607aa17b650.dll`。

## 通用范围

统一内核负责位数、权限、GUI 线程注入、Probe/Attach/Query/Detach、回滚和所有者生命周期。后端按已加载框架能力、控件语义及几何选择；不按应用名、产品版本或私有业务控件名分派。框架 ABI 限制仍然存在，不能把通用协议称为任意窗口兼容。

## 逐窗口结果

| HWND | 应用 | 框架 | 结果 |

| --- | --- | --- | --- |

| 0xa0cae | explorer.exe | Win32 | excluded |

| 0x6b0612 | ChatGPT.exe | Electron | captured |

| 0x230802 | explorer.exe | NetUI / Ribbon | captured |

| 0x2094a | WindowsTerminal.exe | WinUI / XAML Islands | unsupported-layout |

| 0x4f07ae | devenv.exe | WPF | captured |

| 0x9b062a | conhost.exe | Console host | captured |

| 0x2b0f2e | Universal x86 Tuning Utility.exe | WPF | captured |

| 0x3d156c | explorer.exe | Win32 Shell | captured |

| 0x309f2 | QQ.exe | Electron | captured |

| 0x8f0c0e | explorer.exe | NetUI / Ribbon | captured |

| 0x1e17e0 | explorer.exe | NetUI / Ribbon | captured |

| 0x3213a4 | Taskmgr.exe | WinUI / XAML Islands | unsupported-layout |

| 0x59047c | msedge.exe | Chromium Views | unsupported-layout |

| 0x9a0832 | SystemSettings.exe | UWP / Windows XAML | excluded |

| 0xc009cc | ApplicationFrameHost.exe | UWP / Windows XAML | excluded |

| 0x200e4 | cc-switch.exe | Win32 | excluded |

| 0xa0204 | TextInputHost.exe | UWP / Windows XAML | excluded |

| 0xa0c8a | explorer.exe | Win32 | excluded |

## 实测

x64 CTest 12/12、x86 CTest 6/6、分发包控制器 14/14 全部通过；SDK 示例独立编译通过。SDK 契约验证了显式/自动分派、拒绝后继续探测、所有后端拒绝时不回退到原生绘制，以及客户区和撤销。

9 个已接入窗口均通过首次绘制、稳定绘制、最大化、还原大小、黄灯最小化、绿灯最大化/还原和撤销。首次截图在进程内 Query 确认后约 100ms 采集；附加确认耗时记录在逐 HWND JSON 中。红灯关闭仅在自有示例测试，不关闭用户应用。

3 个 Explorer 窗口共用 NetUI 后端：原顶部行插入 spacer，原 QAT 和标题由原布局移动；原系统按钮隐藏，没有新增标题行/子 HWND。DWM 原透明缩放边缘已按窗口主题重绘，消除首次应用后的白边。边缘检查包含左/右/下侧原非客户区。

WPF 根据系统命令/可访问语义识别共同按钮容器；Electron 根据语义和原 drag 行识别，DOM 节点替换时重新发现最大化/还原控件。没有 VS/Wpf.Ui 控件名分支，也没有 QQ/Codex CSS 类名选择器。

所有已接入窗口客户区尺寸保持；控件树后端外框也保持。原生非客户区的外框厚度可能变化，撤销恢复；相关前后几何记录完整保留。

## 尚未适配

Windows Terminal：为遵守通用框架要求，旧版依赖该宿主 PE 身份、私有对象偏移和业务控件类型的入口已经移除。公开 IXamlDiagnostics/IVisualTreeService 初始化和订阅成功，但当前已有窗口没有枚举出匹配的 DesktopWindowXamlSource，未接管。不能把旧版的专用适配结果算作本版通过。

任务管理器：当前原树/根发现未通过；保持原样。Edge：原生 Chromium Views，没有可用的通用原控件树后端；不能把网页 DOM 当成浏览器原标签栏。

Qt 仅验证 6.11.1 x64 的 Widgets/Quick 示例布局。NetUI 仅验证当前渲染库 CodeView ABI。Electron 只验证 V8 major 13/15 的必要导出 ABI；定制初始 overlay 配色的完整恢复、页面刷新、软件升级和跨屏 DPI 尚未完成验证。

## 证据位置

工作区完整截图与指标：`out/live-v7/release-screenshots/report.md`、`report.json`。按钮输入：`out/live-v7/input-final/results.json`。分发包测试：`out/controller-direct-v7/checks.json`。完整屏幕截图不放入 ZIP，包内只附标题区对比图和指标。


## 最终启动检查

管理员控制器已启动，9 个已验证窗口均确认附加。4 个初始最小化窗口在恢复前未执行布局修改，恢复后自动选择正确后端；随后恢复原最小化状态。各窗口原状态恢复检查通过。证据：`out/live-v7/minimized-deferred.json`。
