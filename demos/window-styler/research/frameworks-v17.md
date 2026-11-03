# v17：Office 活动标题组与属性回滚、TXGui 原生系统按钮契约

2026-11-01。延续 v16 的 BCG 原型，本轮完成 Office 原 Node 树的结构校验和原属性事务，以及独立 GF 测试进程中的真实 SysButton 契约。Office/TXGui 尚未完成左侧三灯布局或绘制验证，正式控制器仍未启用这两个后端。

## Office NetUI

旧的任意字段引用图会漏过没有可解码类型名称的基础节点，并在 1,200 个对象预算处截断。本轮改为仅沿原 Node 链遍历：`+8` 为父节点、`+0x10` 为首子节点、`+0x18` 为下一同级节点；`+0x20` 在首节点中指向尾节点，在其余节点中指向上一节点。对应原 getter 入口指令与三模块 CodeView 一起校验。未把基础节点的其他字段一概当作可修改布局。

当前四个 NetUI 根共读取 1,896 个 Node；父节点、同级回链、尾节点校验全部通过，无截断。按 HWND 可见样式、共同父节点、非空几何、FramePart、SysCommand 和 HitTest，得到唯一标题系统按钮组，排除了另一个隐藏根中的备用组。测试时应用保持原最小化状态，因此这里的“活动组”指原树启用的标题组，不代表当前屏幕上可见或完成绘制。

研究注册 269 保持只读并拒绝 Attach。新增研究注册 270 只执行有限的同步属性事务：在目标 GUI 线程把三个原 AppFrameHelper 的原有子元素临时设为圆点文字、Segoe UI Symbol、字体大小属性值 12 与红黄绿前景色，逐项回读后立即恢复。没有移动按钮、输入点击、发布 Owner 会话或维持回调。

原 GetValue、SetValue、RemoveLocalValue、Value 创建及释放方法均按此 Office ABI 和入口指令绑定。回滚保存的是 **原本地值**；若本来继承样式，则移除临时本地覆盖，不把继承后的计算值写回成本地值。

两次连续事务均为 `checks=49; failures=0; propertiesWritten=12`。外层 26/26 项检查通过：完整 Node 身份、父子关系、子项顺序和标题几何一致；窗口样式、外框、客户区、DPI、最小化状态、原正常位置、子 HWND 数量均不变；原应用响应且无 Owner 会话。圆点属性能写入并恢复不代表圆点已被实际绘制，因此报告保留 `layoutMoved=false`、`renderVerified=false`。

Debug 的一轮事务同样为 49/49，外层为 22/22，证据位于 `out/live-v17/office-contract-debug/`。早期 Debug 实测被已固定映射的同名 Release 引导模块按字节一致性保护拒绝，未生成事务报告且窗口状态未变。研究工程现将自身引导模块和 Office DLL 按构建配置区分文件名，验证目标内实际加载了 Debug 路径后通过；未修改正式产品的跨安装引导保护。早期拒绝记录保留在 `out/live-v17/office-debug-bootstrap-rejected/`。

证据：`out/live-v17/office-contract/result.json`、`transaction-1.json`、`transaction-2.json`、`tree-before.json`、`tree-after.json`、`before.json`、`after.json`。初次误判同级链方向的失败只读记录 `out/live-v17/office-tree-first.json` 保留，不计为通过。

源码：`office-tree.py`、`office-metadata.py`、`test-office-transactions.ps1`、`framework-probe/office.cpp`、`host.cpp`。还需验证原布局腾位、左移、绘制、原输入及树重建/窗口销毁后的恢复，不能把该事务当成已完成的 Office 适配。

## TXGui / GF

本轮在独立 x86 fixture 内创建了真正的 `IGFSysButton` 和其三个独立的 `IGFButton`。GF ABI 仍限定 CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63` / age 24。

直接使用 RawCreateGFObjectByName 的早期试验返回 E_FAIL；仅调用 SysButton 创建器能获得外层对象，但不足以证明内部按钮已正确创建。最终 fixture 使用 GF 公开的 SetCustomObjectFactory / CreateObject，在 **自身进程** 只为两个已验证类调用其原注册的创建器，再通过原 QueryInterface 验证真实内部接口。没有伪造按钮、注册 COM、修改 GF/Common 的停机状态或启动应用平台服务。

系统按钮类型 ID 为 `EB0C3B5C-0E7C-44A7-8037-0F7122632782`，普通按钮类型 ID 为 `70551436-19C4-4F25-BFC8-322D86AE1A9C`。原创建器与类 ID 数据经过入口校验；原 SysButton 显示掩码 setter/getter 虚表槽为 227/228。验证 0 至 7 的全部显示组合，确认各位对应原内部按钮的 Hidden 状态，然后恢复原掩码和外层 Frame 状态。

新 `txgui_original_sysbutton_transactions` 为 88/88 项内部检查通过；原 `txgui_original_frame_transactions` 仍为 50/50。引用释放后 GF Frame 计数回到原值；SysButton 创建和检查没有新增顶层 HWND。自有 fixture 的工厂回调在释放对象后移除。

该测试未操作运行中的 TXGui 应用，也未证明其当前标题区的普通按钮与原 SysButton 三个内部按钮相同。原窗口操作委托、左侧布局、三灯绘制及实际输入仍待验证。

源码：`framework-probe/txgui-fixture.cpp`。Release 详细证据：`out/live-v17/txgui-ctest-release.log`。

## 复现

```powershell
. .\demos\window-styler\development-tools.ps1
$cmake = Get-DevelopmentTool cmake
$ctest = Join-Path (Split-Path $cmake) ctest.exe

& $cmake --build demos/window-styler/out/framework-probe-x64 --config Release --target OfficeResearchHost OfficeTransactionHost
# 使用当前清单中的唯一 Office NetUI HWND；有多个窗口时必须指定 -Hwnd。
# 仅运行有限属性修改/恢复，不激活或还原最小化的应用窗口。
.\demos\window-styler\research\test-office-transactions.ps1

& $cmake -S demos/window-styler/research/framework-probe -B demos/window-styler/out/framework-probe-Win32 -G 'Visual Studio 17 2022' -A Win32 '-DSTYLER_GF_LIBRARY=C:\Program Files (x86)\Tencent\QQMusic\GF.dll'
& $cmake --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiFixture
& $ctest --test-dir demos/window-styler/out/framework-probe-Win32 -C Release -R txgui_original_ -V
```

第三方 GF.dll 仅作为本机已安装测试依赖，不纳入分发。正式 `adapters.ini` 没有新增 269/270 或 TXGui 研究注册。VS 项目和筛选器的 100 个文件项完全匹配、路径存在且无重复或通配符。主 C++ 与本轮研究 C++ 格式、Python 与 PowerShell 语法检查通过；x64/x86 的正式控制器 `strict_direct_policy` 回归通过。

最后一次运行中的 TXGui 只读回归为 11/11，证据为 `out/live-v17/txgui-live-readonly/result.json`。收尾清单 `out/live-v17/final-inventory.json` 与 `final-state-comparison.json` 确认三个原用户进程及 HWND 均存活、Owner 为 0、最小化状态和正常窗口位置与本轮初始清单一致。Office 唯一标题组和目标内 Release/Debug 模块路径分别保存在 `office-caption-group.json`、`office-loaded-research-modules.json`。
