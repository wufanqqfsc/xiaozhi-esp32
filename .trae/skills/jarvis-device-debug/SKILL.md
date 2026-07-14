---
name: jarvis-device-debug
description: >-
  Xiaozhi ESP32 JARVIS HUD build/flash/monitor workflow and crash/freeze root-cause
  playbook. Invoke when user reports JARVIS view freeze, reboot loop after WiFi,
  LVGL panic, serial monitor analysis, or asks to debug FortuneWatchfaceView on device.
---

# JARVIS 设备问题调试

小智 ESP32（waveshare esp32-s3-touch-lcd-1.85b）上 JARVIS HUD 的真机编译、烧录、串口分析与修复流程。

## 标准工作流

用户报告 JARVIS 设备问题时，按顺序执行：

```
1. 释放串口 → 2. 编译烧录 → 3. 开 monitor → 4. 复现并抓日志 → 5. 解码 backtrace → 6. 定位代码 → 7. 修复 → 8. 再烧录验证
```

### 1. 编译与烧录（Windows）

**必须使用**项目根目录脚本，不要用 `idf.py` 裸命令，不要用 `2>&1` 重定向（会丢错误信息）。

```powershell
# 项目根目录
.\build_and_flash.ps1 -Port COM9

# 仅编译
.\build_and_flash.ps1 -BuildOnly
```

默认板型：`waveshare/esp32-s3-touch-lcd-1.85b`。未指定 `-Port` 时脚本自动检测 `VID_303A` 的 COM 口。

代码修改完成后：**必须**编译 + 烧录 + monitor 验证，直到用户确认或日志无 Guru/重启。

### 2. 释放 COM 口（烧录前）

monitor 占用会导致 `PermissionError: Access is denied`：

```powershell
Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
  Where-Object { $_.CommandLine -match 'COM9|idf_monitor|idf\.py.*monitor' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
```

### 3. 串口 Monitor

```powershell
$idf = "C:\workspace\tools\esp-idf"
$python = "C:\Users\sfan\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe"
& $python "$idf\tools\idf.py" -p COM9 monitor
```

关注项（按时间线 `+X.XXXs` 阅读）：

| 信号 | 含义 |
|------|------|
| `Network connected` 后 ~10s 内 `Guru Meditation` | 常见 idle 路径 LVGL 崩溃 |
| `rst:0xc (RTC_SW_CPU_RST)` 循环 | 软件崩溃重启环 |
| `FortuneWatchfaceView: Show: LVGL lock timeout` | 锁竞争，UI 可能未完整显示 |
| `ReturnToCompassIdleView` 后立即 panic | `ReleaseIdleResources` 销毁 UI 问题 |
| 无 Guru 但 JARVIS 画面静止 | 动画定时器未 resume 或 LVGL 任务被饿死 |

日志中的 `ELF file SHA256` 必须与当前 `build/xiaozhi.elf` 一致，否则 backtrace 解码无效。

### 4. Backtrace 解码

先 **重新编译**（确保 ELF 与设备固件一致），再解码：

```powershell
& "C:\Users\sfan\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin\xtensa-esp32s3-elf-addr2line.exe" `
  -pfiaC -e "C:\workspace\xiaozhi-esp32\build\xiaozhi.elf" `
  0x4211a707 0x4210aff2 0x42218d93 ...  # 从 monitor Backtrace 行复制地址
```

若提示 `ELF file is missing or has changed`：重新 `.\build_and_flash.ps1` 后再解码。

### 5. HTTP 辅助触发 JARVIS（设备已联网）

```bash
curl -X POST http://<设备IP>:8080/api/debug/jarvis/show
curl -X POST http://<设备IP>:8080/api/debug/jarvis/hide
```

设备 IP 可从 monitor 的 `Network connected` 或 `curl http://<IP>:8080/api/device/status` 获取。

## 关键代码路径

| 场景 | 入口 | 核心文件 |
|------|------|----------|
| 语音唤醒显示 JARVIS | `HandleWakeWordDetectedEvent` → `ShowJarvisWatchface` | `application.cc`, `attitude_display.cc` |
| 进入 idle 清理 UI | `HandleStateChangedEvent(idle)` → `ReturnToCompassIdleView` | `application.cc`, `attitude_display.cc` |
| JARVIS 动画 | `FortuneWatchfaceView::OnTimer` → `UpdateAnimation` | `fortune_watchface_view.cc` |
| 状态栏消息路由 | `IsJarvisHudActive` → `RouteToJarvisStatusBar` | `attitude_display.cc` |

### LVGL 锁规则（高频踩坑）

- `DisplayLockGuard` 与 `lvgl_port_lock` 是**同一把递归互斥锁**（`xSemaphoreCreateRecursiveMutex`）。
- 已持 `DisplayLockGuard` 时，必须调用 `*Unlocked()` 变体，**禁止**再调带锁的 `Show()`/`Hide()`/`ReleaseIdleResources()`。
- `UpdateAnimation()` 只能在 **LVGL 定时器回调**（`lv_timer_handler` 内）运行；**禁止**在 `ShowUnlocked()` 持锁期间同步调用。

### JARVIS UI 生命周期约定

- UI **懒加载**：`FortuneWatchfaceView` 构造函数不创建 LVGL 对象；`ShowUnlocked()` 时 `CreateUI()` + `EnsureTimer()` + `lv_timer_resume`。
- `ReleaseIdleResourcesUnlocked()`：idle 时销毁 overlay；下次 `Show` 重建。
- `fortune_watchface_visible_`（AttitudeDisplay）与 `visible_`（FortuneWatchfaceView）须同步；`ShowUnlocked` 失败时**不要**置 `fortune_watchface_visible_=true`。
- `ShowJarvisWatchface` 已显示时须调 `EnsureAnimatingUnlocked()`（恢复 pause 的 timer），不能 early-return 什么都不做。

## 已知问题模式（速查）

详细案例见 [incidents.md](incidents.md)。

| 症状 | 根因 | 修复方向 |
|------|------|----------|
| 联网后 ~11s 反复重启 `LoadProhibited` in `lv_obj_del` | 构造函数无锁 `CreateUI`，idle 时 `ReleaseIdleResources` 销毁损坏对象树 | 懒加载 UI；`ReleaseIdleResourcesUnlocked` |
| JARVIS 显示但动画全停 | ① timer 被 pause 后 `ShowJarvisWatchface` early-return ② 全屏 canvas 每帧 memset 518KB 饿死 LVGL ③ 持锁同步 `UpdateAnimation` | `EnsureAnimatingUnlocked`；轨道改用 `lv_obj` 圆点；定时器驱动动画 |
| 状态栏有字但弧不转 | 仅 `status_mode_=VoiceActive`，动画层仍应运行；查 timer 是否 resume | 见 `EnsureAnimatingUnlocked` |
| `Show: LVGL lock timeout` | 非递归场景下嵌套加锁或长临界区 | 统一 `*Unlocked()` API |
| 轨道点不显示 | canvas 尺寸远小于轨道半径（曾用 32×32，半径 ~122px） | 用全屏坐标的小圆点 `lv_obj`，勿用小 canvas |

## 分析报告模板

完成分析后，用此结构回复用户：

```markdown
## 现象
[用户操作 + 时间点 + monitor 关键行]

## 根因
[调用链 + 为何触发]

## 修复
[改了什么文件、原则是什么]

## 验证
- [ ] 联网进 idle 不重启
- [ ] 唤醒 JARVIS 动画流畅（扫描弧/轨道点/金色闪烁）
- [ ] 状态栏消息正常滚动
- [ ] 结束会话回罗盘正常
```

## 相关文档与 Skill

- `issues/2026-07-13-jarvis-wake-reboot-lvgl-lock.md` — LVGL 锁竞争
- `issues/2026-07-13-jarvis-hud-status-bar-voice-display.md` — 状态栏路由
- `.trae/skills/esp32-http-api/SKILL.md` — 设备 HTTP API
- `.trae/skills/xiaozhi-code-wiki/SKILL.md` — 项目架构
- `.trae/skills/jarvis-e2e-auto-analyzer/SKILL.md` — 语音 E2E 自动分析
