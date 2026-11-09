# macdowsOS WindowChanger v34

解压后以管理员身份运行 `WindowStyler.exe`，保留旁边的 `x86` 目录。默认自动接管所有检测为支持的窗口，不按软件名称或是否单独测试过筛选；没有黑白名单。关闭主界面后驻留托盘；开机自启需在界面或托盘主动开启。手动还原会暂停自动接管，重新开启后再次检测。

本轮增加匹配原标题树的 Office 编辑窗口适配：三灯在原标题树中绘制，原布局为它们让出左侧空间，快捷工具栏、标题、搜索和账户区由 Office 自身重排。原按钮隐藏并禁用，Windows 标题栏查询也报告不可用，旧非客户区按钮消息被屏蔽。窗口变窄到布局保护边界外时自动恢复原布局；控制器退出或异常结束也会撤销。

实际验证包括当前深色空白 Word、Excel 编辑窗口，以及本机匹配的 Word、Excel、PowerPoint 起始页。Office 后端仍要求匹配三组模块 CodeView、144 DPI、从左到右的布局和已识别标题结构；PowerPoint 编辑窗口、其他 Office 页面及两种实际主题的完整验证尚未完成。

保留 v33 的原配色跟踪、原按钮输入禁用、Electron 标题避让、Avalonia 11.3.12 / FluentAvalonia 2.5 原树后端和可撤销接管。微信内嵌 Qt、Picker、真实 WinUI 3 应用及未知运行时/布局仍有缺口。**本机所有已安装软件的适配尚未完成。** 自有测试通过不等于所有实际软件、页面、DPI 与主题均通过，详见源码中的 `verification-v34.md`、`verification-v33.md` 和 `original-frameworks.md`。

包中包含 x64/x86 的 VC++ 可再发行运行库，不包含测试程序、软件库存、窗口标题、截图或私有诊断。源码与 GPLv3 授权见 [GitHub 仓库](https://github.com/aiboqiao6/macdowsOS-WindowChanger) 及 `LICENSE.txt`；MinHook 授权见 `third_party/minhook-LICENSE.txt`。
