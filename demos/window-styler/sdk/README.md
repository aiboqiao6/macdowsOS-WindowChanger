# 通用进程内后端 SDK v1

`../adapter-api.h` 定义固定大小的 C ABI。控制器只负责架构/权限、GUI 线程注入、生命周期和回滚；后端负责对应渲染能力的原控件树。默认分派不读取程序名、路径或产品版本，不以报告中的 Framework 名称决定选择。

```ini
[Adapters]
Ids=300,301
[Adapter300]
Name=Semantic command tree
AnyWindow=1
ModuleExports=ToolkitGetRoot,ToolkitGetVersion
File=ToolkitBackend.dll
[Adapter301]
Name=Child HWND tree
ChildClass=ToolkitContentBridge
RequiredModules=ToolkitRuntime.dll
File=ToolkitTreeBackend.dll
```

ID 为 256–65535，255 是 Automatic。可用 Class、ClassPrefix、ChildClass、RequiredProperty 约束结构；AnyWindow=1 必须同时带运行时能力约束。RequiredModules 为 AND，ModulesAny 为 OR，ModuleExports 要求同一个已加载模块导出所列全部符号。File 只能是同目录 DLL。QtVersion 在加载 Qt 插件前检查已加载 qVersion，不能为探测加载缺失的框架运行时。

Automatic 在目标 GUI 线程依注册顺序检查结构和运行时，再调用只读 `MacdowsAdapterProbeV1`。Probe 拒绝后尝试下一个；返回 ERROR_RETRY 时由控制器限时重试，不能阻塞目标消息循环。首个成功 Probe 决定后端。Attach 失败必须回滚并终止本次选择；禁止在部分修改的树上继续尝试其他后端。所有后端拒绝时保持窗口原样，不退回覆盖层或额外标题行。Framework 字段保留作描述，不参与路由。

导出 `MacdowsAdapterProbeV1` 和 `MacdowsAdapterAttachV1`，支持 x86 stdcall 装饰名。后端必须与目标位数、已验证的运行时 ABI 相符。版本约束针对框架 ABI；不能用应用名称、私有业务控件名或某个宿主的对象偏移代替通用布局发现。

布局发现应使用原按钮的系统命令、可访问语义、共同容器和顶部几何，确认唯一可复用行后再修改。优先复用原事件或在原树插入控件，用原布局腾出空间；保存属性值、绑定、可见性及原方法描述符。禁止新建顶层绘制/遮罩窗口、窗口跟随、定位定时器和额外标题带。

成功设置 OwnerProperty=owner、AdapterProperty=实际 adapterId，InlineProperty 标记内嵌类型。异步布局完成后 QueryMessage 才返回 Acknowledged；DLL 已加载或 Probe 成功不能代替绘制确认。DetachMessage 只接受同一 owner，恢复后返回 Acknowledged；嵌套命令或尚未完成还原返回 TransitionBusy。DiagnosticMessage 只返回标量，不跨进程传递 C++ 指针。

在正确 GUI 线程安装和移除回调。监测 owner HWND/进程和原树销毁，所有者退出时恢复；生命周期计时器不用于窗口定位。操作尊重原窗口的禁用状态。恢复后 DLL 可能仍被系统固定映射，不能覆盖已加载版本，构建使用内容哈希文件名。

`sample-caption-adapter.cpp` 是协议测试示例，只接受自有测试窗口，未注册到正式分派。`reject-probe-adapter.cpp` 验证拒绝后继续选择的行为。实际后端见 `qt-inline.cpp`、`wpf-inline.cs`、`electron-layout.js`、`xaml-inline.cpp`、`netui-inline.cpp`。

```powershell
cmake -S sdk -B sdk-build -A x64
cmake --build sdk-build --config Release
```

每个后端仍须按实际 HWND 验证首次画面、稳定画面、最大化/还原、按钮输入和撤销，并检查客户区右侧/底部。通用协议并不意味着所有未知渲染树都已覆盖。
