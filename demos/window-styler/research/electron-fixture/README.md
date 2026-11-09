# Electron 原标题栏隔离 fixture

此目录只含自有测试页面，不含 Electron、第三方应用程序或用户数据。准备一个放在 out 下的独立 Electron 运行时副本，使用 [Electron 官方 asar 工具](https://github.com/electron/asar) 将本目录打包到副本的 resources/app.asar。不得替换真实安装目录的资源或关闭完整性校验。品牌运行时有完整性保护时，使用官方运行时，或完整安装副本及独立用户目录。官方运行时与品牌运行时的 V8 修订不一致时，二者不能互相充当 ABI 验证记录。

运行副本时传入 --fixture-output=<独立绝对目录>，亮色加 --fixture-light，默认深色。输出目录须位于本地忽略的 out；fixture 将自己的 userData/sessionData 放在该目录。ready.json 返回 PID、HWND 和实际版本，status.json 返回原输入内容、控件几何、三灯状态和窗口事件。

将 HWND 交给 screenshot-all.ps1，启用 HostRefresh。运行目录须临时包含与当前构建对应的 StylerTests.exe、WindowAudit.exe 及适配器依赖。正式包不包含这些测试工具。检查完整截图、原右侧按钮禁用、最大化/还原及原位置恢复；须核对 captured=1，单看退出码不能证明支持。

fixture 读取输出目录的 command.json，每次命令带不同 id：

- {"id":1,"action":"click","label":"Maximize or restore window"}；另有 Minimize window 和 Close window。关闭测试只针对本 fixture。
- {"id":2,"action":"capabilities","close":false,"minimize":false,"maximize":false}：核验变灰和禁用，并用全 true 检查恢复启用。
- {"id":3,"action":"theme","value":"light"}：核验原应用主题 setter 跟踪；另有 dark。
- {"id":4,"action":"reload"}：核验 renderer 重载撤销。
- {"id":8,"action":"rebuild"}：替换整棵原始标题树，核验新树重新验证和接管。
- {"id":5,"action":"restore"}、{"id":6,"action":"resize","width":1100,"height":700}、{"id":7,"action":"close"}。

另需在三灯已附加时终止自有测试控制器，确认原工具栏位置、输入内容、原生按钮和 HWND 属性均恢复。只允许结束测试 worker，不得终止用户应用。fixture 通过不代表同版本所有应用布局、窗口状态或亮暗主题均已适配。
