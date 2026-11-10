# v39 WinUI 标准 TextBox 编辑状态验证

日期：2026-11-11。延续 [v38](verification-v38.md) 的独立标题岛屿，研究模块修订仍为 `5786259173ed`，研究引导仍为 `6ef65ac3c8ca`。本轮增加自有标准 TextBox 和可复现回归脚本，没有修改适配器、正式注册表或正式包。使用原生消息、真实键鼠输入和脚本截图，没有使用 computer use 或子代理；截图和私有诊断保存在忽略的 out 目录。

## 新增验证

--text-editor 使用标准 WinUI TextBox 默认模板，保留多行、选区和原编辑历史。fixture 提供 IXamlMetadataProvider，并在原岛屿 Content 建立后加载 XamlControlsResources；提供只读文本、选区、CanUndo/CanRedo、ActualTheme、焦点、实际尺寸及 Loaded/Unloaded/TextChanged 计数。原 --interactive 的 XAML Button 回归仍保留，不能把它当成文本编辑验证。

test-native-island-text.ps1 的深色/浅色 × 正常撤销/控制器异常退出共有四种场景。每种先启动一个独立且从未注入的原生对照，再启动研究接管窗口；每配置 8 个自有 fixture。每种场景的原生对照 50 项、研究接管 70 项，共 120 项。

检查覆盖真实键入、Ctrl+A 全文替换、Shift+方向键选区、选区替换、Delete/Backspace、多行、汉字和希腊字符、Tab/Shift+Tab 和 Enter。接管前建立同时具有 Undo 和 Redo 的历史；附加后 Ctrl+Y/Ctrl+Z 必须继续工作，撤销接管后 Ctrl+Z/Ctrl+Y 必须保留接管期间的多行编辑。真实绿灯最大化/还原、宿主内部深浅主题往返和正常/异常恢复前后，文本、选区、焦点、原岛屿焦点、历史能力、TextChanged 计数及控件尺寸必须一致。原 TextBox 始终加载一次、从未卸载；宿主正常关闭、没有 HRESULT 或 XAML 异常日志。

未编辑的接管/撤销要求完整窗口 PNG 与原图逐字节一致。完成编辑、最大化和主题循环后，要求完整窗口 PNG 与执行相同步骤的独立原生对照逐字节一致。截图前通过真实 Tab 将焦点移到原 action，消除光标闪烁，两侧仍比较完整图像，没有裁剪、遮罩或像素容差。测试保留各阶段文本和生命周期快照，输出目录已有结果时拒绝覆盖。

真实输入只发往核验过 PID、Owned 属性和前台窗口的自有 fixture；控制器异常轮只终止自己创建并核验路径及 owner PID 的研究 Host。结束后恢复原前台和鼠标位置。不修改键盘布局、剪贴板、系统主题、系统缩放或用户已有应用，不关闭/重启用户已有应用。Unicode SendInput 不代表 IME 组合输入验证。

## 构建与结果

x64 Release、Debug 的 fixture 与研究 Host 构建成功。十组检查全部通过，每种配置 800 项，总计 **1,600/1,600**。其中新增文本及独立原生对照 960 项，使用当前 fixture 重跑的既有回归 640 项。

| 配置 | 标准文本编辑及独立原生对照 | 初始化故障 | 边界、焦点与标题属性 | 亮暗窗口 | 外部注入与生命周期 |
| --- | --- | --- | --- | --- | --- |
| Release | 480/480 | 97/97 | 107/107 | 60/60 | 56/56 |
| Debug | 480/480 | 97/97 | 107/107 | 60/60 | 56/56 |

最终证据目录为 out/live-v39/native-{text,initialization,boundaries,title,lease}-{Release,Debug}-final。私有 check-final-evidence.ps1 要求各组精确项数、每项结果和当前 fixture/host 哈希匹配；文本组还独立复核四种场景的整窗 PNG 哈希及原生/研究分组项数。

最终 fixture SHA256：

- Release：`A3F8D61F6B23F886C979EA2548F410C2C8590D34DF4EA48D914E88E6A9D3D25F`
- Debug：`59CE08B5FA9CF09CC7DF74B2DD72B6E6457B91EFDA69238B5C7659DC02F01023`

Release 实际空白画图沿用 v38 正常/异常两轮证据；本轮研究 DLL、引导 DLL 和 Host 哈希均与其最终证据相同。最终核对发现 Debug 研究 DLL 和引导 DLL 的二进制哈希与 v38 不同，尽管本轮未修改两者源码，不能复用旧二进制证据。因此使用当前 Debug 产物重跑两轮自建空白画图，正常退出和控制器异常退出均通过，记录实际加载模块的路径与哈希。

新增 Debug 画图两轮均保持原外框、原工具栏和画布位置，原客户区仅向上扩展，三灯各 212 个精确色像素；旧按钮查询隐藏禁用，原 NC 三按钮输入无效。接管时原正文 PNG 不变，恢复后完整窗口及正文 PNG 与原图逐字节一致。正常控制器退出码 0，异常轮 -1；两轮目标自动恢复、自有空白画图正常关闭。已查看接管/恢复截图，没有工具栏、画布或状态栏重叠。实际画图只覆盖当前深色空白状态，没有改变应用主题。DWM 20/33/38 读取成功且保持，34/35/36 返回 E_INVALIDARG，不计为配色验证成功。

新增实机证据在 out/live-v36/paint-title-Debug-v39-{normal,crash}-final。当前 Debug 研究 DLL SHA256 为 `74E116E8E13B2E3BD06175ECA630299E829717245A45F7E6B4CF8241A2C19BBA`，引导 DLL 为 `C18740E3F6BCAE196E53D2A064DF242A6764E50E4B3DC0E874FAEAE01277CE10`。汇总逐项检查实际加载模块、预期字段、退出码、完整 PNG 哈希及错误文件，不以脚本退出码单独判定通过；文本 fixture 的结果不能冒充真实应用完整文档编辑验证。

## 失败实验与对照

早期 TextBox 未加载、宽度为 0，聚焦返回 E_INVALIDARG；这些启动检查未注入目标。仅在构造函数提前加载 XamlControlsResources 曾导致 C000027B，后续捕获到缺失 AcrylicBackgroundFillColorDefaultBrush 的 HRESULT=80004005。最终补齐控件元数据提供器并正确安排 Content/资源初始化顺序，标准模板可正常显示及接收真实输入。中间手工模板实验不计入最终标准 TextBox 结果，保留在 out/live-v39。

文本脚本 r1 的 Type 函数名被 PowerShell 内置 alias 解析为 Get-Content，已改为 TextInput。r2/r3 的键盘事件缺少物理扫描码，原生 TextBox 的 Shift+方向键未建立选区；补上 MapVirtualKey 扫描码、导航键 extended 标志和修饰键保持后，真实选区、撤销/重做才通过。失败轮没有进入研究附加。

r4 的编辑、主题、焦点与回滚状态检查通过，但最初完整截图与恢复截图相差 269 个像素，集中在 TextBox 圆角边缘，不能判为整组通过。独立未注入窗口重放相同编辑、原生最大化/还原及主题循环后，也出现最初/最终图像差异；该原生最终图像与研究恢复图像 SHA256 完全相同。最终脚本分别要求未编辑事务恢复原图，以及编辑事务与独立原生最终图一致，保留全部原图和失败证据。

## 交付与边界

新增脚本和本记录加入 VS 项目与 filters 的显式清单，两者各 217 项，无缺失、重复、通配符或清单差异。五个 PowerShell 回归脚本语法解析、fixture 的 clang-format --dry-run --Werror 和 git diff --check 通过。测试结束无自有 fixture、研究 Host 或画图残留。用户已有 controller.cpp 两处 Beta 标题修改和根目录 debug.log 不纳入本次提交。

正式包仍为 out/WindowChanger-v34-0a931b9107b1.zip，2,438,465 字节，SHA256 为 `CD602B4C9072A53AC17D8C953D8DCFBF2070C72ED10221F8E833358D81ABB029`。没有将研究 DLL 放入正式包或默认自动接管，没有创建 GitHub Release。

本机只有一块 144 DPI 显示器；未伪造跨屏或其他 DPI 验证。全机软件、多岛屿、更多 ABI/DPI、跨屏、真实应用完整深浅主题和文档编辑、IME、宿主重建及更多动态标题行为仍未完成。allInstalledSoftwareVerified、bothThemesVerified、allWindowTypesVerified、imeVerified、crossMonitorVerified 仍为 false。
