# v28：TXGui 原双顺序恢复，无关兄弟不再移除/重插

2026-11-03。未使用 computer-use。本轮打通 v27 未解决的原顺序恢复入口：**Element 链和 Frame 链分别精确恢复，不调用无关兄弟的 Remove/Insert 或 parent setter**。真实窗口有限事务 Release 两轮 / Debug 一轮通过，但没有扩展原标题 Frame、改坐标或迁移三灯到左侧；正式后端未启用。

## 两条原链不是同一顺序

原 GF 的 ordinary Frame 同时持有 Element 首尾链和 Frame 首链。静态追踪 `0x73c50`、`0x7a030` 及枚举入口确认两条链分别维护，Frame 枚举恢复不能代替 Element 顺序恢复。原 `zOrder` 也不能直接当 raw child index。

新增 `txgui-order.h`，验证 GF CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`、age 24，所有相关公开接口 wrapper 和 native leaf ABI；七个 51 字节叶子 setter 逐条指令验证，防止同 RVA 已被拦截的实现引入宿主回调。它们只交换原 COM 引用，不调用绘制、事件或 parent 生命周期方法。静态身份、SHA256 和反汇编证据为 `out/live-v28/original-order-abi.txt`。

| 原链 | 前驱 / 后继 getter | 前驱 / 后继 setter | 原父容器端点 |
| --- | --- | --- | --- |
| Element | 接口偏移 `0x74` / `0x7c` | `0x78` / `0x84` | 首 `0x90`、尾 `0x98` |
| Frame | `0xe0` / `0xe8` | `0xe4` / `0xf0` | 首 `0xf8` |

先回读所有前后链和父身份，拒绝循环、重复、缺失、跨父节点及超限；所有对象在修改前保留 COM 引用。只允许同一父节点、相同对象集合的排列，在原 GUI 线程同步完成，没有消息泵或布局跟随。只对实际不同的链接调用原 setter，不写私有字段。完成后再次读回两条链。发生验证失败或异常时，由预先固定的向量强制恢复原链接；回滚不分配内存、不抛异常。

父对象若为 StackPanel 则拒绝，因为它还有第三套 typed layout vector，本轮不宣称已覆盖其排列。Element 集合与 Frame 集合不相同的父对象也拒绝，不把未知 Element 当 Frame 调用。

## 自有契约：Release/Debug 各 40/40

`TxGuiFixture --order-contract` 使用真实原 Frame、四个原 Button 和原点击 notifier；每个 Button 都预置原 `0x1003` 点击订阅，并通过原过滤器集合装入只读观察器。不是空事件容器替身，没有触发点击。

- 四子项全部 24 种排列，覆盖首、中、尾、相邻调整，逐次核验两链前后链接、父身份和完整计数。
- 两条链故意采用不同排列，然后各自恢复，避免假设两者原本相同。
- 128 次往返，以及显式抛异常后的无分配 scope rollback。
- 重复、缺失、异父对象、非 GUI 线程及未知 StackPanel 顺序模型均在写入前拒绝。
- 空父节点、单子项的空前驱/后继、首尾端点及原删除后的清理。
- 所有原 Button 在纯顺序调整中没有收到过滤器 Stage7/8 通知；几何、可见/启用状态和真实原点击订阅不变，过滤器移除后原集合恢复。
- 原 GF frame count 在释放全部对象后回到初值，不产生 HWND、不发送输入。

v27 的迁移契约改用这一双链恢复，不再移除/重插后续无关兄弟。真实原 header 的 84 DIP 自有迁移和 32 次往返仍通过，契约从 21 项增加到 **22/22**。证据 `order-final-*.log`、`reparent-final-*.log`。

## 真实窗口有限实验 276

新增研究专用 `TxGuiOrderProbe` / `TxGuiOrderHost` 与 `test-txgui-order.ps1`。按原语义标题按钮、祖先及几何找到左侧 header 的普通 Frame 父容器，不按应用名、业务名称或宿主操作回调 RVA 选择。仅将该 header 临时放到原子列表末尾再恢复原位；**同一父节点不变，没有 Remove/Insert、parent setter、几何 setter、按钮输入或绘制请求**。

本机真实顺序如下，证明两条原链实际不同：

| 阶段 | Element 顺序 | Frame 顺序 |
| --- | --- | --- |
| 原始 | A, D, header, C | A, header, C, D |
| 有限事务中 | A, D, C, header | A, C, D, header |
| 撤销后 | A, D, header, C | A, header, C, D |

其中 A/C/D 是原无关兄弟节点，名字仅用于说明关系，不是后端选择器。报告保存实际 identity，不靠示意字母分派。

最终 `order-final-Release/` 两轮、`order-final-Debug/` 一轮均为原生 **10/10**；外层分别 **33/33**、**26/26**。GUI 线程同步事务前/中/后回读完整 **1,075** 节点：全部节点的父关系、几何、padding/margin、可见/启用状态、原按钮绑定和过滤器保持；唯一预期变化是所选父节点的两个排列，结束后两者逐项精确恢复。原提示/命中及全部 typed layout order 保持，原顶层窗口几何、样式、DPI、普通位置/最小化状态和 Owner 也恢复。

没有保存回调到原对象，因此模块不需要像绘制实验那样固定其回调代码。原接口和所有 COM 引用仅在当前调用内使用；研究 Session 仍故意拒绝持续 Attach，以新鲜 JSON 判定事务结果。

## 首次外层差异及只读对照

首次原生事务已 10/10，但外层“两个不同时刻整树 JSON 字节相等”失败，证据 `order-initial-Release/`。差异仅位于底部一个原滚动子树的位置及其后代 screenRect，没有拓扑/绑定变化。

随后只运行两次只读 reader、完全不做顺序事务，**同一组节点也发生平移**，见 `readonly-control-differences.json`。这支持它是宿主自己的滚动动画；没有为了让测试通过而把动画位置写回。

最终外层改为：完整原树拓扑、typed layout、可见性/启用状态和原绑定严格一致；所选 header 全部后代、原父容器和语义标题按钮的几何严格一致。原生同步事务仍要求**所有** 1,075 节点的几何/状态完全一致。原整树 JSON 字节比较没有删掉，而是保留为 `fullTreeByteEqual:false` 诊断字段，因此不声称两个相隔数秒的整个应用画面静止不变。

## 回归与边界

研究 x86 Release/Debug 各 **12/12**，新增双顺序契约；x64 共享契约各 **3/3**。共享研究 Host 的编译宏增加 276 后，x86 各现有 Host/模块、x64 Office 研究 Host 均重建；MFC/BCG、Office 后端逻辑未改，本轮未重新执行它们的真实实验。正式控制器 INI 未新增研究编号。

主程序既有 Release 产物首轮 x64 为 9/11：XAML 自有示例最小化断言及原生自有示例侧/底边像素断言失败，日志 `main-x64-Release.log` 保留。两项单独复查 2/2，随后完整重跑 x64 **11/11**、x86 **7/7**，见 `main-x64-failed-recheck.log`、`main-final-*-Release.log`；没有据此修改正式产品代码或断言，也没有声称已确定首次失败根因。

顺序实验结束后的即时库存与本轮初始库存比较，三原用户窗口 HWND/PID、完整几何/最小化状态和 Owner 0 为 **3/3**，见 `post-experiment-state-comparison.json`。数分钟后、主程序回归收尾时的库存显示 TXGui 已还原并从原位置移动，当前前台为任务管理器；Excel 和 BCG 仍与初值一致。因此最终原始库存比较是 **2/3**，见 `final-state-comparison.json`，不能写成 3/3。该后续变化尚未由用户确认来源，已询问并保留当前新位置，没有强制写回历史窗口状态。三个原用户窗口仍存活、Owner 为 0，无残留测试进程。

主 52 项及本轮研究 C++ 格式、PowerShell 语法通过；VS 项目/筛选器各 **146** 个匹配、存在、唯一的显式文件项，无通配符。

本轮只解决顺序精确恢复，不等于已完成真实跨容器 header 迁移，更不是左侧三灯已适配。下一步验证已有标题 Frame 的原宽度/margin/裁剪扩展及撤销，再把迁移与原布局组合；三灯绘制、原动作、持续附加与异常退出恢复仍需整合。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiFixture TxGuiOrderHost FrameworkResearchHost
& demos/window-styler/out/framework-probe-Win32/Release/TxGuiFixture.exe 'C:\Program Files (x86)\Tencent\QQMusic\GF.dll' --order-contract
# 对已核验 GF profile 的真实 HWND 做短时顺序事务：
.\demos\window-styler\research\test-txgui-order.ps1 -Hwnd 0xYOUR_HWND
```
