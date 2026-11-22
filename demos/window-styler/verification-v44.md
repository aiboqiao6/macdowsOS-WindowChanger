# v44 验证记录（2026-11-22 至 23 日）

本轮没有修改正式运行时。完整候选仍为 `WindowChanger-v43-9751c3cbb38a-candidate.zip`，运行时 `9751c3cbb38a`，58 文件、57 清单项、2,528,855 字节，SHA256 `0EF1CAA02F57FC6C77F736BD124E182220EE0ABB69CEA8AA8A59A125C5233643`。该包含用户工作树已有的 Beta 标题修改；不等于干净 Git 提交的逐字节产物，也不等于 CI core-only 包。

## 当前候选已经完成的验证

| 项目 | 结果 | 范围 |
| --- | --- | --- |
| 实际完整包管理员生命周期 | 138/138 | x64、x64 到 x86、x86 各 46 项 |
| 实际完整包 WinUI 自动接管 | 60/60 | 正常退出、控制器异常退出两个场景 |
| 四配置完整 CTest | 84/84 | x64 Debug/Release 各 30，Win32 Debug/Release 各 12 |
| 正式后端 Debug 边界、焦点和配色 | 107/107 | 自有 144 DPI 单岛屿 WinUI 宿主 |
| 原生岛屿位置往返诊断 | 159/159 | 从未注入的深色标准 TextBox，三组独立窗口 |

管理员组验证实际自动接管、最小化延迟接管、两种架构 helper、托盘驻留与回调、单实例、暂停/恢复、持久化、异常恢复、最高权限交互登录任务注册及实际 TaskScheduler.Run。原设置和临时任务已恢复或清理。没有实际重启或重新登录系统。

自动接管组使用包内实际控制器，确认 Automatic 选择 Adapter288、原按钮隐藏/不可用且矩形为空、正文像素不变、暂停和退出恢复、托盘及控制器异常退出恢复。正式模块 SHA256 为 `103FB582265DBD2E0800CE297446A9D6E37E85DA877C3B3902AFEAA8641C4E72`，控制器为 `17F4ABBFE1BE077C87C5F33A7EA610DEF6BB5078281C68867EB15A9F0431D82B`。此组以子进程 RunAsInvoker 验证同权限接管；管理员启动的证据来自上一组，不能混为同一执行方式。

私有证据位于忽略的 `out/live-v44/elevated-package-r1`、`elevated-launch-r1.json`、`package-winui-auto-r2`、`boundaries-Debug-r4`；完整 CTest 日志及汇总在 `out/live-v42/*v44-r3*`。未把截图、应用库存或诊断文件打包。

## 未关闭的完整窗口像素失败

Debug 文本组 `text-Debug-r4` 深色正常恢复执行 216 项，215 项通过，最终整窗 PNG 严格对照失败。三个事先生成的独立未注入对照均为 `60D1D5EF3C2B86CA8ADB5BF4DEC17511FDB2A60C2C9AA99E9F74E3BE14610BE6`，目标为 `78153854F6745115B632E59EE7A6CE5CBCF8C6B3EA83CF013F7A49A7542249B3`。仅有以下三个 BGRA 像素不同，每通道差 1：

```text
243,259: 2E-2E-2E-FF -> 2F-2F-2F-FF
242,260: 2F-2F-2F-FF -> 30-30-30-FF
242,490: 30-30-30-FF -> 31-31-31-FF
```

此前的文本、选区、undo/redo 状态、焦点、几何、主题、单次 Loaded/无 Unloaded 和无编辑附加/撤销整图检查通过。根因尚未确定，不因此判定像素差异无害；没有增加容差、裁剪、遮罩、失败后补采参考或反复重跑改判。历史 Release 结果及多岛屿跨源诊断不解释本轮标准 TextBox 失败。差异明细在 `out/live-v44/text-pixel-differences-r5.json`。

新增仅限未注入自有 fixture 的原生操作对照：`-WithoutInjection -NativeFrameCycle position|presenter`。命令 28/29 只移动原岛屿再恢复，30/31 只切换公共 presenter 标题。position 组 159 项完成，三张最终图仍匹配上述原生对照；单纯位置往返未复现失败。presenter 组前两个窗口截图相同，第三个因检测到用户按住修饰键而安全中止，整组未完成，不能计通过。

## 不占用前台的生命周期入口

`test-native-island-initialization.ps1` 和 `test-native-island-lease.ps1` 新增显式 `-BackgroundOnly`。自有 fixture 使用 `--background`，显示和定位使用 NOACTIVATE，并放在其他窗口后方；后台模式不设置前台、不移动鼠标、不发送全局键盘输入、不截图。最小化恢复也不激活窗口。默认前台路径保留完整视觉检查；后台报告明确记录 `backgroundOnly=true`、`visualChecksPerformed=false` 和跳过的两项检查，跳过项不加入成功检查数，后台误调用截图函数会报错。

`r9` Debug/Release 各完成初始化 95 项、会话生命周期 54 项，共 298 项。覆盖初始化内根替换、嵌套撤销、宿主关闭、系统关闭、取消关闭、清除临时状态、去除临时标题岛屿、再附加，以及正常恢复、owner 进程消失、最小化后 owner 消失和正文替换。这是契约检查，未验收深浅色最终像素或实际软件布局。调用时显式使用 `out/vs-native-focus-x64/<配置>` 的正式注册表 Host；不能用陈旧研究目录替代正式模块。

`r11` 完成最终后台实现验证：Debug/Release 各初始化 95 项、会话生命周期 114 项，共 418 项，不与早期 r9 重复相加。会话组新增原生标题查询，确认原最小化、最大化和关闭按钮均隐藏、不可用、离屏且矩形为空。五次会话各向自有 HWND 发送三个旧 caption hit 的按下、松开、双击消息，共 45 次，逐次检查窗口仍存活、会话不变、没有最小化或最大化、几何不变。此处是目标消息契约验证，不是物理鼠标验收；没有发送全局输入或操作用户应用。后台窗口在显示、定位和最小化恢复后置于其他窗口后方。两配置报告均明确跳过两项视觉检查，证据目录没有生成截图。

最终自有 fixture SHA256：Debug `03769E86CA85899C5A9836757B5EE592900FF340976FEA837F322F395BDB8D0F`，Release `31670DC81CAC56EFC46E4D222930F6B5B808C2D0C260113C13798A1B8319FB5F`。正式 Host 分别为 `C31F412C96281191A58E6EBFACE9D4F96ECD2483895A37C88EFD377066E658EF`、`E4754FD4E5EC31CF4B5EA58B02217B9C61ECEEFF58EFFA85987C5E72FDFEDB2D`。改动未影响正式模块及包的内容身份。私有明细与汇总在 `out/live-v44/*-background-*-r11`、`background-r11.json`。

后台复现示例（在仓库根目录运行；使用新的证据目录保留历史结果）：

```powershell
$config='Debug'
pwsh -NoProfile -File ./demos/window-styler/research/winui3-probe/test-native-island-initialization.ps1 `
  -FixtureDirectory "./demos/window-styler/out/framework-probe-x64/winui-native-fixture/$config" `
  -HostDirectory "./demos/window-styler/out/vs-native-focus-x64/$config" `
  -OutputDirectory './demos/window-styler/out/my-initialization-background' -BackgroundOnly
pwsh -NoProfile -File ./demos/window-styler/research/winui3-probe/test-native-island-lease.ps1 `
  -FixtureDirectory "./demos/window-styler/out/framework-probe-x64/winui-native-fixture/$config" `
  -HostDirectory "./demos/window-styler/out/vs-native-focus-x64/$config" `
  -OutputDirectory './demos/window-styler/out/my-lease-background' -BackgroundOnly
```

前台初始化尝试 `r7` 错误使用未同步研究目录，模块加载 126，未执行到预期故障回调；修正 Host 路径后的 `r8` 完成根替换与再附加，随后在下一窗口因失去前台而中断。两次均保留且不计通过。当前后台结果不补认前台截图通过。

完整包 `r10` 再次通过逐文件清单、ZIP 对应、正式后端与构建身份检查，48 个 PE 文件架构兼容。当前改动只涉及自有验证宿主与脚本，不要求重新打包生产程序。

由于关闭标题/正文源的契约目标也包含此 fixture 源码，`r12` 重新编译该目标，并执行相关 CTest：Debug 和 Release 各 8/8 通过。其余已完成的 84 项完整 CTest 不重复相加。修改的 C++ clang-format、三个 PowerShell 脚本语法及 Git 空白检查通过；根 VS 项目和 filters 为相同的 237 个显式有效文件项，无重复和通配符。

## 尚未完成

当前修订的完整双配置文本、重建、前台初始化和会话、Release 边界、实际画图与多岛屿前台矩阵尚未全部完成。原像素失败仍开放。本轮未新增已验证的实际应用 profile；多岛屿研究仍未注册到生产自动注入，不能据此宣称实际记事本支持或全部已安装软件适配完成。支持判定仍按结构、能力、ABI 和布局契约；没有软件黑白名单，符合正式契约即尝试自动接管。
