# v22：TXGui 原生画布与三灯像素契约

2026-11-02。未使用 computer-use。本轮完成的是**自有进程中的原框架画布绘制验证**，不是 TXGui 实际窗口的左侧三灯适配。没有向用户进程加载本轮新代码，没有修改用户控件树、输入、布局、绘制或事件订阅。MFC/BCG 与 Office 的实验范围保持 v21，不新增正式后端。

## 原绘图接口与版本保护

静态核验 GF 的原生图形依赖与导出调用约定，新增 `framework-probe/txgui-canvas-contract.cpp`，只在独立 x86 测试进程中加载已安装的原 DLL：

- GF：CodeView `D262C6B9-151B-4748-85A4-AF86943CFC63`，age 24。
- xGraphic32：CodeView `0FD7D531-C786-4D0B-B0FB-1E97996FDFBC`，age 3。
- 检查两份身份及每个使用的导出 RVA，错误 age 拒绝；不根据应用名、业务控件名或宿主回调地址选择后端。

重要修正：`GeoRoundRectEx` 是轮廓绘制，不能当作实心圆点。首轮 42 项检查有 9 项失败，表现为中心没有目标色、每枚圆只有 52 个轮廓像素变化；失败证据 `out/live-v22/canvas-initial-release.log` 保留。改为已经核验四参数调用约定的原 `FillRoundRectEx`，没有放宽实心圆检查来接受轮廓。

静态身份、文件 SHA256 和关键反汇编见 `out/live-v22/native-canvas-abi.txt`。原 `CreateCanvas` / `DeleteCanvas` 管理测试画布；GDI 只读取自有画布 DC 的像素，没有获取屏幕或用户窗口 DC，也没有外部覆盖绘图。

## 最终 Release / Debug 均 49/49

- 96×48 原画布上绘制红、黄、绿三个 12×12 实心圆；中心颜色精确、包围盒角点保持背景色、盒外所有像素不变。每枚改变 124 个像素，其中 36 个是抗锯齿混合像素。
- 通过原接口设置、相交、回读和恢复裁剪矩形；局部重绘只改变裁剪范围内像素。
- 原 viewport 与 window origin 的平移关系通过像素验证，并恢复两个原点。
- 半透明圆与原背景混合（通道取整误差限 2）；全透明绘制不改变任何现有像素。
- 绘制前后的原 filter、裁剪和两个原点一致；恢复背景后无测试圆残留。
- GF DPI 包装层创建的 96×48 逻辑画布在本机回读为 144×72，验证包装调用与轮廓语义；**不代表跨屏 DPI 切换已通过**。
- 预热后 128 次原画布创建、绘制、销毁，GDI 资源计数 1→1、USER 资源计数 5→5；整个契约的自有顶层 HWND 数不变。

最终逐项证据：`out/live-v22/canvas-final-release.log`、`canvas-final-debug.log`。研究 CTest 两种配置各 **5/5**，包括原 Frame 50 项、原 SysButton 208 项、样式和位置恢复旧契约，以及新画布契约；见 `ctest-research-release.log`、`ctest-research-debug.log`。研究两个入口也已按新 CMake 摘要重建，避免注册文件引用未生成的新 DLL 名称。

## 左侧布局和原绘制接入仍待验证

复查 v21 原树证据，真实顶部三枚 Button 共用原 StackPanel。只改三个按钮的局部坐标，不能证明它们能越过右侧父容器的裁剪范围，也不能证明左侧内容已让位。本轮没有移动该父容器或原业务内容，没有把画布契约当作原窗口绘制成功。

后续需要在自有原框架对象中确认可接入的实际绘制入口和祖先布局预留，再做有界原树事务、重绘与精确撤销。若入口只能通过尚未核实的虚表方法猜测，不能在用户进程试调用。本轮静态读到的事件过滤器、测量、XML 加载方法不作为绘制 hook；也未新增 HWND、标题带、跟随定时器或原生兜底。

## 回归与收尾

主程序既有 Release 产物完整回归：x64 **11/11**、x86 **7/7**，见 `out/live-v22/ctest-main-x64.log`、`ctest-main-Win32.log`。本轮修改仅在研究契约、研究 CMake、文档及 VS 文件清单；未改正式适配器源码。

`before-regressions.json` 与 `final-inventory.json` 比较 Excel、TXGui、BCG 三个原窗口，HWND/PID 存活、Owner 为 0、完整几何/样式/DPI/DWM/正常位置/最小化状态一致，**3/3**，见 `final-state-comparison.json`。未关闭或重启任何原应用，收尾无研究验证器进程。

主 52 项 C++ 格式及新增研究源码格式检查通过。VS 项目与筛选器各 **121** 个显式文件项，存在性、唯一性和匹配检查通过，见 `vs-items.json`。首次清单检查误把 4 个 `ProjectConfiguration Include` 当作文件，产生的无效检查报告 `vs-items-initial-invalid-check.json` 保留；改用明确的文件项类型后复核通过，项目本身没有缺失项。

## 复现

```powershell
. .\demos\window-styler\development-tools.ps1
$cmakeTool = Get-DevelopmentTool cmake
$ctestTool = Join-Path (Split-Path $cmakeTool) ctest.exe
& $cmakeTool --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiCanvasContract
& $ctestTool --test-dir demos/window-styler/out/framework-probe-Win32 -C Release --output-on-failure -R '^txgui_original_canvas_pixels$'
# Debug：把上述两个 Release 同时改为 Debug。
```

该契约仅当研究构建的 `STYLER_GF_LIBRARY` 指向已安装 GF 时注册。源文件和本记录加入 VS「框架研究」，继续使用显式路径，不增加通配符。正式控制器未注册画布契约，也没有启用实验 267/272/273。
