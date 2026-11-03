# v25：原 D2D factory、离屏三灯像素与资源生命周期

2026-11-03。未使用 computer-use。本轮在自有隐藏 HWND 上创建了与 v24 真实按钮相同的原 D2DRenderTarget，并完成原 D2D 实心三灯的离屏像素验证。**没有向用户进程加载本轮代码，没有在真实应用窗口绘图或更改其布局。**

## 原 factory 创建与销毁

新增独立 x86 夹具 `framework-probe/txgui-d2d-contract.cpp` / `TxGuiD2DContract`，只接收已安装 GF.dll 路径，不接收外部 HWND 或 PID。

夹具检查 GF CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`、age 24，以及原服务 getter 和 service 虚表。通过原 `GetRenderSerice` 初始化，原 renderer 自动选择 kind 4；没有强行修改 renderer 选择标志。原 factory 针对自有隐藏的 96×48 HWND 创建 target，返回虚表 **`0x1eaf9c`**、target kind **0x10**，与 v24 真实窗口观察一致。

原创建过程交付一个 COM 引用。释放最终引用时，原基类析构自己从 factory cache 移除该 HWND 的条目；夹具回读 cache 数量验证恢复，没有直接修改原 cache 或对象字段。自有 HWND 在 target 释放后仍存活且隐藏，最后显式销毁并注销自有窗口类。所有操作都在夹具进程内，不创建用户进程内的覆盖层或标题带。

## 离屏路径与三个修正

实际 window target 的原 `EndPaint` 不是通用的内存 DC 截图接口。首次将其结果当作像素回读依据，11 项像素检查失败，证据 `out/live-v25/d2d-pixels-initial.log` 保留，没有因为返回值检查通过而宣布绘制成功。

原框架自身的 renderer 检查代码使用一个**按值传入 GUID 的 target 查询接口**。夹具核验并调用该原接口，使用 `ABCC9004-06D3-4836-BB33-1215A9B6A7F3` 获得真实原 compatible bitmap target，不伪造 context、不调用构造函数填充自制对象。

| 原对象 | 虚表 RVA | 用途 |
| --- | --- | --- |
| 自有 HWND 的原 D2DRenderTarget | `0x1eaf9c` | 与真实按钮相同的 target，由原服务/factory 创建 |
| 原 compatible bitmap target | `0x1eae94` | 通过原查询接口创建，用于本轮 CPU 可回读验证 |
| 两者共用的原 solid rounded fill | `0x19870`，虚表偏移 `0x58` | 三参数 `ARGB, RECT*, SIZE*`，实际 D2D 填充实现相同 |

第二阶段背景像素已正确，但重复 BeginPaint 后的圆点没有更新，6 项检查失败，见 `d2d-bitmap-initial.log`。静态追踪与标准 COM 查询确认：原 bitmap 回读路径取得了其自有 `IDXGISurface1` 的 DC。重复绘制前必须配对释放这个 DC。夹具在原回读后 `GdiFlush`，再通过公开 `IDXGISurface1::ReleaseDC` 释放**仅属于夹具的**原 interop DC；后续每帧回读和 ReleaseDC 均通过。没有写入任何私有对象字段。

同时修正了新夹具中的返回类型：原 native EndPaint 是通过 AL 返回的 C++ `bool`，不是通过 EAX 返回的 Win32 `BOOL`。最终版本按原 bool ABI 调用并继续逐像素断言。最初的失败记录未覆盖；这几项修正全部只发生在自有测试进程。

**重要边界**：本轮用于 `IDXGISurface1` 查询的内部字段只在已核验的自有 bitmap-target profile 上读取。不能把这个布局或 ReleaseDC 流程套到真实 HWND 的 window-target profile；两者的 BeginPaint、EndPaint 和资源字段并不等价。

## 最终 Release / Debug 各 58/58

最终证据：`out/live-v25/d2d-final-Release.log`、`d2d-final-Debug.log`。静态 CodeView、SHA256、原 factory、共享填充实现、bitmap 回读及 cache 清理调用链见 `original-d2d-abi.txt`。

- 原服务与 factory、原 HWND target、原 bitmap target 的身份和引用计数核验。
- 红、黄、绿三个 12×12 原 D2D 实心圆：中心颜色精确，包围盒角点保持背景，盒外像素全部不变；每枚变化 124 像素，其中 36 像素为抗锯齿混合。
- 原 clip getter、原局部 clip push/pop；原 D2D 填充方法仅改变裁剪范围内像素，pop 后状态恢复。
- 原 viewport / window origin 设置、读取及恢复；像素实际遵循 viewport 减 window origin，不在旧位置留下圆点。
- 原 scoped-alpha 接口同时管理 clip 和 opacity，验证半透明混合、pop 后恢复不透明绘制、全透明绘制不改背景。
- 原填充前后的 clip、两种 origin、global alpha 语义一致；不以对象原始内存的逐字节一致代替 API 状态恢复。
- 完整原重绘后所有输出像素恢复到三灯绘制前的背景。
- 预热后 32 次原 HWND target / bitmap target 创建、绘制、原回读、interop DC 释放及 COM 销毁，factory cache 每次恢复；GDI **3→3**、USER **7→7**。
- 显式销毁自有隐藏 HWND、注销窗口类。框架全局设备与 TLS 随自有进程退出，不手工卸载仍可能持有回调的原模块。

这里是**原 target、原绘图方法的离屏像素契约**，不是实际用户窗口已经显示三灯。尤其没有将测试中的 bitmap EndPaint 或 surface 字段用于真实窗口，也没有手工提交真实窗口的绘制通知。

## 构建与回归

新增研究 CTest `txgui_original_d2d_pixels`，仅在 `STYLER_GF_LIBRARY` 指向已安装原 GF 时注册；当前契约要求原 renderer 能自动选择已观察的 D2D family，不将其他后端冒充 D2D 成功。

研究 x86 Release / Debug 各 **9/9**；包含此前 Frame 50、SysButton 208、GDI canvas 49、StackPanel 42、原绘制分派 28、透传观察器 17 项契约和新的 D2D 58 项。x64 共享样式 / 位置恢复契约两种配置各 **2/2**。主程序既有 Release 产物完整回归 x64 **11/11**、x86 **7/7**。

本轮修改仅为研究夹具、研究 CMake、文档和 VS 文件项。CMake 摘要改变后，x86 三个研究 Host 和对应模块均按配置重建；没有覆盖用户进程已映射的历史 DLL。MFC/BCG 和 Office 适配器源码未改，正式控制器未新增注册编号。

最终 `final-state-comparison.json` 核对三个原用户窗口 HWND/PID、Owner 为 0、完整几何及最小化状态一致，**3/3**；`final-test-processes.json` 无残留测试进程。主 52 项 C++ 与新增研究源码的格式检查通过；VS 项目与筛选器各 **129** 个显式文件项，匹配、存在性、唯一性及无通配符检查通过，见 `vs-items.json`。

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
$ctestTool = Join-Path (Split-Path $cmakeTool) ctest.exe
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiD2DContract
& $ctestTool --test-dir demos/window-styler/out/framework-probe-Win32 -C Release --output-on-failure -R '^txgui_original_d2d_pixels$'
# Debug：将两处 Release 同时改为 Debug。
```

下一步才是将已验证的原 D2D 填充接入真实按钮绘制回调，核验真实窗口像素和撤销，再处理 v24 已确认的左侧容器空间不足问题。真实左侧布局、原输入、绘制和控制器异常恢复尚未合并完成；正式 TXGui 后端仍未启用。
