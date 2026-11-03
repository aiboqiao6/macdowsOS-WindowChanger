# v18：Office 原树左侧三灯、TXGui 原事件绑定

2026-11-02。Office 本机起始页的原树左侧三灯、原消息输入与撤销已通过有限事务验证；验证后恢复窗口，尚不是持续附加后端。TXGui 仍未完成当前可见标题组关联和三灯布局。本轮未使用 computer-use。

![Office 原框架三灯的实际窗口裁切](../images/office-netui-v18.png)

## Office

沿用 v17 三个 Office 模块 CodeView，只接受唯一活动按钮组、`FullpageUIWindow` 起始页及已核验的原 Element 包装节点。当前为 x64、150% DPI，不代表文档/工作簿编辑窗口已适配。

AppFrameHelper 的 IClassInfo 工厂入口为 UI 库 RVA `0x1c1180`。由原工厂创建三个真实框架按钮，赋予原 FramePart、SC_CLOSE / SC_MINIMIZE / SC_MAXIMIZE 和 HitTest 属性；原 Content、字体、Foreground 管线绘制圆点，原 InsertNodes 将它们放入本来空置的左侧标题区域。没有新 HWND、标题行、外部绘制层或跟随定位定时器。

原按钮与子树保留，通过原本地属性临时隐藏绘制及命中；新节点由原 Remove / Destroy 撤销。试验期间使用原框架引用保持节点生命周期。原始样式属性、原节点顺序和布局均回读恢复。

Office 的 DWM 最小化/最大化按钮独立于 WS_SYSMENU。共享样式读取层新增可选的 CaptionBox 位视图：真实窗口临时去掉系统菜单和最小化/最大化按钮位，Office 仍读取原能力及 caption bounds；保留外框、客户区和 DWM 非客户区。样式通知分别保留旧值与宿主新请求，撤销保留宿主最新能力位。未设置该选项的现有后端维持原菜单行为。

原 HWND 将三灯命中区交给客户区输入；原 NetUI 按钮处理鼠标消息，原分派器产生系统命令。测试器没有直接发送操作系统命令，没有真实鼠标输入，也未测试关闭用户窗口。有限队列处理只分派预期的最大化/还原/最小化，其他系统命令在撤销后交还。

Release 连续两次为 **124/124**，外层 **38/38**；Debug 一次为 **124/124**，外层 **34/34**。检查覆盖颜色像素、左侧布局与命中，原消息路径最大化/还原/最小化，三枚原 DWM 按钮隐藏，客户区及外框保持，原属性与节点顺序恢复；完整 1,896 个 Node 的身份、父子关系与原标题几何一致。原窗口位置、最小化状态、样式、DWM/圆角、子 HWND 数及临时属性也核对通过，无 Owner 会话。

证据：`out/live-v18/office-layout/` 和 `office-layout-debug/` 中的 `result.json`、`transaction-*.json`、`*-before.bmp`、`*-painted.bmp`、`*-maximized.bmp`、`*-restored.bmp`。早期错误几何断言，以及只隐藏菜单时仍有两个 DWM 按钮的中间图保留在 `out/live-v18/`，不计为最终通过。

持续附加、异常退出恢复、树重建、跨屏 DPI、RTL、主题、编辑窗口、UIA 矩形及真实鼠标输入仍待验证。研究注册 271 未加入正式 `adapters.ini`。

源码：`framework-probe/office-layout.h`、`office-layout-host.cpp`、`office.cpp`、`test-office-layout.ps1`。

## TXGui

探针新增 IUnknown 规范身份与 SysButton 内部原按钮的绑定关系，不按业务名称选择按钮。实际读到了三个内部按钮与原树中三个 IGFButton 的一致身份。

原 GF 通知处理器 RVA `0xe6490` 比较内部控件身份并分派：

| 角色 | 数字事件 | 原框架事件名 |
| --- | --- | --- |
| 最小化 | 101 | onMinimize |
| 最大化 / 还原 | 102 / 104 | onMaximize / onRestore |
| 关闭 | 103 | onClose |

事件名也在原 GF 映像中校验。运行中及临时还原后的读取均已完成并恢复窗口状态，但该默认 SysButton 的内部按钮矩形仍为空/负尺寸，不能凭 Visible=true 当作当前可见标题按钮。未改写 TXGui 原树或发送窗口操作消息。

证据：`out/live-v18/txgui-final/tree.json`、`txgui-active.json`。只读状态回归 11/11；独立 SysButton 88 项、原 Frame 50 项契约继续通过。

## 编译与回归

`original_caption_style_view` 在自有隐藏 Win32 窗口验证 16 项真实样式和通知行为，包括原菜单模式、新三按钮模式、宿主改变能力后的旧/新通知、GetWindowInfo、恢复和重复恢复。x64/x86 的 Release/Debug 均通过，记录为 `style-view-*.log`、`txgui-and-style-*.log`。

主程序 Release/x64 11/11、Release/x86 7/7 全部通过，记录为 `ctest-main-x64.log`、`ctest-main-Win32.log`。Office 实验、TXGui fixture 的 Debug/Release 编译通过。VS 解决方案保持显式项目项。

主 C++ 格式检查、六个本轮研究源文件/头文件格式及 PowerShell 语法检查通过；项目与筛选器 106 个文件项匹配、路径存在且无重复或通配符。`out/live-v18/final-inventory.json`、`final-state-comparison.json` 确认 Office、BCG、TXGui 三个原进程及 HWND 均存活，Owner 为 0，样式、最小化状态和正常位置与本轮开始一致。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmake = Get-DevelopmentTool cmake
& $cmake --build demos/window-styler/out/framework-probe-x64 --config Release --target OfficeLayoutHost StyleViewContract
# 自动选择唯一 Office NetUI HWND；多个窗口时指定 -Hwnd。
# 短暂激活起始页测试，最后恢复位置、最小化状态和前台窗口。
.\demos\window-styler\research\test-office-layout.ps1

# TXGui 只读入口；--active 临时还原后读取，随后恢复原状态。
& demos/window-styler/out/framework-probe-Win32/probe/Release/FrameworkResearchHost.exe <当前TXGui-HWND> --active
```
