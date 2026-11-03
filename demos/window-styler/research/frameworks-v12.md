# v12：Chromium Views 框架入口

生产实现见 `../views-inline.cpp`，验证范围见 `../compatibility-v12-2026-11-01.md`。本轮不依赖公开 PDB：当前运行版本的符号请求仍未取得。

通过上游 Chromium 源码、只读反汇编、View metadata 的 X/Y/Width/Height 访问器及实际对象树，交叉验证框架对象字段。模块由 HWNDMessageHandler 虚表所在映像获得，验证 RSDS CodeView 和函数指令，不按模块或应用文件名查找。

当前 profile：CodeView `8DE675E4-DC25-455F-4C4C-44205044422E` age 1。`View::SetBoundsRect` RVA `0x106fa68`，原布局驱动 `0x1092060`，View OnPaint 虚表槽 `0x328`。框架 View Rect `+0x1c8`、parent `+0x198`、children `+0x1a0`、存活标记 `+0x17c`。这些仅对该身份成立，不能用于另一修订的模块。

按钮按 WM_NCHITTEST 标准语义和共同父容器识别；同组隐藏的最大化/还原替换按钮也接管。顶部内容按几何发现，不使用 BrowserFrameView 或应用按钮类名选择。保存原 View 几何与绘制虚表，绘制进入原 gfx::Canvas 的 cc::PaintCanvas，通过 save / clipRRect / drawColor / restore 绘制三圆点，保留原输入路径。

只有在完整原布局之外变换几何才安全。早期只 Hook SetBoundsRect 的方案会污染宿主的下一步布局计算，最大化后标签消失，撤销时也可能保存已变换的按钮位置。正式后端在最外层布局入口先恢复几何，记录原请求，退出后统一变换；撤销后运行原框架布局。早期失败截图仍在 out/live-v12 中，不作验证通过证据。

线程内对象表和实例虚表按生命周期监控。绘制回调发现原 View 离开原树时调用原 OnPaint；撤销时若 View 已移到另一个 Widget，仅恢复绘制虚表，保持新父级几何。回调可能仍在框架栈中，退休条目与 DLL 保留到目标 GUI 线程/进程退出，不强制卸载。

`views-read.py` 为只读研究工具。`views-probe/`、`test-views-live.ps1` 是早期实验，存在已记录的布局反馈与重复线程 Hook 问题，仅保留研究来源；请使用正式后端和 `test-views-inline.ps1` 复验。

引导研究：外部 SetWindowsHookEx 在新 HWND、新模块名及不同磁盘目录下也出现 126；同进程短期注册探针成功。正式实现采用目标内注册再同步发送原附加消息，工作线程不修改控件树。hook-registration.cpp 仅注册/撤销转发 Hook，未改窗口样式或几何；结果在 out/live-v12/internal-hook-probe.json。源码来源路径复用按完整文件内容核验；这项防御本身不能解决已复现的 126。
