# v45：新版 Office 隔离研究与 WinUI 渲染诊断

日期：2026-11-23。本轮按后台验证方式推进，不使用 computer use，不向用户应用发送全局键鼠输入。正式运行时及完整候选包仍为 `9751c3cbb38a`；没有把新版 Office 研究后端注册到正式自动注入。

## 本机 Office 更新后的实际缺口

本机 Office 已更新为 `16.0.14334.21048`。磁盘 PE32+ CodeView 检查确认三个模块均与正式 profile 不同：

| 模块 | 当前 CodeView GUID（age 均为 2） |
| --- | --- |
| mso40uiwin32client.dll | 3E4B8C5A-B16A-4DE5-B063-338EDF2BB546 |
| mso98win32client.dll | 587D5BED-5243-4E42-9E0E-87C87C6A4E20 |
| mso.dll | 6FF021ED-7C7B-4196-BC82-49F056FF3CF8 |

旧地址的 12 组入口检查中 10 组不匹配。现有版本的实际 Office 窗口不能沿用旧版本的适配结论，也不能仅替换 GUID 后继续调用旧偏移。10 个原属性描述符地址仍对应 FramePart、SysCommand、HitTest、content、fontface、fontsize、foreground、padding、isvisible 和 isenabled，但这不证明函数 ABI 和布局已通过。

通过保存的旧版导出表与当前 PE 导出表的相同序号，重新定位 17 个入口，包括 Get/Set/Remove、引用释放、值构造、节点保留/销毁和四个 Node 链接访问器。工厂从原类信息表解析，Visible 包装器通过独立字节与引用检查定位。不能把短指令前缀的候选搜索当成可调用接口证据。

## 只读入口与隔离后端

`office-readonly-profiles.py` 将两个已知模块三元组整体匹配，拒绝缺失模块、混搭版本、不同 age、未知 GUID 及大小写重复模块。原树读取再检查选定版本四个完整父子/兄弟访问器指令，保留原有父链接、兄弟回链、环和节点预算检查。六组 Python 契约通过，并纳入 GitHub CI。元数据和树报告明确区分只读 profile 与正式注入能力。

新建的隐藏 Word、Excel 自动化实例分别读到 170、186 个原树节点，结构完整；没有运行注入模块。首轮 Word 未创建文档时没有可用 HWND，验证器拒绝操作；该空白测试实例最终经身份核对后正常关闭，没有强制终止。首轮 Excel 在退出等待限额内未退出，原失败保留；显式释放中间 COM 集合/窗口引用并等待终结器后，两种新测试实例均正常退出。

`office-21048-research.cmake` 只在独立研究工程生成当前版本的副本，保留同一套原树布局与撤销代码，替换明确列出的函数 RVA 和完整模块三元组。生成目录、DLL 和引导库均按研究内容身份隔离。`office-21048-verify.h` 在任何原树调用前检查三个模块 GUID/age、17 个导出序号与地址、入口指令、工厂/Visible 两个内部入口及 10 个属性名称；现有布局、类型、引用、DPI、LTR、可用空间和撤销检查继续执行。正式原树源代码与生产注册表不因此放开新版本。

## 实际新版 Word / Excel 后台事务

实验只使用验证器新建的进程和空白文档；存在 Word/Excel 进程时，公开验证脚本会拒绝开始，保留已有实例。显示自己的测试窗口时，先临时禁用该 HWND，再调用 Office 公共 Visible 属性初始化真实标题树，随后恢复启用并放在其他窗口后方。测试前后核对 HWND/PID，窗口不能成为前台；没有调用 SetForegroundWindow 或模拟键鼠。

早期仅调用 Win32 ShowWindow 的自有 Word 实例仍保留折叠的 NetUI 树：按钮矩形为零，虽然读取到 SC_MINIMIZE/SC_MAXIMIZE/SC_CLOSE 与对应 hit，布局探测正确拒绝，原树和几何一致。这不是接管通过。私有生成器首轮 GUID 替换未生效的失败也保留，不计入最终结果。

公共 Office 显示入口初始化树后，私有原型先完成 Word 1,032 节点、Excel 1,096 节点的事务。整理为带更严格前置校验的仓库研究目标后，Release 的 Word、Excel 均通过：附加成功、会话查询有效，原最小化/最大化/关闭按钮在原生标题查询中均隐藏且不可用、矩形为空，正常撤销成功、窗口外框/客户区/样式恢复。撤销后全部原树节点、顺序、父子关系和已有几何字段精确匹配附加前；两个应用均无残留测试进程。模块报告 cleanupFailures=0。这是后台结构/事务验证，不是物理按钮、整窗绘制、真实深浅色或业务文档验收。

复现（仓库根目录，保持 Office 应用关闭，使用新的输出目录）：

```powershell
. ./demos/window-styler/development-tools.ps1
$cmake=Get-DevelopmentTool cmake
& $cmake --build ./demos/window-styler/out/framework-probe-x64 --config Release --target Office21048BackgroundHost
pwsh -NoProfile -File ./demos/window-styler/research/test-office-21048-background.ps1 `
  -HostDirectory ./demos/window-styler/out/framework-probe-x64/office-21048-research/Release `
  -OutputDirectory ./demos/window-styler/out/my-office-background
```

Debug 研究目标重新编译后，Word、Excel 也分别通过相同的精确树/几何与正常恢复检查，节点数仍为 1,032 和 1,096，清理均无失败。两种配置共四个实际自有应用事务，不与私有原型或只读遍历重复累计。Debug 证据为 `office-guarded-Debug-r26`，Release 为 `office-guarded-Release-r25`。

新增代码的 C++ 格式、Python 与 PowerShell 语法、六组 profile 契约和差异空白检查通过。根 VS 项目和 filters 同步为 244 个显式有效文件项，无重复或通配符。完整候选的逐文件清单、ZIP 和构建身份于 r28 再次复核通过；本轮不重复运行无关的已通过四配置 CTest，也不以研究结果替代正式包验证。

还需实际深浅色绘制、真实按钮操作、最大化/最小化、尺寸变化后的恢复验收、更多文档状态及正式打包矩阵。当前新版 Office 仍是隔离研究支持；不能称为已经默认接管当前电脑上的 Office 或全机软件。

## 所有者异常退出与后台绘制补验

`test-office-21048-background.ps1 -Scenario owner-loss` 等待子控制器明确报告已附加且原按钮隐藏/禁用，再核对会话 HWND 的 PID 与控制器完整路径，仅终止该测试控制器。宿主的有界等待在父进程没有执行终止时会正常清理，不长期遗留接管。验证器异步读取输出，每条流只保留一个读取者。

Debug、Release 的 Word/Excel 四组异常退出均通过：控制器预期退出码 -1，两个会话属性清除，窗口外框/客户区/样式及完整原树精确恢复，cleanupFailures=0，Office 自有实例正常退出。最终源版本证据为 `office-loss-Release-r40`、`office-loss-Debug-r41`；此前 r30/r31 不重复累计。增强后的默认正常退出也分别复验通过，证据为 `office-normal-Release-r42`、`office-normal-Debug-r39`。每组单独保留 lease-report.json，避免后续 PID 重用覆盖模块清理结果。

私有 `office-print-r32.ps1` 在明确接管期间调用 PrintWindow(PW_RENDERFULLCONTENT)，不抢前台。Word、Excel 均得到 1200×720 绘制图，左上区域各有红/黄/绿实色像素 114/114/114；查看两张完整图后，当前深色主题下三灯位置、快速工具栏右移和原按钮组消失均可见，未见明显重叠。两次之后的控制器异常退出和清理仍通过。这是后台请求绘制图，不是桌面截图、真实深浅色切换或物理输入验收；visualVerification 保持 false。图中含账号信息，仅保存在忽略目录，不公开发布。

## 尺寸边界与原生重布局对照：严格失败仍保留

新增 `-Scenario resize` 是诊断入口，不能据宿主退出码单独宣称通过。它验证当前最小编辑标题宽度、放大和恢复三阶段，每阶段核对实际尺寸、接管健康、原按钮原生隐藏/禁用及三个框架按钮的 typed false。低于已测宽度 24 像素时必须撤销，随后重新附加必须拒绝且不改变窗口几何/样式/会话属性。

首次固定缩小到 1140 像素的 Word 在阶段一退出接管；诊断确认实际最小宽度为 1164。这是已有空间限制的触发，r34/r35 保留失败。按实际边界测试后，Word 的 1164→1320→1200、Excel 的 1023→1320→1200 三阶段均保持健康，越界后撤销和拒绝重新附加也成立。但完整原树逐地址比较仍失败，验证脚本继续返回非零，不计为恢复验收通过。证据为 Release 的 r36/r38 和 Debug 的 r43。

独立新建且未注入任何模块的同类原生实例重放相同尺寸序列，也发生原树变化：Word 从 1032 到 1080 节点，Excel 从 1096 到 1105 节点，部分滚动条节点被宿主重建。将两个独立进程的地址/HWND身份排除后，对每个完整根树逐节点比较深度、vtable RVA、类、子节点数量、顺序和全部已读取 AppFrameHelper 几何，再对根树集合比较：原生对照与 Release 接管测试的最终结果分别一致。没有裁剪布局、忽略几何或覆盖原始失败。该对照解释了原始节点身份不能作为跨重布局的唯一标准，但不替代真实视觉验收，也没有修改现有严格通过条件。原生对照为 `office-native-resize-Word-r37`、`office-native-resize-Excel-r42`，均正常退出。

上一笔源码提交 ab97664 的 GitHub 四配置构建与契约检查已成功，运行号 45068718869。以上新增内容只扩展研究验证，不改变正式运行时、注册配置或完整候选包。

## PowerPoint 无文档视图外壳的后台扩展

公开验证器新增显式 `-Applications PowerPoint`，默认仍为 Word/Excel。PowerPoint 没有沿用 Word/Excel 的 UserControl 判据；开始前要求没有 POWERPNT 进程，COM 创建后只有一个新进程、应用不可见、Presentations 为空，并仅枚举该 PID 的唯一 PPTFrameClass 再核对 HWND/PID。创建自己的空白演示文稿时使用公共 Presentations.Add(WithWindow=msoFalse)，只在该应用内禁用宏和提示；退出时只将这个空白测试文稿标记为无需保存、关闭并正常 Quit。官方接口说明见 [Presentations.Add](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.presentations.add) 与 [Application.Visible](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.application.visible)。

隐藏只读实例读到 162 节点。通过已有不激活显示入口初始化后为 2 个根、1243 节点；Release、Debug 的正常撤销及控制器异常退出四组均通过，两个会话属性清除，外框/客户区/样式与完整原树精确恢复，cleanupFailures=0，测试实例正常退出。仓库入口证据为 `office-powerpoint-public-Release-r48` 和 `office-powerpoint-*-r49`；r47 私有原型不重复累计。

私有 r50 的后台 PrintWindow 图已查看：当前深色外壳左上三灯可见、快速工具栏右移、右侧原按钮消失，三色实心像素各 114；随后异常退出仍精确恢复。演示文稿没有打开文档视图，截图中的正文为空且编辑命令不可用；这不能声称幻灯片编辑器、放映、多文档或业务内容已适配。仍未执行物理按钮与真实主题切换，visualVerification=false。此版本的 PowerPoint 和 Word/Excel 一样没有注册到正式自动接管。

## WinUI 三像素差异仍未关闭

新增私有自有进程诊断，按固定顺序重放程序化 TextBox 编辑/撤销重做、不激活窗口的尺寸往返和宿主主题往返，然后只进行一次最终 RenderTargetBitmap 正文捕获。原生对照先于各隔离操作生成；没有反复采集匹配帧、容差、遮罩或裁剪。深浅色各两组原生对照、位置往返、配色恢复、额外源创建/关闭、同代码标题事务和包内正式模块事务的正文结果分别一致：深色 BMP SHA256 `1402018408EE0406E4383B6B03722E526BCCE76A9811E86E7E5C7A58B6DBE62F`，浅色 `2861AE08BD0F6820452981EB2A35EA549109133FEEA16C75A1A58CCAF8FF8F69`。

仅切换 presenter 的初始诊断没有恢复 ExtendsContentIntoTitleBar，故最终客户区几何检查失败，不能计通过；`r27` 的明确状态诊断确认最终 extends=1，客户区高度由 664 变为 709、屏幕原点 Y 由 205 变为 160。补全公共标题状态恢复的独立场景也得到上述相同正文。一次命令行数组传参错误被当成未知无操作模式，相关 r20 整组排除，不用于结论。首次背景诊断中的程序化 Focus 导致窗口激活，保护检查失败后已移除；一次 TextChanged 尚未派发便取状态的诊断失败也保留，后续在取撤销前状态之前等待固定 dispatcher 间隔。

RenderTargetBitmap 本身可能物化视觉对象；程序化编辑也不是物理输入。上述诊断没有复现原屏幕差异，不能解释或关闭 `verification-v44.md` 中三个像素每通道相差 1 的严格整窗截图失败，更不能替代实际应用视觉验收。

私有证据均在忽略的 `out/live-v45`：模块身份/旧偏移/导出映射为 office-identities-r7.json、office-offsets-r9.json、office-export-mappings-r11.json；只读实例为 owned-office-readonly-r15.json；私有原型为 word-readonly-r22、excel-readonly-r23；仓库正式研究目标为 office-guarded-Release-r25。WinUI 离屏证据为 raster-r8.json、raster-r21.json 及对应模式目录，原失败和被排除的尝试均未覆盖。软件库存、COM 实例信息与截图不进入 Git 或发布包。
