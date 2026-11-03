# v19：Office 持续原树三灯与自动恢复、TXGui 原生命中定位

2026-11-02。Office 本机起始页从 v18 的有限事务推进为独立持续挂载实验；TXGui 定位到原生实际命中的顶部按钮，但操作语义和三灯布局仍待完成。MFC/BCG 保持 v16 原型，本轮未扩大其范围。未使用 computer-use 工具；正式控制器注册未变。

![持续挂载后由 Office 原管线绘制的三灯，实际窗口裁切](../images/office-netui-v19.png)

## Office：独立实验 272

沿用 v18 三个 Office CodeView profile、AppFrameHelper 原工厂及 Node 插入/绘制/命令路径，仅接受本机已核验的 x64、144 DPI（150%）、LTR、FullpageUIWindow 起始页与唯一原系统按钮组。未知布局拒绝，不转为原生兜底。

持续状态持有 20 个原节点强引用，覆盖根、包装节点、原标题路径、按钮及字形；另持有三个自建原生节点的强引用。保存 12 项原本地属性，区分显式值与继承值。最大化时 Office 会更新字形可见性：窗口操作和布局消息进入时暂时还原原属性，原布局返回后保存宿主最新值，再隐藏原按钮，避免把正常更新误判为树失效。

顶层 HWND 把三灯区鼠标消息交给原 NetUI HWND，由 AppFrameHelper 产生操作。没有新 HWND、外部叠加层、新增标题行或定位轮询。250 ms 定时器只检查所有权和保留的树身份。实现 Query、所有者校验、嵌套 Detach 的 TransitionBusy、所有者窗口/进程失效恢复；Owner 保持发布到清理结束。仅销毁自建节点，撤销不覆盖检测到的宿主新本地值。

最终 Release 连续两轮均 **37/37**，外层 **38/38**；Debug 一轮 **37/37**，外层 **34/34**：

- Attach 返回后持续有效，三色原绘制像素存在；最大化、还原、缩放后像素仍存在。
- 顶层输入消息经原框架完成最大化、还原、最小化；会话经状态切换及缩放仍有效。
- 错误所有者不能撤销，嵌套撤销返回 TransitionBusy，重复挂载/撤销正常。
- 销毁自有控制器 HWND、终止自有控制器子进程后均自动恢复；没有终止 Office。
- 1,896 个原 Node 身份、父子顺序和原 Helper 几何一致；样式、位置、最小化状态、DWM、客户区、子 HWND 数及临时属性恢复。最终清理错误、跳过属性均为 0。

证据：`out/live-v19/office-lease-paint-release/`、`office-lease-paint-debug/` 中的 `host-*.log`、`transaction-*.json`、`result.json`、`tree-before/after.json`、`*-paint-0/1/2/3.bmp`。每轮包含两次正常挂载，以及所有者 HWND 和进程退出测试。v18 有限事务 271 保留，回归 **124/124**、外层 **34/34**，见 `office-bounded-release/`。

树、DPI、RTL、能力或环境失效时保守撤销的路径已有实现；**实际树重建、窗口销毁、主题切换、跨屏 DPI 未实测**。硬件鼠标/捕获边界、红灯关闭、UIA 矩形、键盘系统菜单、编辑窗口仍未验证。没有通过关闭用户窗口测试销毁分支。实验未加入正式自动接管。

## TXGui：21 点原生命中

核验 GFSpy TopFrameInfoFromPoint 调用的屏幕坐标查询入口 `0x66e20` 与实现 `0x69780`，读取其原 IGFFrame 返回接口，不猜测私有 tagFrameInfo，也不触发输入或窗口操作。

顶部右侧三个高度、七个横向位置共 21 点查询全部成功。返回对象的规范身份均在 460 节点原树内，点均落在对应有效原矩形中；第一排命中三枚不同的真实 IGFButton，**不是此前几何无效的默认 SysButton 内部按钮组**。

Release/Debug 增强只读契约均 **17/17**：原有 11 项窗口不变性，加 ABI、查询数、成功状态、原树身份、有效矩形、三枚顶部按钮检查。证据：`txgui-active-contract/`、`txgui-active-contract-debug/`、`txgui-hit-abi.txt`。

仅凭位置不能赋予“关闭/最大化/最小化”语义。v18 默认 SysButton 的事件映射不能自动套用到本组，其事件绑定仍待关联。本轮未改写 TXGui 原树或执行其窗口命令。

## 编译与回归

Office x64、TXGui x86 的 Release/Debug 编译通过；样式契约在两种架构、两种配置均通过 16 项检查。TXGui 原 Frame 50 项、SysButton 88 项及样式契约的选定 CTest，Release/Debug 均 **3/3**。

主程序 Release/x86 **7/7**、Release/x64 最终完整复跑 **11/11**，见 `ctest-main-Win32.log`、`ctest-main-x64-final.log`。x64 首轮 10/11：WinUI3 自有夹具终止测试控制器时曾遇到 Access denied；单项重跑及最终全量复跑均通过。原失败日志 `ctest-main-x64.log` 和单项复核 `ctest-winui3-retry.log` 保留，没有通过更改代码掩盖该次失败。

直接对研究构建运行所有 CTest，曾因 EXCLUDE_FROM_ALL 的主测试目标未构建出现 Not Run；随后按研究测试名筛选全部通过。早期保守撤销、前台占用导致未开始的运行、中间版本及失败日志均保留，不计入最终版通过次数。

源码：`framework-probe/office-lease.h`、`office-lease-host.cpp`，共享 v18 布局工具；外层验证入口为 `test-office-layout.ps1 -Lease`。VS 项目继续逐文件声明。

主 C++ 格式检查 52 项及五个本轮研究源文件/头文件检查通过；两份修改后的 PowerShell 脚本语法检查通过。项目与筛选器 110 个文件项匹配，路径存在、无重复或通配符。`final-state-comparison.json` 核对 Office、BCG、TXGui 原进程/窗口和原几何状态，未留下 Owner 会话。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmake = Get-DevelopmentTool cmake
& $cmake --build demos/window-styler/out/framework-probe-x64 --config Release --target OfficeLeaseHost
.\demos\window-styler\research\test-office-layout.ps1 -Lease

# 临时还原指定窗口，读取后恢复；不按业务控件名选择。
.\demos\window-styler\research\test-framework-readonly.ps1 -Hwnd <当前TXGui-HWND> -Active
```
