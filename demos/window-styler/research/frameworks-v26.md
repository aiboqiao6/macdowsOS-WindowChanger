# v26：TXGui 真实原位置 D2D 三灯与逐像素撤销

2026-11-03。未使用 computer-use。真实 TXGui 的三个原标题按钮已通过原 D2D 内容绘制回调显示实心三灯，且撤销后原标题区域逐像素恢复。**按钮仍在原右侧位置；不是左侧布局完成，也不是正式后端。**

v27 更正：本轮 reader 达到 512 节点/16 层上限，原树记录不完整。“完整原树 512 项”应为限额内前缀，不能作为完整拓扑证明。v27 已修复完整性判定，并在完整 1,075 节点下重新通过本轮绘制/撤销验证，见 [v27](frameworks-v27.md)。

![原位置短时三灯实验](../images/txgui-d2d-v26.png)

## 独立实验 275

新增 `TxGuiD2DDrawHost`、按源码摘要和配置隔离名称的 `TxGuiD2DDrawProbe`、共享回调 `txgui-d2d-filter.h`、有限事务 `txgui-d2d-live.h` 与 `test-txgui-d2d.ps1`。沿用 GF CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`、age 24 的严格约束，原按钮身份由原提示语义、原生命中和共同父容器确定；不以应用名、业务控件名或宿主回调 RVA 选择后端。

只对三枚真实原 `IGFButton` 临时注册自己的原过滤器，保留原过滤器身份及顺序、原 `0x1003` 点击订阅和原公共 notifier。GUI 线程、source、payload、真实 D2D 虚表、共享 fill/state 方法和坐标 origin 全部匹配后才绘制；未知状态透传。不创建覆盖层或新标题带，不发按钮输入，不调用宿主操作回调，不手工提交 Frame 绘制通知，不改变原树关系或布局。正常 `RedrawWindow` 驱动原 renderer，原 renderer 自己管理 Begin/EndDraw。

实际绘制使用原 HWND target `0x1eaf9c` 的共享 solid rounded fill；圆直径为 12 DIP，本机 144 DPI 下为 18×18 物理像素，半径为 9。颜色按语义映射：关闭红色、最小化黄色、最大化/还原绿色，因此截图中的原右侧顺序为黄、绿、红。没有移动按钮来伪装成左侧方案。

## 修正绘制阶段，而非覆盖原字形

首次接入背景通知时，三个中心颜色、引用移除、状态保持和像素撤销检查均通过，但截图仍有原来的横线、方框、叉号。保留 `drawing-initial.*`、`drawing-initial-*.bmp`，不将 42 项旧检查通过当作替换完成。

静态追踪确认原 renderer 分别发送以下通知：

| 原通知 | 含义 | 本轮行为 |
| --- | --- | --- |
| `0x39a` | 背景/框架绘制 | 保留原路径 |
| `0x385` | 原 Button 内容绘制 | 通过原 filter Stage7 替换为 D2D 圆点并停止该内容路由 |
| `0x3a8` | 边框绘制 | 保留原路径 |

原入口 `0x18b160`、原 Button `0x96680`/`0x968bf` 以及原 filter-list 分派证据见 `out/live-v26/original-content-paint-abi.txt`。回调签名仍是六参数原过滤器接口，不是五参数 Frame 消息接口。部分无效区完全落在圆外时，内容阶段仍被消费但不写像素，防止局部重绘重新露出原字形。裁剪、两种 origin、global alpha 语义保持，线程 LastError 保存并恢复。

内容阶段截图正确后，最初新增的字形检测还误把 Close 的非对称内边距/圆角边框和采样点上的原叉号算作背景差异，产生两项误报。保留 `drawing-content.*`、`drawing-Release/`、`drawing-final-Release/`。最终检测使用实际标题行的独立背景采样并验证各控件的背景一致性，明确检查原字形在绘制前存在、绘制中消失，以及圆内实心和抗锯齿像素。未涂抹背景来清除旧字形。

## 真实窗口最终结果

证据：`out/live-v26/drawing-complete-Release/` 两轮、`drawing-complete-Debug/` 一轮。

- 每轮原生 **48/48**；外层 Release **31/31**、Debug **25/25**。
- 三枚实际圆点中心颜色精确，每枚圆内变化 **276** 像素，其中 **60** 个抗锯齿像素。
- 圆外检测区域的原字形像素：关闭 156、最小化 69、最大化 180；绘制期间均为 **0**。
- 只捕获未被遮挡的原系统按钮区域，本机范围 `[2158,915,2371,999]`，不保存整个桌面或应用业务内容。
- `before-*.bmp`、`painted-*.bmp`、`restored-*.bmp` 保留；撤销后整块 **213×84** 标题按钮区域逐像素等于绘制前。
- 三个回调从原集合移除，引用计数恢复，撤销重绘后无后续回调。原按钮几何、padding/margin、可见/启用状态、提示、命中、点击订阅和原过滤器身份/顺序保持。
- 本轮只回读到 **512** 节点前缀，前后已读部分的身份、父子关系和顺序一致；v27 确认了未展开子项，因此这里不能声称完整原树恢复。
- 保持原窗口 style、exStyle、DPI、客户区和普通窗口位置；结束后恢复原最小化状态，Owner 为 0。

回调不会对真实 target 查询或读取 bitmap 专用字段；v25 的 bitmap `+0x94` / `ReleaseDC` 配对仅在自有离屏夹具继续使用。实际像素由系统屏幕 DC 的有限区域读取，未调用未知的硬件 target 回读方法。

## 自有契约与位置恢复

共享回调先在自有进程的原 Button、原 filter collection、原 dispatcher 和原 D2D bitmap target 上验证；Release/Debug 各 **79/79**，见 `d2d-final-*.log`。除原来的颜色/裁剪/平移/alpha/资源检查外，覆盖实际共享回调注册和原分派、内容替换、背景透传、不相关通知透传、部分 clip、圆外 clip 消费、origin 不符时透传、移除后无回调与无像素残留、32 次注册/绘制/移除循环。GDI 3→3、USER 7→7，原 factory cache 恢复。

一次只读后验检查发现 `IsIconic` 已变化但最小化动画的 outer/client bounds 尚未结束。共享研究位置恢复改为有界等待并连续回读实际 outer/client bounds、原普通位置和 iconic/zoomed 状态，不再只检查最小化标志。自有位置恢复契约扩展为 **12/12**，x86/x64、Release/Debug 均通过；原误报记录保留。该等待仅用于研究验证器收尾，不是布局跟随定时器。

## 范围与下一步

研究 x86 CTest 两种配置各 **9/9**，x64 共享契约各 **2/2**。共享 Office 研究 Host 随位置恢复头重新构建，但本轮没有重新执行 Office 真实租约或 BCG 真实输入实验；不把历史结果计为本轮实测。MFC/BCG 与 Office 适配器逻辑未改。

主程序既有 Release 产物完整回归 x64 **11/11**、x86 **7/7**。三个原用户窗口（TXGui、Excel、BCG）的 HWND/PID、Owner 0、完整几何和最小化状态最终核对 **3/3**；无残留测试进程。主 52 项和本轮研究源码格式、PowerShell 语法检查通过；VS 项目和筛选器各 **134** 个显式、存在、唯一且匹配的文件项，无通配符。

实验 275 只注册在独立研究目录的 INI；正式控制器未启用 267/272/273/274/275。DLL 仍采用新摘要名称并固定映射，避免卸载仍可能被原分派快照持有的回调；不为解锁 DLL 关闭或重启用户应用。每次 Attach 在同步撤销所有注册后故意返回 `ERROR_NOT_SUPPORTED`，Host 根据新报告判定实验是否通过；这不是持续附加成功。

**仍未完成**：TXGui 左侧容器空间分配、原布局与绘制/原动作整合、持续附加与异常退出恢复、交互态和跨 DPI 验证。v24 发现左侧 200 宽容器无法直接容纳 84 宽组，不能把本轮原位置绘制结果当作左侧适配完成。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiD2DDrawHost FrameworkResearchHost TxGuiD2DContract
# 仅对已核验 GF profile 且允许临时还原的真实 TXGui HWND：
.\demos\window-styler\research\test-txgui-d2d.ps1 -Hwnd 0xYOUR_HWND
```
