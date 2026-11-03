# v32：按能力自动接管，不设软件黑白名单

2026-11-03。沿用 v31 的精简 UI、托盘、单实例和当前用户登录自启。本轮不使用 computer use，不关闭或重启用户应用，不注销或重启系统。

## 行为

首次使用默认开启“自动接管所有支持窗口”，已保存的明确暂停选择仍保留。候选条件只看实际支持能力和是否已拥有，不查询软件名称、测试记录、软件黑白名单或逐窗口排除表。

移除固定初始化等待；启动和重新启用自动模式时立即扫描，单批最多两个附加操作，消息队列立即续批。后来出现的窗口在正常发现扫描中尝试；隐藏/最小化窗口在显示后尝试。临时失败按 5 秒起步、最长 60 秒退避重试，不永久封禁。

“还原选中”和“全部还原”暂停全局自动接管，避免立即重新注入；重新开启后所有支持窗口重新参与。生产模式不接受测试用的 `--scope-pid` 限制。保留框架 ABI、原布局、所有权及失败回滚检查：软件未单独测试不构成跳过理由，但未知 ABI/不匹配原树也不被强制写入。

## 本机验证

- x64/x86、Debug/Release 四配置编译成功。
- 设置/调度纯契约各 **23/23**，覆盖无测试软件清单、立即就绪、失败退避、无永久排除、显示恢复、PID/HWND 复用和持久化。
- 四配置及完整包 x64 控制器接管 x86 窗口的生命周期各 **46/46**，共 **230/230**。自有窗口每次生成全新类名和软件标题，不带 Sample 标记；验证默认自动接管、暂停/恢复、手动还原全局暂停、单实例、托盘注册/唤回/重建、计划任务实际执行、重复启动及控制器异常退出恢复。计划任务为隔离测试任务，没有声称做过真实重启登录测试。
- 全量 CTest：x64 Release/Debug 各 **13/13**，x86 Release/Debug 各 **9/9**，本轮首轮全部通过，未修改原断言。
- 测试前后现存 MFC/BCG、Office NetUI、TXGui 原窗口存活，所有权和几何状态保持 **3/3**；本轮未重新对这三类窗口执行附加/输入测试，既有有限 profile 范围见 [v30](research/frameworks-v30.md) 和 [v31](verification-v31.md)。
- 用户批准暂时暂停旧控制器后才执行验证；正式运行验证开始时已无旧版控制器，未终止旧进程。包装验证器在 `finally` 中恢复并回读原自动接管设置，隔离任务/测试设置清理通过；用户真实自启设置保持不变。
- 主目录/SDK **59** 项源码格式和变更 PowerShell 语法检查通过；公开 VS 工程及 filters 各 **178** 个匹配的显式文件项，不使用通配符。
- 完整运行包 `out/WindowChanger-v32.zip` 解压回读 **50** 项 SHA-256，零差异；包括 x64/x86 CRT，不含测试工具。CI 同步生成 `WindowChanger-v32` artifact。

本机证据：`out/live-v32/auto-all-final/result.json`、`existing-restored.json`、各配置 `native.log`，`out/vs-auto-*/ctest-v32-*.log` 和 `out/live-v32/package-integrity.json`。真实窗口库存及用户应用截图不上传公开仓库。

## 复验

```powershell
.\build.ps1 -Architecture x64 -Configuration Release -Test
.\build.ps1 -Architecture x64 -Configuration Debug -Test
.\build.ps1 -Architecture Win32 -Configuration Release -Test
.\build.ps1 -Architecture Win32 -Configuration Debug -Test
.\demos\window-styler\package-original-frameworks.ps1
# 管理员 PowerShell；仅操作自有窗口和隔离任务
.\demos\window-styler\research\verify-auto-all.ps1 -BuildPrefix vs
```

本轮为避免覆盖旧版可能仍在运行的可执行文件，构建到独立的 `out/vs-auto-x64` 和 `out/vs-auto-Win32`，打包时使用 `-X64BuildDirectory` / `-X86BuildDirectory` 指定它们；未改动旧包路径。
