# 验证记录 · 2026-10-21

## 构建与核心测试

MSVC 19.51、Windows SDK 10.0.26100、C++20、Release、静态运行库。`build.ps1` 构建并运行六组 CTest：

- AceGovernorTests：目标名称与安装路径边界、真实测试子进程 CPU/I/O/内存页优先级降级、读回确认、显式恢复与析构恢复。
- KeepAliveTests：页面筛选与后台消息。
- StartupTests：启动项隔离与模式解析。
- AttackSpeedTests：攻速解析/输出协议与间隔。
- HotkeyTests：热键去重与按住不重复触发。
- FolderVideoTests：实际文件夹中发现无历史记录的视频、扩展名大小写、排除非视频和子目录、历史时长保留、外部删除和目录切换。

## 工具箱集成

`tests/integration.ps1 -Preview`：六个源码模块同进程初始化、录屏/倒计时嵌入、六页导航、单实例、旧 ACE 资源不存在、默认关闭到托盘、直接退出偏好保存和实际退出、两秒倒计时、新 MP4、后台重启、快速开关恢复及打开设置对话框时正常退出。

测试不注册全局快捷键、不访问真实游戏 API、不调整真实 ACE 优先级。预览通过 PrintWindow 只保存工具箱自身窗口。

## 用户授权的真实 LOL / ACE 测试

原生 `AceLiveProbe --apply` 与 `tests/ace_ui_live.ps1` 均完成。后者实际操作发行 EXE 的 ACE 页面，通过独立的 Win32/Native API 再次查询结果。

| 进程 | PID | 调整前 | 调整后读回 | 停止后 |
| --- | --- | --- | --- | --- |
| SGuardSvc64.exe | 29948 | CPU Normal / I/O 2 | CPU BelowNormal / I/O 1 | Normal / I/O 2 |
| SGuard64.exe | 46888 | CPU Normal / I/O 2 | CPU BelowNormal / I/O 1 | Normal / I/O 2 |

全部恢复成功，权限/设置错误为零。调优前 3 秒与调优后 5 秒样本中，两进程新增读写字节均为 0；因此只能确认调度设置生效及可恢复，不能据此声称扫盘下降或游戏帧时间改善。原始控制台记录保存在项目根目录 `ace-live-test.txt` 与 `ace-ui-live-test.txt`。

后续内存策略实测：两个进程的内存页优先级由 Normal（5）降到 BelowNormal（4），独立 API 读回及恢复为 5 均通过。SGuardSvc64 工作集约 11.16 MB、私有提交约 2.85 MB；SGuard64 工作集约 33.02 MB、私有提交约 31.14 MB。短测无立即工作集下降；系统内存占用约 95–97%，不能归因于这两个合计约 44 MB 的 ACE 进程。没有进行强制工作集清空或内存硬限额。

## 后续问题修复验证

- 窗口缩小约 15% 到 1003×731，六页截图检查无控件裁切；倒计时保持其原生画布尺寸。
- 截图快捷键原来在模块线程中以主线程 HWND 为弹出菜单 owner，已将弹出菜单移到主线程，选项以消息交给模块注册；自检确认原生菜单真正打开、选择到达模块。被占用时恢复旧组合并提示。
- 用户管理员实例 PID76040 的 SeDebugPrivilege 已分配但未启用，而成功测试进程启用了该权限；据此加入作用域权限启用/恢复及只读句柄重试。用 `ace_ui_live.ps1 -RestrictedLaunch` 故意从未启用 SeDebugPrivilege 的管理员父令牌启动新版，两个真实 SGuard 的三项调度设置与恢复均成功，经独立 API 复核。
- 保活旧测试曾因用户同时移动真实鼠标出现一次“real pointer moved”，相同未改动测试随即单独及整组重跑通过；未修改或屏蔽该断言。

## 尚未验证

真实 ACE 持续扫盘场景的物理磁盘延迟与游戏帧时间、长时间录制及音频设备组合、多屏混合 DPI、所有系统/ACE 版本。I/O 降优先级不是硬限速，内核驱动 I/O 未必遵循进程设置。
