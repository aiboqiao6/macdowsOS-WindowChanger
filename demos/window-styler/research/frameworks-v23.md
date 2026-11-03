# v23：TXGui 原容器左侧留位、原绘制通知接入

2026-11-03。未使用 computer-use。本轮在**自有测试进程的真实 GF 对象**上验证原 StackPanel 重排及原绘制通知分派，不是实际应用窗口适配完成。没有向三个原用户应用注入本轮代码，没有改变它们的输入、控件树、布局或绘制。MFC/BCG、Office 的实验限制和正式控制器范围不变。

## 原 StackPanel：42/42，Release / Debug

扩展 `framework-probe/txgui-fixture.cpp`，使用原 creator 创建真实 StackPanel，而不是模拟容器。其类身份为 `FCAA4CE1-0CCE-4D72-BC81-E95F3CCB86D9`；沿用 GF x86 CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`、age 24，并检查 creator、类 GUID 和使用的虚表方法。

原容器有两种顺序，不能混淆：Frame 子树枚举顺序和 StackPanel 自己的排版向量。使用原 typed indexed-item 接口插入索引 0，可以让新组排在左侧，同时原子树内两个内容对象的身份与相对顺序不变。删除新增组时也必须走原容器接口，不能只改 Frame 链表。

夹具首先建立两个原内容 Frame，原矩形为 `[8,4,88,36]`、`[88,4,188,36]`。新增一个宽 84 的原 StackPanel，包含三个宽 28、高 32 的真实 Button。由原重排方法自动得到内容矩形 `[92,4,172,36]`、`[172,4,272,36]`；没有逐次写入原内容位置。

最终检查包含三枚 Button 的组内位置、组尺寸、原内容尺寸保持、全部内容在父容器 padding 内，以及父宽 480 / 280 / 320 后的原重排。240 宽不足以同时容纳内容和新组，夹具回读预算拒绝该情况，移除新组并恢复原内容；**未把被裁掉的内容算作布局成功**。移除后原 typed 顺序、原 Frame 子树数量和两个内容对象的完整状态一致。全部对象释放后 GF Frame 计数回到起点，没有新增顶层 HWND。

首次使用通用 `width/height` 属性时返回成功但未生效，4 项检查失败，见 `out/live-v23/stack-contract-initial.log`。改用原 `rect` 属性并回读后得到有效尺寸。早期 38 项版本只检查位移，没有充分检查窄宽裁剪，保留于 `stack-contract-rect.log`；最终 42 项已加入正尺寸、子 Button 布局、裁剪边界和不足预算检查。以上均只发生在自有夹具。

关键已核验入口（此表是 ABI 证据，不是应用或业务控件选择规则）：

| 原 StackPanel 操作 | primary 字节偏移 | 包装函数 RVA |
| --- | --- | --- |
| 重排 | `0x2f4` | `0x4430` |
| 方向读取 / 写入 | `0x30c` / `0x310` | `0x93b00` / `0x17ca90` |
| typed item 读取 | `0x32c` | `0x17cc30` |
| typed indexed 插入 | `0x338` | `0x17caf0` |

## 原绘制通知：28/28，Release / Debug

新增 `framework-probe/txgui-paint-contract.h`，只编入独立夹具。静态追踪确认原 CFrame 通知 `0x39a` 进入实际背景/内容绘制分支，并确认原过滤器集合的注册、移除和分派方式。过滤器集合 IID 为 `85675A56-F6F5-4D26-8334-888FF31DDE43`，不是五参数的 Frame 消息接收接口；实际过滤回调是六参数，已通过原分派器调用验证，没有直接调用测试回调。

测试使用原 GF graphics 构造方法包装自有原画布，并严格沿原代码的栈对象生命周期：`0x272e0` 构造 0x40 字节原对象，`0x27630` 分离借用画布，再由 `0x273f0` 销毁原对象。原代码中的同样生命周期已在 `0x186e36` 调用链核验。原 graphics 虚表匹配 `0x1ecd6c`；图形引擎仍检查 v22 的 xGraphic32 CodeView、age 3。

夹具向原分派器 `0x63590` 提供自有对象的绘制通知。分派器调用原集合中注册的 observer，observer 只在目标 Frame、预期 graphics、当前线程和绘制事件均一致时接管该通知，并使用原填充接口画圆点。验证内容包括：

- 原过滤器两个阶段各被调用一次，实心抗锯齿圆实际出现在自有画布中。
- 无关通知不绘制、不消费处理标志，也不改变已有像素。
- 原接口移除 observer 后，后续绘制不再回调，重绘背景无圆点残留。
- 部分裁剪范围内的原通知绘制只改变裁剪内像素。
- 回调中自移除时，原集合的快照持有引用；回调结束后引用归还，后续无悬挂回调。
- 32 次原注册 / 绘制 / 移除循环后 observer 不残留引用；原 stack graphics 也没有残留借用引用。
- 预先配置一条真实原 click 订阅；绘制事务前后的订阅身份、所有原事件容器和 Button 几何保持一致，没有调用点击动作。结束仅清理夹具创建的订阅和对象。

这是**手工提交自有绘制通知，经原 GF 分派器验证的接入契约**。它尚未证明原窗口 renderer 自动遍历、graphics 的实际后端种类、窗口级失效/动画、输入捕获、DPI 改变或控制器崩溃恢复。`0x18acd0` 原 renderer 遍历入口本轮仅静态定位，未伪造其 context 进行调用，更没有在用户进程中试调用未知接口。

## 证据、回归与复现

`out/live-v23/stack-final-Release.log`、`stack-final-Debug.log` 均为 42/42；`paint-final-Release.log`、`paint-final-Debug.log` 均为 28/28。静态 ABI、CodeView、SHA256 与关键调用链见 `native-layout-paint-abi.txt`。

研究 CTest 两种配置各 **7/7**，包含原 Frame 50 项、SysButton 208 项、画布 49 项和两个新增契约；样式、位置恢复旧契约也继续通过。研究 Host 按当前 CMake 摘要重建，避免注册文件指向不存在的新 DLL 名称；没有覆盖用户进程内已映射的历史 DLL。

主程序既有 Release 产物完整回归 x64 **11/11**、x86 **7/7**，见 `ctest-main-x64.log`、`ctest-main-Win32.log`。`before-regressions.json` 与 `final-inventory.json` 核对三个原用户窗口 HWND/PID、Owner 为 0、完整几何和最小化状态一致，**3/3**，见 `final-state-comparison.json`；没有关闭、重启原应用，无残留研究测试进程。

主 52 项 C++ 格式及两个本轮研究源码的格式检查通过。VS 项目与筛选器各 **123** 个显式文件项，匹配、存在性、唯一性及无通配符检查通过，见 `vs-items.json`。正式适配器源码和注册范围未变。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
$ctestTool = Join-Path (Split-Path $cmakeTool) ctest.exe
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiFixture
& $ctestTool --test-dir demos/window-styler/out/framework-probe-Win32 -C Release --output-on-failure -R '^txgui_original_(stackpanel_layout|paint_dispatch)$'
# Debug：将两处 Release 同时改为 Debug。
```

两个契约只在研究构建的 `STYLER_GF_LIBRARY` 指向已安装 GF 时注册，不增加任何注入适配器编号。后续需要先确认真实 renderer 自动通知的 graphics profile、现有顶行可用祖先布局及裁剪，再连接原动作、布局与绘制；不能将本轮两项独立契约合称为实际窗口三灯已完成。
