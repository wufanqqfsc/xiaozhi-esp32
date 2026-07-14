# JARVIS 设备问题案例库

> 蒸馏自 2026-07-13~14 真机调试（设备 192.168.0.152，COM9，板型 1.85b）

## 案例 1：联网后反复重启（LoadProhibited）

**现象**

- WiFi 连接成功后约 +11s，`Guru Meditation Error: Core 0 panic'ed (LoadProhibited)`
- `EXCVADDR: 0x00005e84`，`rst:0xc` 循环重启
- 崩溃前大量 `AudioService: Input resample output_samples` 警告（与崩溃无直接关系）

**Backtrace（解码后）**

```
ReleaseIdleResources → DestroyUI → lv_obj_del
  ← ReturnToCompassIdleViewUnlocked
  ← Application::HandleStateChangedEvent (idle)
```

**根因**

1. `FortuneWatchfaceView` 构造函数中无锁调用 `CreateUI()`（`GetInstance()` 经 `IsJarvisHudActive()` 间接触发）
2. 进入 `idle` 时 `ReturnToCompassIdleView()` 无条件 `ReleaseIdleResources()` 销毁该 UI
3. `lv_obj_del` 时对象树已损坏 → `event_mark_deleting` LoadProhibited

**修复**

- 构造函数仅分配非 LVGL 资源；`CreateUI()` 延迟到 `ShowUnlocked()`
- `IsJarvisWatchfaceVisible()` 只读 `fortune_watchface_visible_`，不触发 `GetInstance()`
- `ReleaseIdleResourcesUnlocked()` + 调用方已持锁时用 Unlocked 版本

---

## 案例 2：JARVIS 界面卡住不动（动画停）

**现象**

- JARVIS HUD 可见，扫描弧/轨道点/金色文字不动
- 无 Guru，设备不重启

**根因 A：定时器未恢复**

- `Hide()` / idle 清理会 `lv_timer_pause`
- `ShowJarvisWatchface()` 在 `fortune_watchface_visible_==true` 时直接 `return`，未 `lv_timer_resume`

**根因 B：LVGL 任务饿死（更严重）**

- 全屏 360×360 ARGB canvas：每 33ms `memset(518KB)` + `lv_canvas_set_buffer`
- `ShowUnlocked()` 在 `DisplayLockGuard` 内同步调用 `UpdateAnimation()`
- 主线程长时间持锁 → LVGL 任务无法 `lv_timer_handler()` → 全界面冻结

**修复**

- `EnsureAnimatingUnlocked()`：已显示时仍 resume timer + 确保 active screen
- 轨道动画改用 12 个 `lv_obj` 圆点，去掉全屏 canvas
- 移除 `ShowUnlocked`/`EnsureAnimating` 中的同步 `UpdateAnimation()`
- 定时器周期 50ms

---

## 案例 3：轨道点从不显示

**现象**

- 扫描弧正常，轨道区域空白

**根因**

- canvas 仅 32×32 置于屏幕中心，轨道半径 ~122px，绘制坐标全部落在 canvas 外

**教训**

- 勿用小 canvas 画大半径轨道；优先用少量 `lv_obj` 更新位置，或 canvas 必须覆盖轨道包围盒

---

## 案例 4：addr2line 解码失败

**现象**

```
ELF file is missing or has changed, the build folder was probably modified.
```

**处理**

1. `.\build_and_flash.ps1` 重新编译烧录
2. 用新 monitor 崩溃日志中的 Backtrace 地址
3. 对应当前 `build/xiaozhi.elf` 解码

monitor 内嵌的 addr2line 也会因 ELF 不匹配失败，需手动用完整路径工具。

---

## 案例 5：烧录失败 COM 口占用

**现象**

```
Could not open COM9, the port is busy or doesn't exist
PermissionError(13, 'Access is denied.')
```

**处理**

结束所有 `idf_monitor` / `idf.py -p COM9 monitor` 的 python 进程后再烧录（见 SKILL.md 释放 COM 口命令）。

---

## 调试检查清单

```
[ ] 已结束占用 COM 口的 monitor
[ ] build_and_flash.ps1 编译烧录成功
[ ] monitor 已启动并复现问题
[ ] 记录崩溃前 5 秒日志（含状态机、JARVIS 相关 TAG）
[ ] ELF 与固件 checksum 一致后解码 backtrace
[ ] 修复后验证：idle 不重启 + JARVIS 动画 + 状态栏 + 回罗盘
```
