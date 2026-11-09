# 实际 WinUI 3 标题树诊断

`WinUI3TitleRead` 只读枚举当前 GUI 线程的现有 XAML 岛屿，复用产品 `winui3-inline.cpp` 中的运行时 ABI 校验、根枚举和标题匹配。未知模块 CodeView 或入口字节不匹配时不调用私有入口；没有重用 Windows.UI.Xaml 的诊断接口。它不修改 XAML 属性，不调用适配器 Attach，也不启用正式 WinUI 3 后端。

从 `research/framework-probe` 构建 x64 的 `WinUI3TitleRead` 目标，需要已经准备好的 `out/winui3-sdk`。程序及独立注册表输出到该构建目录的 `winui-title/Release` 或 `winui-title/Debug`，以匹配目标权限执行：

```powershell
.\WinUI3TitleRead.exe 0x当前HWND
```

日志只写入诊断构建目录。只记录类型、内部控件名、布局和主题状态，不采集 Text/TextBlock 的文字或文档内容；深度最多 24，每棵根树最多访问 1000 个节点，忽略纵坐标大于 120 DIP 的分支。结果不代表软件已适配。

主机使用已有 Session 的线程引导，因为本机外部线程 Hook 注册返回 126；先前仅使用外部 Hook 的尝试没有取得标题树。只读 Probe 写出报告后返回不支持，Attach 始终拒绝。主机核验没有留下 owner/inline 会话、外框/客户区和 caption style 没有改变；成功退出要求本次新报告完整生成。

2026-11-09 实际读取记事本 11.2607.14.0 和画图 11.2605.91.0。两者都使用匹配的 Windows App Runtime 2 / Microsoft.UI.Xaml CodeView `4ACDBE85-D668-192E-B8B6-A9334AC91B79` age 1，当前 144 DPI、深色。记事本在同一顶层 HWND 中有五个岛屿，分别承载标签栏、菜单栏、状态栏和两个滚动条；画图有一个填满客户区的岛屿。两者公共 AppWindow 均报告 `ExtendsContentIntoTitleBar=false`、`HasTitleBar=true`，产品的单一扩展标题行匹配均拒绝。截图只读复核与布局一致，未关闭记事本恢复的现有文档。私有证据在忽略的 `out/live-v35/installed-titles-r4`。

最终只读模块修订 `d48540a20979` 的 x64 Release、Debug 均构建成功，并分别读取以上两个实际窗口，四次主机退出码均为 0；没有留下会话、改变原 style 或几何。最终记录在 `out/live-v35/installed-titles-final-Release`、`installed-titles-final-Debug`。

## 已撤回的原生标题实验

在自建空白画图窗口中，精确匹配运行时并要求唯一、完整覆盖客户区的岛屿后，限时复用原生标题渲染仍失败：外框、客户区及客户区原点查询完全不变，原按钮查询也变为隐藏禁用，但实际工具栏上移，三灯像素为 0，原客户区截图 hash 改变。通过公共 SiteBridge 重申原矩形的第二轮也未修复。

两轮均在期限内撤销，原客户区像素 hash、几何和 caption style 恢复一致，没有修改主题。失败原型已从研究构建与注册表中撤回，未加入运行包；代码及失败截图仅保留在忽略的 `out/live-v35`。不能因为运行时、单岛屿和接口几何匹配，就为实际 WinUI 3 应用选择 GDI 原生标题兜底。

完成最终读取后，正常关闭自己创建的空白画图进程，没有强制终止；原用户 Word 和记事本保留。清理记录在 `out/live-v35/owned-paint-cleanup.json`。

本轮增加的是实际框架诊断证据，没有把记事本、画图、Picker 或真实 WinUI 3 记为适配通过；全机软件和两种主题的覆盖仍未完成。
