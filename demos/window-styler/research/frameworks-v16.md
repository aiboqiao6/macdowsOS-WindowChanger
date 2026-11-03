# v16：BCG 左侧三灯原型、Office 操作语义、TXGui 原树事务

本轮新增一个已在真实窗口验证的 **MFC/BCG 原框架左侧三灯实验后端**。Office NetUI 和 TXGui 尚未完成窗口布局适配。正式控制器没有自动启用这些研究注册；实验工程与正式 `adapters.ini` 分开。

![原 BCG 顶栏内左侧三灯，实际窗口截图裁切](../images/mfc-bcg-v16.png)

## MFC/BCG：实际窗口验证通过

验证范围为本机 x64、150% 缩放下的一个静态链接 BCG 构建。静态链接映像 CodeView 为 `73D590B1-7FA3-4F9D-9CE2-E2A6D801C11C` / age 1；未知映像或原指令不匹配时拒绝。未按应用名称、安装路径、业务按钮名称选择后端。

原型在目标 GUI 线程按 BCG Ribbon HWND、MFC 运行时类和 HWND 一致性寻找真正的 `CBCGPRibbonBar`，再在其框架对象范围内发现 `CBCGPRibbonCaptionButton`。命令字段由原处理代码确认：`+0xbc` 对应 SC_MINIMIZE / SC_MAXIMIZE / SC_CLOSE，原矩形为 `+0xec`。当前活动的一组三个矩形具有实际标题区尺寸，未使用另一组空矩形实例。

原绘制入口为虚表槽 118，当前实现 RVA `0xa6ec00`。仅复制三个实例的原虚表并替换绘制回调，保留 RTTI 前缀及所有原操作。取得原 MFC CDC 中的 DC，在原按钮绘制流程中画圆点，没有新增标题行、遮罩或跟随窗口。

左移在原 BCG 元素矩形上执行事务：三灯位于 18/38/58 DIP，左侧原 Ribbon 内容预留 84 DIP。只移动标题同行的原框架元素；客户区、外框和窗口样式均保持不变。布局、最小尺寸计算和窗口变化进入时恢复原坐标，退出后重新应用变换，不以定位定时器维持位置。

BCG 的原非客户区输入路由不能直接命中新位置。最终方案只在三个新命中区由原 HWND 接收输入，并转换为原 Ribbon HWND 的正常鼠标事件；原 Ribbon 控件继续负责高亮、按下、捕获和系统命令分派，没有直接伪造关闭/最大化操作。

通过的实际检查：

- 三种真实颜色的中心像素及原绘制回调计数；截图保存了应用前、左移三灯、最大化和撤销。
- 外框、客户区、样式不变，目标子 HWND 数量不变。
- 原按钮消息路径的最大化、还原、最小化。
- 真鼠标输入的最大化、还原、最小化，点击前核对前台窗口、光标位置和所属根 HWND。
- 四次移动/缩放后三灯位置和间距稳定，未累积偏移。
- 重复附加/撤销；终止新建的测试控制器后，目标内生命周期检查自动还原。
- 测试结束恢复用户原窗口位置、最小化状态、前台窗口及光标。没有关闭或重启用户应用。

红灯保留原 SC_CLOSE 语义，本轮没有实际关闭用户窗口。跨屏 DPI、RTL、主题切换、全屏、Ribbon 树重建和窗口销毁顺序尚未完成广泛验证，因此保留为独立实验，不能声称任意 MFC/BCG 构建已兼容。

最终证据：`out/live-v16/mfc-complete/result.json`、`before.bmp`、`painted.bmp`、`maximized.bmp`、`restored.bmp`、`owner-failure-restored.bmp`。仅绘制原位置、初次左移、输入失败、检查基准错误和指令校验拒绝的早期记录均保留，不计为最终通过。

其中初版缩放断言混用了外框原点和 Ribbon 客户区原点，固定差 1 像素；改用真正的 Ribbon 原点后通过。初版真实鼠标路由未进入原 Ribbon，最终通过转换原 HWND 的输入解决，而非以消息测试冒充真实点击。

源码：`framework-probe/mfc-paint.cpp`、`mfc-paint-host.cpp`。实验 DLL 为 `MfcCaptionPaintProbe-9e74f2e0f727.dll`，研究注册编号 268。

## Office NetUI：读到原窗口操作语义

延续 v15 的三模块 CodeView 和原类型检查，本轮从 AppFrameHelper 的不可变 PropertyInfo 声明解析出：

| 属性 | 已核验的语义 |
| --- | --- |
| FramePart | CaptionButton=1、SysMenuIcon=2 |
| SysCommand | Minimize=0xf020、Maximize=0xf030、Restore=0xf120、Close=0xf060 |
| HitTest | Minimize=8、Maximize=9、Close=20，另有 Caption/Client 等原命中枚举 |

框架自身的整数属性读取器为当前 UI 库 RVA `0x333d0`；核验入口指令和原调用约定后，在原 GUI 线程读取了 32 个 AppFrameHelper 的 FramePart、SysCommand、HitTest，实际得到匹配的最小化、最大化、关闭命令和原命中值。Probe 始终拒绝 Attach，没有修改布局。

对象引用图内存在多组命令节点，不能仅凭有正确命令值就认定其为当前可见标题组。唯一活动组、父子布局、绘制、属性事务和撤销仍需验证。外部元数据读取还达到对象预算，不能当作完整视觉树。

源码：`framework-probe/office.cpp`、`office-metadata.py`。记录：`out/live-v16/office-property-read.json`、`office-property-schema.json`、`office-active.json`。Office 临时还原读取后已恢复原最小化状态。研究注册编号 269 未加入正式控制器。

## TXGui：真实 GF 对象的修改与回滚

本轮不再停留在只读枚举。在独立自有 x86 测试进程中，从已安装 GF.dll 的原 COM 工厂创建真实 GF Frame。使用 CLSID `DDA0ED51-B482-4A5B-A2B4-31A31768CD1D` 与原 IGFFrame 接口，未注册或替换系统 COM 配置。

50 项检查通过，包括：

- 读取初始 Rect、Padding、Margin、Hidden、Enabled。
- 用框架导出的 SetFrameAttribute 修改并回读属性；多组 Padding 不改变 Frame 外部矩形。
- 精确恢复原状态，重复恢复不产生累积变化。
- 在包含两个原内容节点的真实 GF 容器内，通过原方法插入三个临时节点，核验计数、顺序及对象身份，再撤销。
- 撤销保留两个原内容节点及顺序；自有对象全部释放后框架 Frame 计数回到原值。
- 事务没有创建顶层 HWND，没有修改运行中的 QQMusic 控件树。

早期独立 fixture 曾因遗漏 GF 的公开 InitDPI 初始化而在 GF.dll RVA `0xe290` 退出。现已在创建对象前调用 InitDPI，测试通过。该故障仅发生于自有 TxGuiFixture，不是用户应用。另修复了 CMake 正斜杠 DLL 路径与原生搜索选项组合导致的载入错误 126，并修正了插入锚点/清理顺序的早期测试假设；失败记录保留。

这验证了真实框架对象的原树事务，不代表 TXGui 三灯已经绘制或实际应用完成适配。运行中窗口的普通按钮操作语义、框架绘制和可见布局仍未完成。

源码：`framework-probe/txgui-fixture.cpp`。CTest 名称 `txgui_original_frame_transactions`，记录：`out/live-v16/txgui-ctest-final.log`，内部 `checks=50; failures=0`。仅使用本机已安装的 GF.dll，不分发第三方框架二进制。

## VS 编译及运行

```powershell
. .\demos\window-styler\development-tools.ps1
$cmake = Get-DevelopmentTool cmake

# x64：MFC/BCG 原型和 Office 只读研究
& $cmake -S demos/window-styler/research/framework-probe -B demos/window-styler/out/framework-probe-x64 -G 'Visual Studio 17 2022' -A x64
& $cmake --build demos/window-styler/out/framework-probe-x64 --config Release --target MfcCaptionPaintHost OfficeResearchHost

# 运行 MfcCaptionPaintHost.exe 会请求管理员权限。
# 无参数时按框架能力及已验证 CodeView 发现唯一匹配窗口，短暂测试后自动恢复。
# 有多个匹配窗口时拒绝自动选择，可以显式传入 HWND 与输出目录。

# x86：独立 GF 容器事务测试，GF 路径仅为本机测试依赖
& $cmake -S demos/window-styler/research/framework-probe -B demos/window-styler/out/framework-probe-Win32 -G 'Visual Studio 17 2022' -A Win32 '-DSTYLER_GF_LIBRARY=C:\Program Files (x86)\Tencent\QQMusic\GF.dll'
& $cmake --build demos/window-styler/out/framework-probe-Win32 --config Release --target TxGuiFixture
& (Join-Path (Split-Path $cmake) ctest.exe) --test-dir demos/window-styler/out/framework-probe-Win32 -C Release -R txgui_original_frame_transactions --output-on-failure
```

也可打开生成的 `out/framework-probe-x64/FrameworkResearch.sln`，使用管理员 VS，选择 Release/x64，启动 MfcCaptionPaintHost。它是可复现的实验验证器，不是正式后台自动接管程序。

## 本轮收尾检查

主解决方案 Release/x64、Release/x86 均编译成功；两种架构的 `strict_direct_policy` 回归均通过，未知框架仍不会落入原生加标题行路径。主 C++ 格式检查、六个研究 C++ 文件格式检查及新脚本语法检查通过。VS 项目与筛选器均为 97 个匹配的显式文件项，没有恢复通配符声明。

最终窗口盘点保存在 `out/live-v16/final-inventory.json`。本轮三个用户进程仍存活，Owner 均为 0，最小化状态及 NormalRect 与原记录一致。上述原型不扩展到尚未验证的其他 BCG 静态构建，也不代表 Office/TXGui 已完成三灯适配。
