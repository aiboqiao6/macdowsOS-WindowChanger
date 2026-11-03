# v24：TXGui 真实 renderer 透传观察与实际 D2D 后端

2026-11-03。未使用 computer-use。本轮不再只依赖手工提交的自有通知：已在本机真实 TXGui 窗口的三个原按钮上，接收到原 renderer 自动生成的绘制通知。**没有画三灯、改布局、发送按钮输入或扩大正式适配范围。**

## 独立实验 274：有界、透传、可撤销

新增 `framework-probe/txgui-paint-observer.h`、`txgui-paint-live.h`、`test-txgui-paint.ps1`，以及独立 `TxGuiPaintProbe` / `TxGuiPaintHost`。实验 274 仅注册在研究构建中；原动作实验 273 和只读入口 267 保留。

先在自有原 graphics / Button 上验证同一个透传观察器，Release / Debug 各 **17/17**。覆盖原过滤器集合身份与顺序、引用计数、两个分派阶段、原画布大小/裁剪只读查询、像素不变、原处理标志/返回值/线程 LastError 保持，以及移除后的无回调。原 v23 绘制过滤器的六参数 ABI 继续沿用，不把它当成五参数消息接口。

真实窗口测试在原 GUI 线程上暂时注册三个 observer，只通过 `RedrawWindow` 请求原窗口正常重绘。没有直接调用 `0x63590` 提交通知，也没有直接调用任一宿主点击回调。回调不改事件的 handled、result、route 或 observers 参数；不调用绘图方法、不取用户窗口 DC、不创建覆盖 HWND。未知 graphics 只记录受保护模块内的虚表身份，不尝试其方法或内部画布布局。

全部观察器在有界调用返回前通过原 API 移除，再检查原过滤器身份及顺序、引用归还、原 click 订阅、三枚按钮的矩形/边距/可见性/启用状态。移除后再次原重绘，确认没有后续观察回调。C++ 作用域清理覆盖部分注册和异常路径；回调 DLL 固定映射以保护原快照持有的 COM 回调，不通过重启应用卸载 DLL。

## 真实结果：39/39；原窗口和 460 节点恢复

最终证据使用 `out/live-v24/paint-restored-release/`（连续两轮）与 `paint-restored-debug/`（一轮）：

- 三轮原生检查各 **39/39**，三个原按钮均收到两个原绘制阶段；每枚各记录两次 paint-stage 观察。
- 原来源、GUI 线程与 payload 校验均通过；没有跨线程或未知来源读访问。
- Release 外层 **29/29**，Debug 外层 **24/24**；前后两次只读入口分别 **25/25**。
- 全部 460 个原节点的身份、父子关系、子节点顺序，以及原标题按钮的几何、提示、原命中身份和所有点击订阅保持一致。
- 原 HWND/PID 存活，样式、扩展样式、外框/客户区、正常位置、DPI、DWM、圆角与最小化状态恢复，Owner 为 0。

报告明确记录 `layoutModified=false`、`pixelsWritten=false`、`inputSent=false`、`syntheticNotifications=false`、`persistentSession=false`。注册/移除 observer 是临时框架状态变更，不能将其描述为完全没有修改进程。返回“不支持持续附加”是预期行为；日志 `Attach accepted: 0` 不表示未收到原通知，成功依据是新报告和全部恢复断言。

较早的 `paint-initial-release`、`paint-final-release`、`paint-final-debug` 保留。增加线程 LastError 保持后完整重跑，最终结果以 `paint-restored-*` 为准；没有覆盖失败或阶段证据。

## 重要发现：实际是原 D2DRenderTarget，不是 GDI canvas

三个原按钮实际提供同一个 GF graphics 虚表 RVA **`0x1eaf9c`**。静态核验其 `EndPaint` 方法 **`0x190a0`**，以及该实现引用的 `D2DRenderTarget::EndPaint %0x` 原诊断字符串，确认这是原 D2DRenderTarget。证据见 `out/live-v24/actual-d2d-profile.txt`。

此前 v22 / v23 自有夹具的 GDI graphics 虚表是 `0x1ecd6c`，其内部借用 HGCANVAS 路径不能直接套到实际对象。观察器仅对经过版本保护的旧 GDI profile 执行已测试的画布只读查询；真实 D2D profile 的报告为 `originalD2d=true`、`originalGdi=false`、`canvasRead=false`。后两个字段不是读取失败，而是明确没有尝试不匹配的对象布局。

本轮**没有在用户进程中执行任何尚未核验的 D2D 虚表方法**。实际 D2D 圆点绘制、alpha/变换/裁剪状态保持、原 glyph 隐藏与绘制撤销仍需先在自有原 render target 上完成像素契约。

后续静态线索：原 `GetRenderSerice` 返回的服务通过原 factory `0x17cf0` 创建 target；匹配 `0x1eaf9c` 的原分支位于 `0x17e97`，分配大小 0xc8，调用原基构造 `0x15ae0`，kind 为 0x10，销毁路径 `0x16130`。这些入口本轮只做静态追踪，**未伪造 context 或手工填充对象字段进行调用**。下一步应使用自有 HWND 与原 factory 验证创建、绘制、回读及销毁。

## 实际左侧布局边界

观察报告同时读取六级原祖先链及直接子节点的类型、几何和可见性，不按业务控件名选择。当前窗口宽 1226 逻辑像素；实际结构不是 v23 夹具的整行横向 StackPanel：

| 原区域 | 已读逻辑边界 / 关系 | 对左侧留位的影响 |
| --- | --- | --- |
| 原标题内容 Frame | x=232…1226，高 76 | 不覆盖窗口最左侧区域 |
| 原按钮 StackPanel | 相对上行 x=811…994，含三个系统按钮及一个其他原 Button | 不能整体隐藏该组或将三个子按钮简单移出其父范围 |
| 左侧原区域 | 宽 232；其顶行原 StackPanel x=18…218、y=23…55 | 已用宽 200，横向总余量只有 32，不足直接增加 84 宽占位 |

因此没有把原按钮挪出裁剪父容器，也没有套用 v23 夹具的 84 宽插入来挤掉原内容。实际原树的空间分配、命中范围及输入/撤销仍待验证；本轮没有增加标题带、外部覆盖层或跟随定时器。

## 构建、回归和入口

TXGui x86 研究构建 Release / Debug 编译通过，最终选定研究 CTest 各 **8/8**（包含新 17 项透传观察器，以及此前原 Frame 50、SysButton 208、画布 49、StackPanel 42、绘制分派 28 项契约）。共享 Host 的 Office x64 研究入口也按当前摘要重建；本轮没有重跑 Office 真实窗口租约，MFC/BCG 源码未变。

主程序既有 Release 产物完整回归 x64 **11/11**、x86 **7/7**。正式适配器源码和注册范围未变；动作实验 273 本轮仅编译，没有向真实按钮重新发送输入。相关日志位于 `out/live-v24/`。

最终 `final-state-comparison.json` 核对 Excel、TXGui、BCG 三个原窗口 HWND/PID、Owner 为 0、完整几何及最小化状态均一致，**3/3**；`final-test-processes.json` 无残留测试进程。主 52 项 C++ 与六个本轮研究 C++ 文件/头文件格式检查、PowerShell 语法检查通过。VS 项目与筛选器各 **127** 个显式文件项，匹配、存在性、唯一性及无通配符检查通过。x64 共享样式和位置恢复契约 Release / Debug 各 **2/2**。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiPaintHost FrameworkResearchHost TxGuiFixture
.\demos\window-styler\research\test-txgui-paint.ps1 -Hwnd <当前TXGui-HWND>
# Debug：使用 probe/Debug/TxGuiPaintHost.exe，通过 -Probe 参数指定。
```

本轮已打通真实原 renderer 通知观察，但**真实窗口左侧三灯的 D2D 绘制、原布局接管、原输入与异常恢复尚未合并完成**，不能宣称 TXGui 适配已完成。
