# C++ 六合一工具箱

将截图、录屏、倒计时、坦克风暴保活、LOL 自动接受/攻速导出和新写的 ACE 性能优化模块整合到一个原生 C++ Windows 程序。六项功能均在同一进程中运行，旧 ACE EXE 不再参与构建或运行。

## 运行

双击 `dist/CppToolbox.exe`。发行时只需复制这一个 EXE，无需 Python、.NET、外置语言包或 VC++ 运行库。适用于 Windows 10/11 x64，截图 OCR 和窗口录制需要系统对应组件。

左侧切换六个工具。关闭主窗口默认收进托盘，可点击左下角“关闭时”或托盘菜单切换为直接退出，选择会保存；最小化按钮始终收进托盘。“退出工具箱”始终真正退出，录制中退出会先停止编码、完成 MP4 并保存记录。

默认窗口已缩小至约 1003×731（100% DPI），仍可调整尺寸。录屏列表读取当前保存文件夹内的已有视频，每 2 秒同步新增和删除；切换保存位置立即更新。支持 MP4/MKV/AVI/MOV/WMV/WebM/M4V，不递归扫描子目录；正在录制的文件完成后才显示。已有录制记录保留时长，外部视频未知时长显示“—”。

新图标为蓝底白色工具箱与青色锁扣，包含 16–256 像素多尺寸。矢量源文件为 `assets/toolbox.svg`，运行 `scripts/make-icon.ps1` 可重新生成 ICO 和预览。

| 工具 | 使用方式 |
| --- | --- |
| 截图 | 点击“开始截图”，或使用当前状态栏显示的快捷键。默认尝试 Alt+A，被占用时自动尝试其他组合；可在页面更换。保留框选、标注、马赛克、贴图、OCR 和翻译。 |
| 录屏 | 选择屏幕、区域或窗口，设置帧率、编码器和音源后开始录制。保留录制历史、文件复制、内置播放及悬浮窗。快捷键默认 Ctrl+Alt+R，可在录屏页面设置。 |
| 倒计时 | 通过单选框切换倒计时或定时闹钟。倒计时输入天/小时/分/秒；闹钟使用 24 小时制时/分/秒，已过时间安排到明天。保留自定义提示音、结束弹窗和进度。 |
| 后台保活 | 首次需要点击开启。可暂停/继续、立即检测并保活，以及切换“跟随 QQ 游戏”。跟随模式在未检测到客户端时等待；统一开机启动后仍会恢复此设置。消息入队不等于游戏在线。 |
| LOL 助手 | 首次需要点击开启。保留自动接受开关、手动接受、攻速刷新间隔和 `D:\LOL.txt` 输出；自动接受偏好会保存。没有可写 D 盘时输出会失败。 |
| ACE 优化 | 点击“启用游戏优先优化”，按选择降低 ACE/SGuard 的 CPU、磁盘 I/O 和内存页优先级。每 3 秒发现新进程并重新检查；停止或正常退出时恢复本工具修改的原值。显示读回确认、CPU、I/O 速率、工作集和私有提交。 |

ACE 优化初始关闭，开启偏好会保存；它不会启动反作弊服务、暂停或结束 ACE，也不注入进程或改动反作弊文件。只处理已知组件名，且路径必须属于 Program Files 中的 AntiCheatExpert 安装目录，或与 ACE 服务注册的映像路径完全一致。

管理员身份不等于已启用 Windows 的进程调度访问权限。新版会在已提权时按作用域启用令牌中已有的 `SeDebugPrivilege`，退出时恢复原状态；只以查询/调度设置权限打开 ACE，不请求内存读写、终止或注入权限。拿到只读句柄后会定期重试可调整权限。受保护进程仍可能拒绝；错误码 5 如实显示原因。普通权限首次启用会请求 Windows 管理员重启，取消授权则保持关闭。

优化降低调度优先级，不能保证固定的磁盘速率上限。驱动发出的 I/O 可能不受用户态设置影响；进程 I/O 计数包含缓存等访问，不等于物理磁盘吞吐量。普通权限或受保护进程可能拒绝调整，界面会显示实际结果，可使用“以管理员权限重启”重试。即使管理员权限也不绕过系统保护。

内存优化将默认内存页优先级降至 BelowNormal（4），由 Windows 在内存压力下按需回收；不直接释放私有分配，不保证立即降低工作集。不定时强制清空工作集，也不设置硬内存上限，避免反复缺页和回读。工作集是驻留内存，私有提交不等于实际物理占用。

旧版独立工具不会被自动关闭，迁移后请从旧工具菜单退出，避免快捷键争用或重复处理。正常停止/退出会恢复优先级；强制终止或系统断电无法保证执行恢复，目标进程重启后恢复其默认设置。

## 配置与日志

统一目录：`%LOCALAPPDATA%\CppToolbox`。点击“配置与日志”可打开。

- `toolbox.ini`：后台模块开关、跟随 QQ、LOL 自动接受偏好。
- `screenshot/settings.json`：截图快捷键和翻译服务设置。
- `recording/settings.ini`、`history.tsv`：录屏配置与记录。
- `countdown/timer.ini`：可选倒计时配置；默认提示音会自动释放。
- `logs/`：主程序、截图和保活日志。
- `toolbox.ini` 的 `[ace]`：优化开关及 CPU、I/O、内存页优先级选项。
- `toolbox.ini` 的 `[general] close_action`：`tray`（默认）或 `exit`。

统一开机启动只管理当前用户 Windows Run 中的 `CppToolbox` 项，不改动原工具的启动项。LOL 的攻速刷新间隔保存在 `HKCU\Software\CppToolbox\League`，倒计时托盘偏好保存在 `HKCU\Software\CppToolbox\Countdown`。

原始工具与配置保持原样。工具箱首次运行使用新配置；如要沿用旧设置，可在关闭工具箱后将对应配置复制到上面相应目录。API Key 保存在本机截图配置中；OCR 在本地运行，翻译文本发送到所选在线服务。

## 构建与验证

安装 Visual Studio 的 C++ 桌面工具、Windows SDK 和 CMake，然后在项目根目录执行：

```powershell
.\build.ps1
.\tests\integration.ps1
```

构建脚本会复制源码到 `%LOCALAPPDATA%\CppToolboxBuild`，避开 NMake 对中文路径的兼容差异，以 Release 和静态运行库编译，运行六组核心测试，再生成 `dist/CppToolbox.exe`。已有实例运行时可先用 `-NoPublish` 编译验证，正常退出旧实例后再替换 EXE。

集成测试使用独立的 `CppToolbox/self-test` 目录，禁用全局快捷键、真实游戏访问、实际 ACE 调优和提醒音。检查单实例、六个同进程模块、嵌入窗口、六页导航、倒计时完成、实际 MP4 文件、后台重启、快速开关和打开设置对话框时正常退出。ACE 核心测试使用自己创建的测试进程验证低优先级、读回、恢复与析构恢复。

```powershell
.\tests\integration.ps1 -Preview
```

附加 `-Preview` 会短暂显示工具箱并保存六页 PNG，用于界面检查。测试报告位于 `%LOCALAPPDATA%\CppToolbox\self-test\integration-results.txt`。此测试不替代真实游戏、长时间录制、音频设备及多显示器高 DPI 的实际验收。

用户明确授权且真实 ACE 正在运行时，可手动执行 `.\tests\ace_ui_live.ps1`，验证工具箱界面调优及独立 API 读回、恢复。它不纳入自动 CTest，不调用游戏匹配 API。此次真实 LOL 测试中，两个 SGuard 的 CPU、I/O、内存页降级和恢复全部成功；没有新增 I/O，工作集合计约 44 MB 且未立即下降，不据此声称游戏卡顿改善。详见 `docs/verification.md`。

## 源码结构

- `src/main.cpp`：主界面、导航、统一托盘、启动设置和集成测试入口。
- `src/gateway.*`：模块线程、消息命令、状态和通知代理。
- `src/*_module.cpp`：五个原工具模块的适配器。
- `src/modules/`：从原始工具导入并适配的 C++ 源码。
- `src/ace.*`、`src/ace_governor.*`：原生 ACE 监测、调度降级、验证和恢复。
- `assets/`：图标、内置语言和提示音。
- `工具/`：用户提供的原始项目，保留供对照。

六个源码模块与主窗口属于同一个进程，各使用自己的消息线程。原始项目目录保留供对照；`工具/ACE` 中的旧二进制不被编译、嵌入或启动。

实现参考：[微软 SetPriorityClass 文档](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass)、[PHNT 的 ProcessIoPriority 定义](https://github.com/winsiderss/phnt/blob/master/ntpsapi.h)。I/O 优先级使用运行时解析的 Windows Native API；不支持或拒绝访问时明确报告失败，不宣称成功。

内存策略参考：[微软 SetProcessInformation 文档](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation)及[工作集与缺页说明](https://learn.microsoft.com/en-us/windows/win32/memory/working-set)。
