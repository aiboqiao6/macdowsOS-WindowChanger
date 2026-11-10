# v38 WinUI 初始化与关闭重入验证

日期：2026-11-11。研究模块修订 `5786259173ed`，研究引导修订 `6ef65ac3c8ca`。正式主运行时仍为 `0a931b9107b1`。使用原 GUI 线程、原生消息、真实键鼠输入和脚本截图，没有使用 computer use 或子代理。实际窗口截图、调试器调用栈、失败实验和私有汇总只留在忽略的 out 目录。

## 修复范围

延续 [v37](verification-v37.md) 的独立标题岛屿。公开 XAML 调用可以泵消息；初始化、查询、布局、主题和按钮能力更新期间，使用嵌套操作计数延后 WM_CLOSE / SC_CLOSE，保留原消息与参数。外部 Attach 的保护覆盖 Apply、最终根所有权确认和租约发布。嵌套 Detach 只能请求释放，不能在 Apply 栈上删除 frame 或提前返回成功。初始化期间替换原 Content 时终止附加并恢复，不覆盖宿主的新根。

宿主收到关闭请求前先恢复原生标题、布局和输入区域。恢复及 WinRT 引用释放期间继续保留 releasing 状态和消息拦截，使重入 Query 不访问正在关闭的 Content，关闭请求随后按原消息重新投递。恢复失败则保留会话和关闭请求，等待重试。宿主仍可取消关闭；此时窗口保持撤销后的原生状态，可再次接管。

不修改正式自动接管注册表，不增加软件黑白名单。独立研究仍要求精确 WinUI ABI、144 DPI、LTR、正常窗口、原生标题和唯一完整客户区岛屿。

## 最终构建和自有窗口检查

x64 Release、Debug 构建成功。八组检查全部通过，每种配置 320 项，总计 640 项。

| 配置 | 新增初始化故障 | 边界、焦点与标题属性 | 亮暗窗口回归 | 外部注入与生命周期 |
| --- | --- | --- | --- | --- |
| Release | 97/97 | 107/107 | 60/60 | 56/56 |
| Debug | 97/97 | 107/107 | 60/60 | 56/56 |

新增 test-native-island-initialization.ps1 在自有 fixture 的 WM_WINDOWPOSCHANGING 回调中、标题模式 setter 尚未返回且 owner 属性尚未发布时，同步触发五种故障：根替换、嵌套 Detach、WM_CLOSE、SC_CLOSE 和宿主取消一次 WM_CLOSE。检查回调返回前 HWND 仍存活、回调无异常、进程正常退出、旧属性清理、原生几何/标题恢复、临时标题岛屿移除、替换根保留及再次接管成功。嵌套撤销返回 0；嵌套撤销及取消关闭还要求完整窗口 PNG 恢复一致。关闭两轮必须以 fixture 退出码 0 判为通过，不能接受崩溃退出码。

沿用的 223 项检查覆盖亮暗主题、原内容输入焦点、126/125px 边界、宿主标题配色和选项更新、原按钮隐藏禁用、真鼠标三灯、最大化/还原、根替换、最小化中撤销、控制器正常/异常退出及应用内部主题切换。输入 fixture 是显式模板的 XAML Button，不代表文本编辑或输入法验证。

最终证据位于 out/live-v38/native-{initialization,boundaries,title,lease}-{Release,Debug}-final。私有 check-final-evidence.ps1 检查每组 result.json、checks.json 的精确项数和每项结果，以及当前 fixture/host 哈希；汇总为 final-evidence.json。

最终 fixture SHA256：

- Release：`EE6B2DA8070AB8791E35AE5CDED1DA7F359ADF8D8DCDD0E0B5E879052F018933`
- Debug：`B081D4FF42BB285EF74731CDD8C0248760547767006A79C61DDC43B930F38C4F`

最终研究 DLL SHA256：

- Release：`DB7FBB37139860BF94F8CF37A65A7BC5927170B9EB173EBDBB7BBC881F530AE0`
- Debug：`81CD299454CA223AB07FAA8694EA3BD8DCB00463546F3884BBEF4249366CDB09`

## 真实空白画图

Release/Debug 各执行正常退出和控制器异常退出，四轮全部通过。只启动和关闭自己创建、核验路径和空白标题的画图实例，没有关闭或重启用户已有应用，没有改变系统或应用主题。测试结束后无自有画图、fixture 或研究控制器残留。

每轮实际进程模块列表均确认加载本轮研究 DLL 和研究引导，并记录其哈希及 host 哈希。原外框不变，原客户区仅向上扩展；原正文完整 PNG 在接管中不变，撤销后完整窗口及正文 PNG 均与接管前逐字节一致。三灯各有 212 个精确色像素，旧按钮查询隐藏禁用，原 NC 三按钮输入无效。正常控制器退出码为 0，主动终止控制器轮为 -1，目标自动恢复。已查看接管及恢复截图，工具栏、画布和状态栏无重叠或位移。

证据位于 out/live-v36/paint-title-{Release,Debug}-v38-{normal,crash}-final。汇总同时要求所有预期字段存在且通过、changedTheme=false、退出码符合预期、完整 PNG 哈希一致、无错误文件，并核对实际加载模块；不以脚本退出码单独判定成功。DWM 20/33/38 读取成功且保持，34/35/36 返回 E_INVALIDARG，不计为颜色验证成功。实际画图仍只覆盖本机深色空白状态。

## 失败实验与定位

早期 native-initialization-Release-r1…r8 的根替换及嵌套撤销已通过，但关闭轮仍以 C000041D 崩溃，不能计为整组通过。私有调试器仅附加自己创建的 fixture，调用栈显示宿主 source.Close 泵消息期间，Query 经 NativeProcedure 调用 OwnsSource，读取正在关闭的 DesktopWindowXamlSource.Content 导致 Microsoft.UI.Xaml 内部访问冲突。aliveAfterCallback=1 已证明关闭请求延后生效；仅在 Apply 内推迟关闭仍不充分，必须在宿主关闭源之前先撤销并静默查询。

期间尝试 AppWindow.Closing(auto_revoke) 引入另一处访问冲突；本机投影不支持该对象的 weak_ref。改用手工 token 后消除了这处新增错误，但未解决原来的 Content 查询重入，最终移除该事件方案。无注入标题切换加同步关闭的原生对照也曾崩溃，但其隐藏启动条件与最终可见测试不同，不能据此把适配器缺陷归咎于框架。所有失败诊断保留在 out/live-v38。

关闭前撤销修复后的三场景 r9 为 61/61；补充 SC_CLOSE 和取消关闭、完善操作深度保护后，以最终构建重跑两配置五场景，各 97/97，再完成上述 640 项和四轮画图验证。没有屏蔽失败、修改截图基线或忽略差异。

## 交付与边界

新增脚本和本记录加入 VS 显式清单，项目与 filters 各 215 项，无缺失、重复、通配符或清单差异。四个 PowerShell 回归脚本语法解析及 git diff --check 通过。用户原有 controller.cpp 两处 Beta 标题修改和根目录 debug.log 不纳入本次提交。

正式包仍为 out/WindowChanger-v34-0a931b9107b1.zip，2,438,465 字节，SHA256 复核为 `CD602B4C9072A53AC17D8C953D8DCFBF2070C72ED10221F8E833358D81ABB029`。未将研究 DLL 放入正式包或默认自动接管，没有创建 GitHub Release。

全机软件覆盖、多岛屿、更多 ABI/DPI、跨屏、真实应用完整亮暗主题、文本编辑/IME 和宿主重建仍未完成。本轮也不涵盖 setter 内直接 DestroyWindow、任意线程强制销毁或框架调用失败的全面故障矩阵。allInstalledSoftwareVerified、bothThemesVerified、allWindowTypesVerified 仍为 false。
