# v43 多岛屿原生标题事务的后台验证

2026-11-22。本轮继续用户选择的后台检查，不使用 computer use、子代理、全局键鼠或屏幕截图；不激活、关闭或重启用户应用，不修改系统缩放、已有应用主题、键盘布局和剪贴板。用户原有 controller.cpp 的 Beta 标题修改及根目录 debug.log 保留且不提交。

## 研究实现

新增 `research/winui3-probe/tab-caption-guard.h`，与原 `TabStripSlot` 分离。插槽继续在原 TabView 中预留 84 DIP，不重建、包裹或卸载原根；新事务仅在已有顶端客户区的自有研究窗口中，通过公共 OverlappedPresenter 隐藏原生标题并保留边框。附加前要求正常、非最大化窗口，同一 GUI 线程、同一顶层 HWND、原 presenter 带标题和边框且没有扩展标题，原岛屿 source.Content 与 TabView 的 XamlRoot.Content 为同一对象。关闭源先检查保留的 SiteBridge，不读取其 Content。

普通原生标题窗口的客户区不在外框顶端，附加前即拒绝。隐藏标题后再次核验外框、客户区尺寸、屏幕原点和三灯坐标；有变化则回滚并拒绝。没有以改写客户区或移动原生 EDIT、菜单、状态栏和滚动条补偿差异。当前选择条件只是研究边界，没有注册为正式后端，不能用于判定全机软件已支持。

三灯输入矩形由实际控件变换、XamlRoot 比例和对应 SiteBridge 子 HWND 的客户区原点共同计算，不把顶层原点当成标签岛屿原点。原 Caption 矩形排除该完整区域，Passthrough 增加三灯区域；其他矩形保留。保存原输入区域和原生标题配色，撤销精确恢复；如果宿主后来替换输入区域或修改标题配色，保留新值。宿主在隐藏标题的公开 API 调用中修改输入区域时拒绝覆盖其新状态。

旧按钮的 WM_GETTITLEBARINFOEX 查询报告隐藏、禁用、离屏且矩形为空，旧关闭/最小化/最大化的非客户区左键消息被拦截，旧按钮命中结果改为 HTCLIENT；其他命中结果和新三灯系统命令保留。真实隐藏依据 presenter.HasTitleBar=false，不以查询结果或移除 WS_CAPTION 冒充实际隐藏。框架在本机隐藏标题时仍可保留 WS_CAPTION。

重复事务不能覆盖已有子类；拒绝事务销毁时不能卸掉真正所有者的输入过滤器。公开 API 调用期间重入的关闭消息延迟到外层操作返回后重投。恢复所需输入快照仅采样一次，失败保留状态供显式重试。该类仍是研究对象，没有正式产品租约、外部 owner 进程监视、恢复失败调度或动态布局定时器；调用方负责 Refresh、撤销标题事务后再撤销插槽，不能以此替代产品状态机。

## 自有窗口方法

复用 `WinUI3TabStripTests` 的五个真实 XAML 岛屿和原生 EDIT，新增 `--caption-guard --custom-client`。自有宿主通过 WM_NCCALCSIZE 保留整个顶部客户区，用于验证与普通原生标题不同的布局形状；它不是实际记事本，也没有模仿记事本的全部非客户区行为。窗口 SW_SHOWNOACTIVATE 在底部显示，持续核验未成为前台。

新增深浅色标题事务及普通标题拒绝两组 CTest，与原插槽测试一起执行。检查原 Content 身份、五岛屿及 EDIT 的 HWND 几何、标签选中对象、原根和标签单次 Loaded/无 Unloaded、文档和撤销保留、原生标题真实隐藏和恢复、旧按钮禁用查询、九条旧按钮模拟鼠标消息、新三灯的实际 Click 回调与系统命令投递，以及输入区域的排除和精确恢复。实际系统命令由自有宿主拦截，不真正关闭、最小化或最大化。

宿主随后替换拖动/穿透区域并更新标题配色，要求撤销保留新状态；移动自己的标签岛屿后，旧输入坐标失效并恢复标题，撤销不覆盖宿主的新位置。另在标题移除期间让自有 WM_NCCALCSIZE 仅一次改变顶部客户区 1px，明确确认扰动已经执行，要求附加拒绝、过滤器清理及原几何恢复。最后真实执行 EM_UNDO 并核验原文档内容。

使用 RenderTargetBitmap 采样完整原标签根，分别独立连续稳定 300ms 后比较恢复的完整 BGRA 缓冲区。没有裁剪、遮罩、搜索匹配帧或像素容差。框架输出不含其他岛屿、原生 EDIT、非客户区和桌面背景，不能当成整窗截图或原生标题的像素证明。BMP 的透明背景在部分查看器中呈黑色，也不能作为主题错误或整窗主题通过的依据。

## 失败与结果

`r1` Debug 新增四个用例通过。`r2` 的深浅色布局扰动用例失败，其余四个用例通过：测试错误地依据 WS_CAPTION 消失触发布局扰动，而框架隐藏标题时仍保留该 style，扰动并未发生。旧日志保留。验证器改为一次性 WM_NCCALCSIZE 扰动，不再依据 style 判断，并增加“扰动确实执行”的断言；`r3` 的 Debug 深色标题事务通过。该失败是负面场景未被触发，不是生产程序崩溃，也不是修改生产代码后修复错位的证据。

最终 `r4` 的 x64 Debug/Release 每配置六个 CTest 全部通过：原插槽深浅色各 147 项、标题事务深浅色各 145 项、普通标题拒绝深浅色各 52 项，每配置 688 项，两配置共 1,376 项、12/12 用例通过。12 次正常恢复或拒绝后的原标签根位图比较均为 0 差异像素。布局扰动均明确执行，Apply 返回 E_NOTIMPL，事务 inactive，原几何/标题/输入区域恢复。新三灯命令始终由自有宿主拦截，没有执行真实系统动作。

根目录 VS 解决方案 x64/x86、Debug/Release 四配置均构建成功，没有项目通配符警告。两份 IDE 显式清单各 236 项，集合相同、路径存在，无重复或通配符。新源码 clang-format 验证、Git 差异空白检查通过。私有日志与原始位图位于忽略目录 out/live-v43、out/framework-probe-x64，四配置 VS 日志在 out/live-v42/root-vs-*-v43-r4.log，不提交、不打包。

~~~powershell
. ./demos/window-styler/development-tools.ps1
$cmake = Get-DevelopmentTool cmake
$ctest = Join-Path (Split-Path $cmake) ctest.exe
foreach ($configuration in @('Debug', 'Release')) {
    & $cmake --build ./demos/window-styler/out/framework-probe-x64 --config $configuration --target WinUI3TabStripTests --parallel 3
    if ($LASTEXITCODE) { throw 'TabView research build failed' }
    & $ctest --test-dir ./demos/window-styler/out/framework-probe-x64 -C $configuration -V -R '^winui3_tab_(caption|strip)'
    if ($LASTEXITCODE) { throw 'TabView background contract failed' }
}
~~~

## 尚未完成

没有在真实记事本或其他已安装多岛屿应用中运行新事务，没有完整窗口截图、真实鼠标命中和拖动、完整深浅主题、IME、跨屏或其他 DPI 验证。没有验证该实验事务中任意宿主关闭/重建、控制器异常退出、恢复 API 失败和所有重入组合，也尚未完成标签岛屿相对窗口左边的最终定位。实际应用也可能由客户区自行绘制原按钮，公共 presenter 的标题状态不足以证明这些按钮消失。

因此新事务没有加入正式注册表、默认自动注入或运行包。正式运行时仍为 b6ad8edbc843，上一候选包仍为候选；新包管理员生命周期、真实托盘唤回和全机适配未完成。总体任务不能标记为完成。
