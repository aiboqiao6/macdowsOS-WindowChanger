# v27：完整原树、左侧布局候选与原容器迁移契约

2026-11-03。未使用 computer-use。本轮补齐 TXGui 原树完整性验证，并为左侧三灯找到一个几何上可容纳的原布局候选。**未在用户窗口改布局；候选方案尚未成为可用后端。**

## 更正 v26 的原树范围

复核发现旧 reader 的两个上限：512 个节点、深度 16。它在达到上限时停止展开，但没有输出“不完整”，只读验证器也未逐一比较 `childCount` 与实际读出的 children 数量。因此 v26 的“完整原树 512 项恢复”应更正为“限额内的 512 项前缀身份/顺序一致”。原 48 项绘制、原字形消失、213×84 标题区域逐像素恢复、原按钮绑定/过滤器/几何检查仍有效；旧原树部分不能充当完整拓扑证明。

本轮新增共享 `txgui-tree-budget.h` 与自有 `TxGuiTreeContract`：有界预算为 4,096 节点、48 层、每节点 1,024 子项。明确记录节点/深度/子项限额、重复身份、count/index getter 失败；不完整时原 probe 返回失败，绘制 Attach 不再继续。reader 不因上限读完一个成功前缀就报告成功。

新增独立 PowerShell `txgui-tree-check.ps1`，再次逐节点核验非空唯一身份、原 child count 状态、声明子项与实际数组长度、原生总数和完整声明。`test-framework-readonly.ps1` 强制使用该检查；旧截断报告即使有相同前缀也不再通过。原生预算契约 **15/15**，脚本契约 **9/9**，涵盖精确边界、超限、重复/空身份、getter 失败、缺根、总数不符和历史 v26 截断数据拒绝。

最终 Release/Debug 的真实完整原树均为 **1,075** 节点、深度 **20**，错误、限额及重复身份计数均为 0。只读契约为 **27/27**。证据：`out/live-v27/layout-read-Release/`、`paint-fulltree-Release/read-*`、`paint-fulltree-Debug/read-*`。不把超过历史 460 项的宿主自身节点变化强行回滚。

## 原布局候选：仅生成计划

reader 增加对已核验原 `IGFStackPanel` 的方向和 typed item 顺序读取，与 raw child tree 分开报告。`txgui-left-plan.ps1` **只读取完整 JSON，不连接窗口、不写入原进程**；从原提示意图/命中对应的三按钮、共同父容器、原顶部 Frame 和几何分区识别左侧原容器，不按应用名或业务控件名选择。

本机读到：

- 原标题 Frame 范围 `[232,0,1226,76]`，左侧兄弟区域宽 232 DIP。
- 左侧原 header 为纵向 StackPanel，宽 200 DIP，margin 左 18、上 23；包含可见内容和原隐藏行。它在原父容器的 raw index 为 1，后面还有两个无关兄弟节点。
- 给原左侧 header 前预留 84 DIP，需要右边界 302 DIP；候选左侧区宽 `232+84=316` DIP，可容纳原内容。
- 原顶部内容宽 569 DIP，候选右边界 885 DIP，仍小于现有右侧标题按钮组左边界 1043 DIP。
- 原右侧按钮容器还有一个无关 Button，不能整体隐藏或删除。

候选方向是在**已有**标题 Frame 中完成左侧区域合并和原 header 迁移，让原横向布局预留 84 DIP，同时保持原 sidebar 正文范围；不是把按钮画到已裁剪父容器外，也不是增加新标题带或整列空白。

`left-plan.json`、`left-plan-Debug.json` 均输出 `candidateFits:true`、`proposalOnly:true`、**`liveReady:false`**。尚需核验原标题 Frame 扩展及祖先裁剪、无关兄弟生命周期、真实按钮迁移后的原命中/原操作路径。对不完整历史输入的规划拒绝见 `planner-negative.log`。几何预算可容纳，不等于真实左侧适配已完成。

## 自有原容器迁移：Release/Debug 各 21/21

新增 `txgui-reparent-contract.h` / `TxGuiFixture --reparent-contract`，仅在自有进程创建真实原 GF Frame、纵向原 header、横向原 wrapper、84 DIP spacer、可见原 Button、隐藏行及原公共点击 notifier。没有外部 HWND/PID。

通过原 Remove/Insert 和原 StackPanel layout，header 从原 `[18,23,218,55]` 重排到 `[102,23,302,55]`：原框架确实预留 84 DIP，不缩小原内容、不改变其 y/高度。原 header 身份、嵌套子项顺序、隐藏状态、padding/margin，以及一条预先填充的原 `0x1003` 点击订阅保持；不调用该订阅。32 次迁移/恢复后原顺序和状态恢复，原 GF frame count 回到初值，不产生 HWND。

首次撤销出现两项失败：把通用 Insert 第三个参数误当“插入到下一兄弟之前”，实际 raw 顺序未恢复；尝试 `zOrder` 也未恢复 raw 顺序。保留 `reparent-initial.log`、`reparent-zorder-initial.log`，没有把 HRESULT 成功等同于原顺序恢复。

最终自有契约通过原方法临时移除/重插**夹具自己的**后续兄弟来恢复 raw 顺序，每次都逐项回读。该方法会触及无关兄弟生命周期，**没有因此获得在真实用户原树使用它的结论**。真实 source header 后面有两个原兄弟；在它们的生命周期副作用或更窄的原顺序接口验证完成之前，不执行真实迁移。

## 完整原树下重跑绘制与回归

原位置绘制 275 逻辑未改，但重建为新摘要名称并通过新完整性前置检查。Release 两轮 / Debug 一轮原生仍各 **48/48**，外层分别 **31/31**、**25/25**；现在原树比较覆盖全部 **1,075** 节点，标题区域仍逐像素撤销，原按钮提示/命中/绑定/几何及原过滤器恢复。证据 `paint-fulltree-*`。

研究 x86 Release/Debug 各 **11/11**，含完整性和原容器迁移新契约；x64 共享契约各 **3/3**。D2D 79 项、StackPanel 42 项、原绘制分派 28 项等既有契约保持。MFC/BCG、Office 适配器逻辑未改，本轮未重新做其真实应用实验，正式控制器未增加研究编号。

主程序既有 Release 产物完整回归 x64 **11/11**、x86 **7/7**。三个原用户窗口 HWND/PID、完整几何、最小化状态和 Owner 0 最终核对 **3/3**；无残留测试进程。主 52 项及本轮研究源码格式、PowerShell 语法通过；VS 项目与筛选器各 **141** 个文件项存在、唯一、匹配且没有通配符，证据 `final-state-comparison.json`、`final-test-processes.json`、`vs-items.json`。

下一步是验证不扰动无关兄弟的原顺序恢复接口，或证明必要的原生命周期变动完全可撤销，再尝试已有标题 Frame 的有限布局事务。不会直接将自有迁移成功推广为用户窗口左侧布局成功。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target FrameworkResearchHost TxGuiFixture TxGuiTreeContract
.\demos\window-styler\research\test-framework-readonly.ps1 -Hwnd 0xYOUR_HWND -Active -OutputDirectory demos/window-styler/out/live-v27/read-current
.\demos\window-styler\research\txgui-left-plan.ps1 -TreePath demos/window-styler/out/live-v27/read-current/tree.json
```
