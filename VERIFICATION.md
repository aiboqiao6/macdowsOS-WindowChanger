# Visual Studio 构建与代码清理验证

验证日期：2026-11-01。环境：Visual Studio 2022 Community 17.14、MSVC v143、CMake 3.31.6、.NET SDK 9.0.316；可选 Qt 6.11.1 msvc2022_64。

根目录 `macdowsOS WindowChanger.sln` 已连接实际 CMake/MSVC 构建。原 ButtonChanger 解决方案也已验证可以继续编译。VS 项目提供源码分组、C++20 IntelliSense、生成/重新生成/清理命令和 `WindowStyler.exe` 调试启动路径；生成的原生多项目解决方案保存在各构建目录内。

最新交付 v32（2026-11-03）：默认接管所有实际受支持窗口，不设软件黑白名单、已测试软件清单或逐窗口排除；检测到支持即尝试注入，保留明确暂停偏好与 ABI/原布局安全检查。随机类名、无 Sample 标记的自有窗口通过四配置和完整包跨架构生命周期各 46/46（共 230/230），纯契约各 23/23；全量 CTest x64 各 13/13、x86 各 9/9，首轮全部通过。手动还原改为全局暂停，重新开启后不排除任何支持窗口。现存三类框架窗口状态保持 3/3，真实用户设置恢复；未关闭旧控制器或用户应用，未使用 computer use。主 59 项格式、变更脚本语法与公开 VS 各 178 项显式清单核验通过。完整包 `demos/window-styler/out/WindowChanger-v32.zip` 的 50 项解压哈希零差异，不含测试工具。见 [v32 说明](demos/window-styler/release-v32.md)及[验证](demos/window-styler/verification-v32.md)。

最新交付 v31（2026-11-03）：主界面精简，托盘驻留/唤回、单实例、UIPI 下 Explorer 回调、登录后台自启、自动接管/暂停持久化及失败退避已完成。四配置和完整包跨架构生命周期各 44/44；计划任务实际启动与外部同名任务拒绝覆盖/删除通过；纯设置/队列契约 21/21。真实 MFC/BCG、Office、TXGui 的包内自动接管各 10/10，原窗口状态全部恢复。主 CTest x64 两配置各 13/13，x86 各 9/9；x86 Release 首轮原生边框像素失败日志保留，完整重跑通过，未改断言。主 59 项格式、新脚本语法、公开 VS 各 175 项 Git 跟踪显式清单通过。完整包 `demos/window-styler/out/WindowChanger-v31.zip` 含 x64/x86 运行文件、VC++ 可再发行库及授权，50 项解压哈希零差异，剔除测试工具。Git 已提升到根目录、保留历史和原元数据备份，远程为重命名后的 `aiboqiao6/macdowsOS-WindowChanger`；构建缓存/凭据/真实应用截图不入库。没有注销/重启或使用 computer use，用户实际自启设置保持不变。见 [v31 说明](demos/window-styler/release-v31.md)及[验证](demos/window-styler/verification-v31.md)。

最新进展 v30（2026-11-03）：TXGui 原 header/原三按钮迁移、原 D2D、原输入和持续会话整合完成；三序契约两配置各 23/23、组合自有契约各 36/36、真实有限事务各 26/26，持续会话各 55/55；Release 外层两轮 29/29、Debug 22/22。MFC/BCG、Office、TXGui 已验证有限 profile 接入默认主构建和完整 x64+x86 包；主自动路径 TXGui/Office 两配置各 8/8，包内跨架构 TXGui 和 Office 各 8/8。Debug/Release 引导名称隔离，原字节校验保留，无需重启用户进程。四配置编译通过，主全量 x64 各 12/12、x86 各 8/8；首轮 Debug 原生边框像素失败及重跑记录保留。研究 x86 各 15/15、x64 各 3/3；VS 工程/filters 168 项显式路径匹配，主 53 项格式通过。用户授权后 MFC/BCG 真实管理员主路径 Release/Debug/完整包各 10/10，三枚灯中心实际色值和位置、持续/重复附加、撤销及当前最小化状态恢复通过，无按钮输入；包装脚本的 Windows PowerShell 5 默认路径和 UTF-8 读取问题已修正，早期日志保留。未知版本/Office 编辑窗口/跨 DPI 不扩大支持，未使用 computer-use。详见 [v30 记录](demos/window-styler/research/frameworks-v30.md)、[使用与边界](demos/window-styler/original-frameworks.md)，运行包为 `demos/window-styler/out/WindowChanger-original-frameworks-v30.zip`。

最新研究 v29（2026-11-03）：真实 TXGui 已有标题 Frame 通过原 padding/margin 和原布局短时扩展并撤销，Release 两轮、Debug 一轮均 18/18，外层 28/28、23/23；原内容和标题按钮的实际位置不变，左右局部像素扩展中/撤销后逐点一致，完整 1,075 节点及完整原布局记录恢复。自有契约两种配置各 18/18，覆盖 32 次往返及异常回滚；原通用属性未生效、错误位置/尺寸接口、圆角外采样和 DIP/物理像素比较的早期记录保留，最终均已修正。研究 x86 各 13/13、x64 各 3/3，正式严格分派策略两架构通过；共享代码提取后的真实绘制两配置各 48/48，顺序 Release 10/10。本轮未重新执行主程序全量 UI 回归。三原窗口以本轮当前状态为基线核对 3/3、Owner 0、无残留测试进程；源码格式、脚本语法和 VS 各 153 个显式文件项核验通过。未使用 computer-use，没有增加标题带，尚未将三灯迁到左侧，正式后端未启用。见 [v29 记录](demos/window-styler/research/frameworks-v29.md) 和 `demos/window-styler/out/live-v29/`。

v28 研究（2026-11-03）：原 Element/Frame 独立顺序的严格 ABI 和完整 leaf 指令核验，自有契约 Release/Debug 各 40/40，覆盖全部 24 种排列、两链不同序、128 次往返、异常无分配回滚、空/单节点、原绑定和通知保持。原迁移契约改用双链恢复，各 22/22，不再移除/重插无关兄弟。真实顺序有限事务 Release 两轮、Debug 一轮各 10/10，外层 33/33、26/26；同步核对完整 1,075 节点，父身份/几何/状态/原绑定/过滤器不变，两链精确恢复。首次外层整树字节差异在纯只读对照也出现于同一底部原滚动子树；保留失败与 `fullTreeByteEqual:false`，不写回宿主动画。研究 x86 各 12/12、x64 各 3/3。未使用 computer-use，未扩展原标题 Frame、改坐标或将三灯迁到左侧，正式控制器未启用 276。见 [v28 记录](demos/window-styler/research/frameworks-v28.md) 与 `demos/window-styler/out/live-v28/`。

v28 收尾：主程序 Release 首轮 x64 9/11（自有示例最小化及侧/底边像素断言），单项复查 2/2、完整重跑 x64 11/11 与 x86 7/7，首次日志保留，根因未确定。三原窗口实验后即时状态比较 3/3；数分钟后 TXGui 出现还原和位移，来源待用户确认，最终初始库存比较为 2/3，当前新位置未强行覆盖。无残留测试进程、所有 Owner 0。源码格式、脚本语法和 VS 各 146 个显式文件项核验通过。

v27 研究（2026-11-03）：修复 TXGui 原树静默截断，新增原生预算及独立脚本完整性验证；真实 Release/Debug 完整原树均 1,075 节点、深度 20，只读各 27/27。更正 v26 所称“完整原树 512 项”：当时为限额内前缀，不能作完整拓扑证据。现已在完整原树下重跑绘制，Release 两轮/Debug 一轮仍 48/48、外层 31/31 与 25/25。左侧几何候选可以容纳 84 DIP；真实原容器的自有迁移/精确撤销及 32 次循环两种配置各 21/21，但通用 Insert 不支持按下一兄弟插回，夹具的后缀重建方案尚未验证适用于用户原树，实际布局未改。完整性契约原生 15/15、脚本 9/9；研究 x86 各 11/11、x64 各 3/3，主程序既有 Release 回归 x64 11/11、x86 7/7。三原窗口状态 3/3、Owner 0、无残留测试进程；源码格式、脚本语法和 VS 各 141 个显式文件项检查通过。未使用 computer-use，未启用正式后端。见 [v27 记录](demos/window-styler/research/frameworks-v27.md) 和 `demos/window-styler/out/live-v27/`。

v26 范围更正：下段历史结果的 512 项仅指截断前缀。原字形替换、标题区域像素撤销和原按钮绑定/几何验证有效；完整拓扑证明以 v27 重跑为准。

v26 研究（2026-11-03）：TXGui 三枚原标题按钮接入真实原 D2D 内容绘制，原右侧位置不变；Release 两轮、Debug 一轮均 48/48，外层 31/31、25/25。原字形在绘制中消失，撤销后完整 213×84 标题按钮区域逐像素恢复；旧枚举前缀的 512 节点身份/关系/顺序、原绑定、过滤器、几何和最终最小化状态保持，完整原树结论由 v27 更正。首次误用背景通知而保留原字形的截图、后续背景采样误报及最小化动画后验失败均保留；已改用原内容通知 0x385，并核验真正的字形消失和实际 outer/client bounds 恢复。共享原 D2D/分派契约两种配置各 79/79，位置恢复契约在两种架构/配置各 12/12；研究 x86 CTest 各 9/9、x64 各 2/2，主程序既有 Release 回归 x64 11/11、x86 7/7。三用户窗口最终完整状态 3/3、Owner 为 0、无残留测试进程；格式、脚本语法和 VS 各 134 个显式文件项核验通过。未使用 computer-use，未发送按钮输入、改布局或开启持续会话，正式后端未启用。左侧布局、动作/绘制整合和异常恢复仍待完成。见 [v26 记录](demos/window-styler/research/frameworks-v26.md) 与 [实际短时效果](demos/window-styler/images/txgui-d2d-v26.png)，证据 `demos/window-styler/out/live-v26/drawing-complete-*`。

v25 研究（2026-11-03）：通过原 service/factory 在自有隐藏 HWND 创建与实际按钮同型的 D2DRenderTarget，再通过原 compatible bitmap target 验证共享原 D2D 填充实现；Release/Debug 各 58/58，涵盖实心抗锯齿三灯、精确颜色、裁剪、平移、scoped alpha/状态恢复、原重绘、32 次完整绘制回读和销毁循环。原 factory cache 每次恢复，GDI 3→3、USER 7→7，自有 HWND 与窗口类显式清理。初始硬件 target 直接回读失败及 bitmap 回读未配对释放 DC 的失败记录保留；最终通过原 bitmap 接口和公开 IDXGISurface1::ReleaseDC 配对，并修正原 EndPaint 的 C++ bool 返回类型。研究 CTest x86 两种配置各 9/9、x64 共享各 2/2；主程序既有 Release 完整回归 x64 11/11、x86 7/7。三原用户窗口最终状态 3/3、Owner 为 0，无残留测试进程；主 52 项及新增研究源码格式、VS 各 129 个显式文件项检查通过。未使用 computer-use，未在用户窗口绘图、注入本轮代码或改布局，正式后端未启用；实际回调绘制、左侧空间分配和异常恢复仍待合并。见 [v25 记录](demos/window-styler/research/frameworks-v25.md) 和 `demos/window-styler/out/live-v25/`。

v24 研究（2026-11-03）：TXGui 真实窗口的三枚原标题按钮已接到原 renderer 自动生成的绘制通知；临时透传观察器 Release 两轮 / Debug 一轮均 39/39，外层原树与窗口恢复检查分别 29/29、24/24。全部 460 原节点身份、关系和顺序、原按钮几何/提示/命中/点击订阅及原过滤器身份顺序恢复。确认实际 graphics 为原 D2DRenderTarget（GF 虚表 0x1eaf9c），不匹配此前 GDI 夹具；未在用户进程调用未知 D2D 方法，未绘图、改布局、发送按钮输入或手工提交 Frame 绘制通知。新增自有透传契约两种配置各 17/17，研究 CTest x86 各 8/8、x64 共享契约各 2/2；共享 Office Host 已重建但本轮未重跑 Office 真实租约。主程序既有 Release 完整回归 x64 11/11、x86 7/7。三原窗口最终几何/最小化状态 3/3、Owner 为 0、无残留测试进程；主 52 项及研究 C++ 格式、PowerShell 语法、VS 各 127 个显式文件项检查通过。未使用 computer-use，正式后端未启用；实际 D2D 三灯绘制、左侧空间分配与异常恢复仍待完成。见 [v24 记录](demos/window-styler/research/frameworks-v24.md) 和 `demos/window-styler/out/live-v24/paint-restored-*`。

v23 研究（2026-11-03）：TXGui 自有真实 StackPanel 的左侧 84 逻辑像素留位、原重排、空间不足判定与原内容撤销，Release/Debug 各 42/42；原 GF 绘制通知分派、过滤器注册/移除、部分裁剪及回调中自撤销，各 28/28，保留一条真实原 click 订阅且没有触发点击。研究 CTest 两种配置各 7/7；主程序既有 Release 产物完整回归 x64 11/11、x86 7/7。初次通用尺寸属性未生效的 4 项失败和早期未充分检查窄宽裁剪的记录保留，最终补上回读及边界检查。三个原用户窗口的 HWND/PID、Owner 为 0、完整几何/最小化状态核对 3/3，无残留研究验证器进程。主 52 项 C++ 及新增研究源码格式检查通过，VS 清单各 123 个显式文件项匹配。未使用 computer-use，未向用户进程注入本轮代码；本轮仍是自有对象契约，真实窗口自动绘制、祖先布局和异常恢复未完成，正式控制器范围不变。详见 [v23 记录](demos/window-styler/research/frameworks-v23.md)，证据位于 `demos/window-styler/out/live-v23/`。

v22 研究（2026-11-02）：TXGui 自有进程的原生画布/三灯像素契约 Release、Debug 均 49/49，涵盖实心抗锯齿圆、原裁剪、坐标平移、透明混合、绘制状态保持及 128 次画布生命周期；两种配置研究 CTest 各 5/5。发现并纠正 `GeoRoundRectEx` 仅画轮廓的语义，初次失败记录保留，实心圆改用原 `FillRoundRectEx`。主程序既有 Release 产物完整回归：x64 11/11、x86 7/7。三个原用户窗口的 HWND、PID、Owner 清除及完整几何/最小化状态核对 3/3。新研究源码格式和主 C++ 52 项格式检查通过；VS 项目/筛选器各 121 个显式文件项匹配，无缺失、重复或通配符。未使用 computer-use，未将本轮代码注入用户进程，正式控制器范围未变；原窗口绘制接入和 TXGui 左侧布局仍未完成。详见 [v22 记录](demos/window-styler/research/frameworks-v22.md)，证据位于 `demos/window-styler/out/live-v22/`。

| 构建 | 编译结果 | CTest 结果 | 最终测试日志 |
| --- | --- | --- | --- |
| x64 Release | 通过 | 11/11 | [日志](out/tests-release-x64-final.log) |
| x64 Debug | 通过 | 11/11 | [日志](out/tests-debug-x64-final.log) |
| x86 Release | 通过 | 7/7 | [日志](out/tests-release-x86-final.log) |
| x86 Debug | 通过 | 7/7 | [日志](out/tests-debug-x86-final.log) |
| x64 Release + Qt/WPF 示例 | 通过 | 16/16 | [日志](out/tests-optional-x64.log) |

共 52 次 CTest 执行全部通过，其中包含重复运行的基础回归，不代表 52 个不同测试。WinUI 3 fixture 的 92 项内部检查通过。测试范围包含自有 Win32/WPF/XAML/WinUI 3/Qt 测试窗口、注入、布局、操作和恢复；本次没有重新验证真实第三方应用的兼容范围，也没有启动管理员自动接管界面。

代码清理使用根目录 `.clang-format`：52 个第一方 C++ 源文件/头文件完成格式化，`scripts/cleanup-code.ps1 -Check` 通过。清理前备份：[sources-20261101-161317-443.zip](out/code-cleanup/sources-20261101-161317-443.zip)。生成的 SDK、第三方代码、历史研究和截图保留。

构建过程中修正的问题：

- 旧 VS 项目没有源文件；现在能从解决方案编译全部实际模块。
- 复制来的 CMake 缓存包含旧绝对路径；新构建使用 `out/vs-*` 目录。
- XAML/WinUI 3 与系统交换 C++ 容器，Debug 后端需要 release CRT 和 `_ITERATOR_DEBUG_LEVEL=0`；保留 `/Od` 与调试符号。修复前相关 Debug 测试失败，修复后全部通过。
- .NET 中间文件按构建目录隔离；WPF 示例排除旧 `obj/bin`，避免重复程序集属性。
- 导出符号显式校验 ASCII，修复隐式窄化警告；COM 导出标记 `PRIVATE`，修复链接器警告。
- WinUI 3 检查脚本显式加载当前 PowerShell 对应的工具模块，修复从构建环境启动时 `Get-FileHash` 不可用。
- 回归检查在宿主刷新和最大化/恢复后等待实际布局或动画稳定，再继续后续操作；逐次同步 resize 检查仍然保留。

第三方 MinHook 的既有编译警告和 Qt 私有 API 版本绑定提示仍保留。本次未扩大 WinUI 3、MFC/BCG、Office NetUI 或 TXGui 的实际应用适配范围。

VS 项目加载警告复核：`.vcxproj` 和 `.filters` 均使用 80 个显式相对文件路径，项目全局增加微软文档支持的 `ReplaceWildcardsInProjectItems=true`。通过 VS 2022 自带 `devenv.com` 加载原 ButtonChanger 解决方案并构建 Debug x64，退出码为 0；构建日志和 ActivityLog 均未包含通配符项目项警告。记录：[VS 加载及构建日志](out/vs-ide-load-verified.log)、[ActivityLog](out/vs-ide-activity-verified.xml)。未更改已打开的 VS 窗口或清除用户缓存。

后续框架研究入口已添加到解决方案的「框架研究」筛选器，当前为 153 个匹配的显式项目项，继续保留逐文件声明。BCG 原型范围见 [v16](demos/window-styler/research/frameworks-v16.md)；Office/TXGui 后续验证见 [v17](demos/window-styler/research/frameworks-v17.md)、[v18](demos/window-styler/research/frameworks-v18.md)、[v19](demos/window-styler/research/frameworks-v19.md)、[v20](demos/window-styler/research/frameworks-v20.md)、[v21](demos/window-styler/research/frameworks-v21.md)、[v22](demos/window-styler/research/frameworks-v22.md)、[v23](demos/window-styler/research/frameworks-v23.md)、[v24](demos/window-styler/research/frameworks-v24.md)、[v25](demos/window-styler/research/frameworks-v25.md)、[v26](demos/window-styler/research/frameworks-v26.md)、[v27](demos/window-styler/research/frameworks-v27.md)、[v28](demos/window-styler/research/frameworks-v28.md)、[v29](demos/window-styler/research/frameworks-v29.md)，不扩大上述正式控制器兼容范围。

v17：Office 完整 Node 树 1,896 项结构校验通过，唯一活动标题系统按钮组已识别。Release 连续两次原有子元素属性事务均为 49/49，外层窗口/树状态检查 26/26；Debug 一轮为 49/49、外层 22/22。研究引导模块与 Office DLL 按配置隔离名称，避免已固定映射的 Release 模块导致 Debug 实测被拒绝；正式产品的引导保护未变。TXGui 的真实 SysButton 契约 88/88、原 Frame 契约 50/50，Release/Debug CTest 均为 2/2。实验工程 Office x64、TXGui x86 的 Debug/Release 编译通过；两个架构的正式控制器 `strict_direct_policy` 回归通过。主 C++ 格式检查、新研究 C++ 格式检查、Python 编译与 PowerShell 语法检查通过。Office/TXGui 的左侧布局和绘制仍未完成，未将属性回滚或独立对象测试当作实际适配成功。

v18（2026-11-02）：Office 起始页的原树左侧三灯、原鼠标消息路径最大化/还原/最小化与撤销通过，Release 两轮均 124/124、外层 38/38；Debug 一轮 124/124、外层 34/34。共享样式层 16 项检查在 x64/x86、Debug/Release 均通过；主程序 Release/x64 11/11、Release/x86 7/7 全部通过。TXGui 只读 11/11，原事件分派与内部按钮身份已确认，但当前可见标题按钮仍未关联。Office 为测试后恢复的有限事务，持续附加、异常恢复和更多窗口类型尚未完成；正式控制器范围不变。

v19（2026-11-02）：Office 起始页的独立持续挂载实验完成，Release 两轮均 37/37、外层 38/38，Debug 一轮 37/37、外层 34/34；覆盖持续原绘制、状态切换/缩放后的像素、重复撤销、错误/嵌套撤销及自有控制器 HWND 销毁和进程终止后的自动恢复。原 1,896 节点树和窗口状态恢复，正式后端未启用；用户窗口销毁、原树重建、编辑窗口及跨 DPI 尚未实测。TXGui 21 次原生命中查询成功，对应三枚真实顶部按钮，Release/Debug 只读契约 17/17；事件语义和三灯布局仍待完成。研究样式及 TXGui fixture 契约通过；主程序 Release/x86 7/7，Release/x64 最终全量复跑 11/11。x64 首轮 WinUI3 测试控制器终止曾遇到 Access denied，单项及全量重跑均通过，原失败日志保留。未使用 computer-use 工具。

v20（2026-11-02）：Office 增加原输入取消、捕获释放和按下中撤销；Release 两轮均 55/55、外层 38/38，Debug 一轮 55/55、外层 34/34。覆盖原按钮仍按住时显式撤销、所有者 HWND 销毁或控制器进程终止，以及嵌套诊断失效和主题通知后的恢复；未关闭用户窗口，未更改系统主题。原树/窗口状态恢复。TXGui 92 个原按钮事件容器读取完整且两次读取一致，顶部三枚按钮的原点击链均读到不同宿主回调；Release/Debug 只读契约 22/22，独立真实 SysButton 契约 157/157。未按宿主回调地址选择后端，通用操作语义及 TXGui 三灯布局仍未完成。研究样式/Frame/SysButton 回归通过；主程序 Release/x64 11/11，Release/x86 首轮 6/7（原生侧边/底边像素断言），完整重跑 7/7，首次日志保留。实验模块修订摘要增加验证器源码以避免覆盖进程中已映射的 DLL。未使用 computer-use 工具，正式控制器范围不变。

v21（2026-11-02）：TXGui 新增原 Button 提示属性和动作意图核验，不按业务名称或宿主回调地址选择。原鼠标消息路径的最大化、还原、最小化有限实验：Release 两轮均 25/25、外层 29/29；Debug 一轮 25/25、外层 24/24。全部 460 个原节点身份、关系和顺序及标题按钮绑定/几何恢复，关闭按钮未触发、布局未改写、无持续会话。只读检查 25/25、真实 SysButton 夹具 208/208；初始属性接口“返回成功但未生效”的自有夹具失败记录保留，改用原 Button setter 后回读通过。Office 增加宿主改变按钮能力时撤销并保留最新请求，最终 Release 两轮 60/60、外层 38/38，Debug 一轮 60/60、外层 34/34，包含最终位置/最小化状态恢复检查。首次全局核对发现 TXGui 未保持最小化（2/3）；研究验证器新增共享不激活式恢复及状态回读，自有窗口在 x64/x86、Debug/Release 均 10/10，完整复跑后全局 3/3、无残留会话。原失败记录保留。研究样式/Frame/SysButton 契约通过；主程序 Release/x64 11/11、Release/x86 7/7，正式控制器仍未启用实验。未使用 computer-use 工具；TXGui 左侧三灯布局和绘制仍待完成。
