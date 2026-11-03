# v30：原树左侧三灯整合与主控制器接入

2026-11-03，全程未使用 computer-use。TXGui 已把真实原 header、原三枚 Button、原 D2D 绘制、原输入和持续会话整合。MFC/BCG、Office NetUI、TXGui 的已验证有限 profile 已加入主构建、注册表和 runtime 清单；不是任意版本/任意窗口类型兼容保证。

## 原布局事务

`txgui-stack-order.h` 在 Element/Frame 两条链之外保留 StackPanel typed vector。普通双链 API 仍拒绝直接处理 StackPanel。自有三序契约 Release/Debug 各 **23/23**：24 种排列、三套不同顺序、128 次跨容器迁移、原 click 订阅保留、重复/缺项/异父/异线程拒绝、原对象数恢复。

`txgui-left-layout.h` 的自有组合契约两种配置各 **36/36**。原标题 `[232,0,1226,76] → [0,0,1226,76]`，原 header `[18,23,218,55] → [102,23,302,55]`，原三枚 Button 占 84 DIP，原内容到 `[316,0,885,76]`。原 header 整体迁入已有 title，不增加高度或 HWND；32 次往返、异常 scope、重复撤销恢复公共几何和全部 48 字节布局记录。

移出按钮后，原右侧 caption 会自动收窄，带动剩余无关 Button 右移。真实断言捕获该问题，撤销仍成功；后增加空的原 StackPanel 项，仅替代移出按钮的空间，保持无关 Button 原位。三个新增对象都是原树布局容器，不是覆盖窗口。严格 GF CodeView 下调用原 creator，未覆盖目标进程全局 custom factory。原 policy 物理尺寸与公共 DIP setter 分开处理，避免 DPI 舍入损失。

## 实际布局、绘制与持续会话

有限事务 278 原生 **26/26**：完整 1,075 节点同步比较，原生命中返回迁移后的同一原 Button，原 D2D 内容过滤器画三色圆，无构造假绘制通知；原绑定/过滤器与无关几何保持，撤销后完整原树及左右局部像素精确恢复。早期右侧自动宽度失败保留于 `out/live-v30/`。

![真实原窗口左侧三灯与迁移后的原 header](../images/txgui-left-v30.png)

持续会话 279 两种配置原生各 **55/55**：

- Attach 返回后的实际三色、重复 apply/undo、错误 owner、嵌套 detach 拒绝。
- 原 Button 输入触发最大化、还原、最小化和缩放后的持续绘制。
- 直接在新尺寸/最大化状态撤销，保留新 viewport，不强制历史外框；原布局约束恢复。
- 原按下/捕获、取消、按住时撤销；owner HWND 丢失、控制器进程崩溃、嵌套失效后的延迟恢复。
- 宿主能力位变化时安全退出且保留最新请求；验证器仅恢复自己临时改动的位。

只终止验证器自己创建的控制器子进程，不终止或重启用户应用。Close 从未在验证中点击，原关闭/托盘策略沿用原 Button。原输入继续交给原 HWND，不直接调用宿主 callback。唯一 250 ms timer 只检测 owner/树失效；原绘制回调读取原按钮几何核验 origin，不用 timer 跟随或写布局。清理错误均为 0。

独立外层 Release 两轮 **29/29**、Debug 一轮 **22/22**；每轮完整只读各 27/27。全树身份、父子序、typed order、绑定，以及受影响 title/header 子树状态/几何恢复，窗口状态恢复且 Owner 0。

初期外层两项整树断言失败：后台业务 hidden 和底部滚动子树数值变化，完整原始 JSON/失败结果保留在 `lease-Release/`。不向未修改正文写回业务状态或动画。最终明确分开“全树拓扑/订阅及受影响子树状态恢复”与“整个活动应用所有数值字节相同”；后者只记录为 `fullTreeByteEqual` 诊断，不能声称业务树冻结。有限事务的同步完整树比较仍精确要求相同。

## 主控制器

新增 `original-framework-adapters.cmake`，默认开启 `STYLER_ORIGINAL_FRAMEWORKS`。x64 注册 268 MFC/BCG、272 Office NetUI；x86 注册 279 TXGui。只读/有限实验 267、270、271、273…278 不进入主注册表。未知 ABI、未知树、空间不足拒绝，不放宽严格无兜底策略。

新模块使用内容摘要与配置文件名，TXGui 探测上下文 thread-local。主引导 DLL 原 Debug/Release 同名，真实 Debug 接管被原字节校验拒绝；加入配置后缀后保留原校验，不重启用户应用。主自动路径 TXGui Release/Debug 各 **8/8**、Office Release/Debug 各 **8/8**，验证自动发现、持续实际三色、重复 apply、撤销和窗口状态。初次 Debug 失败保留。

MFC/BCG 沿用 v16 已有原输入、绘制、缩放和 owner 异常恢复实现。用户授权后，本轮新增主控制器的真实管理员验证：Release、Debug、完整运行包各 **10/10**，确认自动选择/附加、持续附加、原 renderer 三枚左侧灯的屏幕中心实际颜色及位置、重复 apply、显式 undo、窗口状态恢复。没有发送按钮输入，不把 v16 的动作/异常测试当作本轮新执行。Office 编辑窗口、更多 ABI、跨 DPI 等仍不在支持范围。

新增 `verify-mfc-main.ps1` 只接受现有空闲 MFC/BCG Ribbon HWND，要求调用者已是管理员，自身不提升权限。前后管理员库存核对原 PID/HWND、完整窗口几何/状态、Owner 0；本轮 `mfc-main/result.json` 为 `passed:true`、`originalWindowRestored:true`。原窗口原本最小化，三轮结束后仍为原最小化状态。验证器只读取三枚灯中心像素，没有截图桌面、点击关闭或重启应用。

最初管理员包装脚本未进入附加：Windows PowerShell 5 的脚本根默认参数为空，以及默认 ANSI 读取 UTF-8 库存导致 JSON 失败。已将默认路径初始化移至脚本体并显式按 UTF-8 读库存；早期错误日志保留，不计为后端成功或后端失败。打包脚本同步避免默认参数中的脚本根求值问题。

完整包的 x64 主入口读取匹配架构辅助目录的注册表，避免漏掉仅 x86 提供的 TXGui。包内自动跨架构 TXGui **8/8**、Office **8/8**；未知 ABI 自有模型拒绝检查 x64 **5/5**、x86 **4/4**。初次跨架构候选列表缺项已修正，未增加任意路径 DLL 加载或放宽版本匹配。

## 构建和复核

四种主配置编译通过，x64 Release/Debug 全量回归各 **12/12**、x86 各 **8/8**。首轮 Debug 的一个已有原生边框测试出现侧边/底边像素失败，单项复查及完整重跑通过；原失败日志保留，未改断言，未声称已确定根因。研究 x86 两配置各 **15/15**，x64 各 **3/3**；主 53 项 C++ 格式、改动研究源码格式和新脚本语法通过。VS 工程/filters 各 **168** 个存在、唯一、匹配的显式文件项，无通配符。后续管理员补验仅改动验证器和脚本，不改变已验证的三类后端。

三原应用当前 HWND/PID、完整几何/状态与本轮基线相同，Owner 均 0。证据和所有早期失败均在 `out/live-v30/`；已映射历史 DLL 保留，不等于仍有活动会话。

使用和精确边界见 `../original-frameworks.md`。`package-original-frameworks.ps1` 读取两架构 runtime 清单生成完整包，根目录 x64 控制器配套 `x86` 辅助目录。

```powershell
.\build.ps1 -Test
.\build.ps1 -Architecture Win32 -Test
.\demos\window-styler\research\test-txgui-left.ps1 -Hwnd 0x当前HWND -Lease
.\demos\window-styler\package-original-frameworks.ps1
```
