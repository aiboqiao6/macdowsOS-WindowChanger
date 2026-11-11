# v40 WinUI 宿主根与岛屿重建

日期：2026-11-11。延续 [v39](verification-v39.md) 的独立标题岛屿研究。本轮修复宿主关闭旧 XAML 源后的访问冲突及新岛屿恢复时的正文位置偏移，增加同一 HWND 内重建标准 TextBox 的公开回归。研究模块修订为 `0f54df5a5444`，研究引导为 `6ef015eec49c`，正式运行时仍为 `0a931b9107b1`。使用原生消息、真实 SendInput 和脚本截图，没有使用 computer use 或子代理。截图、调用栈及私有诊断只保存在忽略的 out。

## 修复与可复现检查

NativeIslandFrame 在建立会话时保留原 DesktopChildSiteBridge，读取源 Content 或调整原桥接前检查公开 IsClosed 状态。宿主关闭旧源后，即使 WinRT 引用仍存在，框架的 Content getter 也可能触发访问冲突；不能靠 C++ catch 继续读取。关闭后自动撤销旧会话，不访问旧源、不覆盖宿主提供的新内容。

恢复独立标题岛屿时，只有旧桥接已关闭才将原生客户区的最终 WM_SIZE 传给宿主，由宿主布局当前新源。旧源仍存在时，通过保留的桥接调整最终视口，不再重复触发宿主布局。附加过程中仍屏蔽临时视口尺寸，原正文保持原屏幕位置及尺寸。

该尺寸通知还可能重入 NativeIslandFrame::Resize。恢复期间 Resize 只调整原生正文视口，不再访问已关闭的临时标题岛屿，也不应用接管状态的标题最小尺寸限制。

fixture 将内容建立抽为 BuildContent，资源仅首次加载。每个标准 TextBox 的 Loaded、Unloaded、TextChanged 使用独立计数对象，保留退休控件的计数，不把旧控件事件计到新控件。新增三种命令：

- root：同一源内替换完整根及标准 TextBox。
- source：关闭旧 DesktopWindowXamlSource，立即创建新源及完整内容。
- gap：关闭旧源，间隔 750ms 后通过计时器创建新源，HWND 保持。

关闭前先清空 fixture 当前 source 引用，使重入的 WM_SIZE 不再操作正在关闭的桥接。只读指标包括源代次、桥接 HWND、根代次、退休控件加载/卸载计数及异步等待状态。

test-native-island-rebuild.ps1 对每种方式运行深色和浅色；每种先运行从未注入的独立原生对照，再运行研究接管窗口。旧控件先接受实际键入和选区，随后重建。检查旧租约退出且宿主存活、原生标题/属性/外框/客户区恢复、新岛屿视口和正文位置、新控件保留宿主初始文本和独立历史。旧控件卸载一次，新控件加载一次且不再卸载。

新岛屿随后两次重新接管，分别正常撤销和终止自有控制器。真实绿灯最大化/还原和 Ctrl+Z/Ctrl+Y 不得改变编辑状态或控件生命周期。重建后和最终完整窗口 PNG 必须与原生对照逐字节一致，不裁剪、遮罩或增加容差。私有最终汇总还检查两次恢复阶段的完整 PNG。

只向已核验路径、PID、Owned 属性和前台状态的自有 fixture 发送真实输入；只终止已核验 owner PID 的自有研究 Host。测试结束恢复前台及鼠标位置，不修改系统缩放、系统/已有应用主题、键盘布局或剪贴板，不关闭或重启用户已有应用。

## 构建与结果

x64 Release、Debug 的最终 fixture 和研究 Host 均构建成功。最终十二组检查全部通过，每种配置 1,596 项，总计 **3,192/3,192**。新增重建和独立原生对照共 1,592 项，使用当前产物重跑的既有五组回归共 1,600 项。

| 配置 | 根/源重建及原生对照 | 标准文本编辑及原生对照 | 初始化故障 | 边界、焦点与标题属性 | 亮暗窗口 | 外部注入与生命周期 |
| --- | --- | --- | --- | --- | --- | --- |
| Release | 796/796 | 480/480 | 97/97 | 107/107 | 60/60 | 56/56 |
| Debug | 796/796 | 480/480 | 97/97 | 107/107 | 60/60 | 56/56 |

最终证据为 out/live-v40/native-{rebuild,text,title,initialization,boundaries,lease}-{Release,Debug}-final-r3。私有 check-final-evidence.ps1 核对各组精确项数、每项结果、当前 fixture/Host 哈希及错误文件；独立复核重建的 48 对、文本的 16 对完整窗口 PNG 哈希。重建对照包含重建后、正常恢复、异常恢复及最终四个阶段，两配置全部一致。最终汇总为 out/live-v40/final-evidence.json，totalChecks=3192。中间修订和失败轮均不计入最终结果。

最终产物 SHA256：

| 产物 | Release | Debug |
| --- | --- | --- |
| WinUI3NativeIslandFixture.exe | `00A5E849F2DC9D6DFA6710B982C7BD95120BF18A170C6CB2BE909E87B4D1AF52` | `F1DF04CEB01FE47E3C94B34E5D3C30673AFCC1623D18933D4E81C8E3A286EED5` |
| WinUI3NativeIslandHost.exe | `E91A40796B5162989D11279D8F4C95F6B5AD2E0E3EFAA0345706163A2F03550D` | `86E002396C48E65D462EDE0BA368F651FE0CA290EC60DF993E524C82D5F5BCF7` |
| WinUI3NativeIsland-0f54df5a5444.dll | `7D3CA147C5DDAAE0618D0B8BA876A36D55A1F032C516EAB12DC9D1C9DA63A901` | `20A14D0FC14C6119A6E3DEDD3D94D08BDC3D2CF95FE9360DC837712003AE89B3` |
| 研究引导 6ef015eec49c | `1D7D2922BF39712E342C1C4EA7F14807EF8FFFC1ADD1BB2C2116D7B1C6682DA3` | `FCBA722BD5B6A890364EA6EC900242A9000DA47B29D3B0BAB330B4F0ACA6CBE4` |

实际空白画图使用当前两种配置各运行正常撤销和控制器异常退出，共四轮，全部通过。证据在 out/live-v36/paint-title-{Release,Debug}-v40-{normal,crash}-final-r3。每轮核对实际加载研究 DLL、引导 DLL、Host 的路径及哈希，不复用旧修订结果，也不以脚本退出码单独判定通过。

四轮都保留原外框，客户区只向上扩展，原工具栏、画布及状态栏的屏幕位置和正文 PNG 保持；原按钮查询隐藏禁用且矩形为空，旧 NC 按钮输入无效，三灯各有 212 个精确色像素。正常控制器退出码 0，异常轮 -1；目标自动恢复，恢复后完整窗口及正文 PNG 与原图逐字节一致，自有空白画图正常关闭。已查看最终接管及恢复截图，没有工具栏、画布或状态栏重叠。画图只覆盖当前深色空白状态，没有改变应用主题；DWM 20/33/38 读取成功且保持，34/35/36 返回 E_INVALIDARG，不计为颜色验证成功。

VS 项目与 filters 各 219 项显式文件，无缺失、重复、通配符或清单差异。两个修改的 C++ 文件通过 clang-format --dry-run --Werror；六个公开回归和三个私有收尾脚本通过语法解析，git diff --check 通过。结束后没有自有 fixture、研究 Host、调试器或画图进程残留。

## 失败证据

第一轮 source 的原生对照通过，注入侧重建后宿主退出。自有调试器在 Release 和 Debug 捕获 C0000005；Debug 符号栈为 Microsoft.UI.Xaml!DesktopWindowXamlSource::Content → NativeIslandFrame::OwnsSource → NativeProcedure。保留在 out/live-v40/rebuild-debug-r1、rebuild-debug-r2；调试器只附加自有 fixture，设置 DebugSetProcessKillOnExit(false) 并限时脱离。

加入桥接关闭状态检查后的 r2 不再崩溃，旧会话退出、新控件状态和外框恢复均通过，但完整截图差 50,820 个像素，边界为 x=240…959、y=255…599，涉及编辑区和 action 的位置。原因为恢复时拦截了宿主最终尺寸通知，新源仍沿用扩展客户区高度。该轮判为失败；补上宿主布局通知并加入视口/正文位置检查后，Release source r3 的深浅色严格像素对照通过。失败原图和日志保留，不以容差或遮罩放行。

中间修订 dd574f38a0f3 已通过 Release 重建 796、文本 480、初始化 97 和边界 107 项，但原有标题组在第一次正常撤销后失败，整组未通过。自有 Debug 调试栈确认第二处 C0000005：NativeIslandFrame::Resize 在恢复期间重入，读取已置空 titleSource 的 SiteBridge。记录在 out/live-v40/title-restore-debug-r1，失败标题组保留在 native-title-Release-final。最终修订使恢复分支只调整正文；先重跑原标题组，再重新执行全部最终矩阵，旧产物结果不替代最终结果。

随后 b7c2421530e4 的 Release 六组 1,596 项及两轮画图通过，Debug 标题 60 和重建 796 项也通过；但 Debug 文本的 dark-normal 最终整窗与独立对照差 3 个像素，集中于 TextBox 圆角边缘，边界为 x=242…243、y=259…490。编辑状态、历史、焦点和几何一致仍不能判整组通过。失败轮保留在 native-text-Debug-final-r2；未放宽像素比较。最终将尺寸通知转交限定为旧源已关闭的情况，以避免原源仍存在时桥接调整与宿主调整重复，并用新产物重跑全部矩阵。

## 交付边界

正式注册表、默认自动接管和运行包没有加入该研究模块。正式包仍为 out/WindowChanger-v34-0a931b9107b1.zip，2,438,465 字节，SHA256 为 `CD602B4C9072A53AC17D8C953D8DCFBF2070C72ED10221F8E833358D81ABB029`；没有创建 GitHub Release。用户已有 controller.cpp 两处 Beta 标题修改和根目录 debug.log 不纳入本轮提交。

自有根/源重建测试不能冒充实际应用宿主重建或完整文档编辑。全机软件、多岛屿、更多 ABI/DPI、跨屏、真实应用完整深浅主题和文档编辑、IME 等仍未完成。本机只有一块 144 DPI 显示器；allInstalledSoftwareVerified、bothThemesVerified、allWindowTypesVerified、imeVerified、crossMonitorVerified 仍为 false。
