---
name: esp32-lvgl-compass
description: |-
  Develop the AI Compass firmware on Waveshare ESP32-S3-Touch-LCD-1.85B (LVGL 9.5 on 360x360 circular QSPI ST77916, 8MB PSRAM / 16MB Flash, ESP-IDF v5.5+). Triggers: editing main/display/attitude_display.*, compass_taiji.*, snapshot/, the 1.85b board port, the I2C sensor stack (CST816S / QMI8658 / ES8311 / ES7210 / BQ27220 / PCF85363), the auto-rotation task, the USB-Serial JPEG snapshot pipeline, AttitudeTheme / DisplayLockGuard / lv_obj_move_foreground patterns, the concentric compass layout, or the AI Fortune engine (Idle/Animating/Result + 200x240 result card) in doc/ai_compass_product_and_tech_spec.md. Use for: adding concentric rings (taiji, 8 bagua, 8 trigrams, 12 tiangan, 12 dizhi, 28 constellations, 4 directions, 2 fisheye icons), wiring WiFi/BLE to fisheye, implementing ShowFortune / EnterAnimatingState / EnterResultState, flashing via build_and_flash.sh, capturing snapshots. Skip for upstream xiaozhi-esp32 audio / protocol / MCP server work.
---


# ESP32 LVGL Compass (AI 罗盘)

Project-local skill for the **AI Compass** firmware on **Waveshare ESP32-S3-Touch-LCD-1.85B**. Loads when you touch any of the compass-specific code paths, hardware bindings, or the AI Fortune engine.

## When this skill triggers

- Editing `main/display/attitude_display.*` or `main/display/compass_taiji.*`
- Touching the `waveshare/esp32-s3-touch-lcd-1.85b` board port (`main/boards/waveshare/esp32-s3-touch-lcd-1.85b/*`)
- Adding a new concentric ring, fisheye status icon, or compass point
- Wiring `esp_wifi` / NimBLE events to fisheye state updates
- Implementing or extending the AI Fortune engine (state machine, 200x240 result card, MCP tool)
- Building/flashing via `build_and_flash.sh`
- Capturing a JPEG snapshot over USB-Serial/JTAG for visual verification
- Reviewing or rebaselining the iteration status in `doc/ai_compass_product_and_tech_spec.md`

## What to read first

Before writing any code, open (in this order):

1. `references/hardware.md` — 1.85B pin map, I2C addresses, QSPI, audio, known pitfalls. **Read every time you wire a peripheral.**
2. `references/lvgl-patterns.md` — the 6 LVGL patterns this codebase actually uses (round mask, move-foreground, lock guard, theme, animation, auto-rotation task). **Mirror these exactly; do not invent new ones.**
3. `references/iteration-baseline.md` — what the code actually does today (vs. what `doc/ai_compass_product_and_tech_spec.md` v1.3 claims). **Treat the spec as aspirational; treat the baseline as truth.**
4. `doc/ai_compass_product_and_tech_spec.md` — full product + tech spec, design rationale, rejected alternatives, parameter tables. Read for design intent, **not** for status.

## Hard rules (project-specific, non-negotiable)

1. **360x360 circular screen.** Use `lv_obj_set_style_radius(180)` + `clip_corner=true` to mask the corners. All UI must fit in `r<=178` from `(180,180)`. See `references/lvgl-patterns.md` §1.
2. **PSRAM-first for large allocations.** `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` then fall back to internal. The 8MB PSRAM is there for the LVGL image buffer and the 144x144 taiji canvas.
3. **`DisplayLockGuard lock(this)` around every LVGL mutation.** Multi-task UI crashes are the #1 bug class in this project. See `references/lvgl-patterns.md` §3.
4. **Labels and overlays live on `lv_screen_active()`, not inside `attitude_container_`.** Always call `lv_obj_move_foreground()` after create. Otherwise the rotation arc / boundary ring covers them. See `references/lvgl-patterns.md` §2.
5. **Auto-rotation task ticks every 50ms.** `compass_taiji.cc` already runs a FreeRTOS task; when adding new animated layers, use `lv_anim_t` driven by `lv_timer_t` (20Hz), **do not** spawn another task unless you can prove the lock contention is safe.
6. **Fisheye icons are "pseudo-rotated" (fixed position, color pulses only).** Symbol stays upright; never let them follow the taiji rotation. See `references/lvgl-patterns.md` §5 and `doc/ai_compass_product_and_tech_spec.md` §2.2.3.
7. **No TCA9554 on the 1.85B variant.** Skip the IO-expander init that other waveshare boards do. The BSP has been patched: see `main/boards/waveshare/esp32-s3-touch-lcd-1.85b/esp32-s3-touch-lcd-1.85b.cc`.
8. **Snapshot output goes to USB-Serial/JTAG (UART0) at 115200.** Do not invent a new transport; the protocol is `===SCREENSHOT_START===` / `===SCREENSHOT_END===` with base64 JPEG between. Use `scripts/snapshot_recv.py` to decode.
9. **Build with the board override:** `python3 $IDF_PATH/tools/idf.py -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y build` or `./build_and_flash.sh all`. The `idf.py fullclean` is required when switching board type.
10. ~~**Default rotation period is 60s/圈, not 30s/圈.**~~ **Resolved 2026-06-14**: period is now 30s/圈 (`StartAutoRotation(30000)`), matches the spec. Step = 0.6° (6 × 0.1° units) per 50ms tick.

## Quick task recipes

### Add a new concentric ring (e.g. 12 地支)
1. Read `references/hardware.md` for available r-ranges and `references/lvgl-patterns.md` §4 for theme access.
2. Pick a free radius slot: 72 (taiji) / 88 (bagua name) / 115 (trigram) / 145 (unused) / 158 (direction) / 178 (outer border). Document the slot in the header.
3. Add a `CreateLayerNDizhi()` method on `AttitudeDisplay`, called from `SetupUI()` in radius order.
4. Add a `lv_timer_t*` if the ring rotates; reuse the 20Hz tick, do not multiply tasks.
5. If elements are labels/symbols: create on `screen`, not inside `attitude_container_`, and call `lv_obj_move_foreground()`.
6. Update `references/iteration-baseline.md` to mark the ring done.

### Add the AI Fortune engine (三态 + result card)
1. Read `doc/ai_compass_product_and_tech_spec.md` §2.2.3 and §5.3 (parameter tables) for the canonical design.
2. Add `FortuneState` enum + `fortune_state_` / `fortune_anim_timer_` / `fortune_result_timer_` / highlight indices to `attitude_display.h`.
3. Add `ShowFortune(gua, dir, func_label, gua_name, core, yi, ji)` to the public API.
4. Result card geometry is fixed at `FORTUNE_CARD_W=200, FORTUNE_CARD_H=240, pos=(80,60), radius=100`. Use the `0xFFD700` gold border.
5. **Highlight is color-pulse only** (gold↔white 300ms, 3 cycles). Do **not** scale or move the label — that races with the 50ms rotation tick and produces visible jitter.
6. Animating state: bagua accelerates to 15s/圈, taiji stays at 30s/圈 (matches `StartAutoRotation(30000)`).
7. Touching the result card must close it (`lv_obj_add_event_cb` with `LV_EVENT_CLICKED`).
8. **Sync the spec** — update `references/iteration-baseline.md` and `doc/ai_compass_product_and_tech_spec.md` §13.4 with the new audit row.

### Wire WiFi events to fisheye
1. In `application.cc`, register `WIFI_EVENT` / `IP_EVENT` handlers with `esp_event_handler_instance_register`.
2. From the handler, translate to `WifiStatus` (DISCONNECTED / CONNECTING / CONNECTED / ESPNOW) and call `attitude_display_->UpdateWifiFisheye(status)`.
3. Do not block the event loop; the lock will serialize. Do not call LVGL APIs without the `DisplayLockGuard`.
4. Ble mirrors the same pattern on `BLE_ADVERTISE_COMPLETE` / `BLE_CONNECT` / `BLE_DISCONNECT`.

### Verify on real hardware
```bash
# Build & flash in one go
./build_and_flash.sh all

# Capture a screenshot (in another terminal)
python3 docs-skills/esp32-lvgl-compass/scripts/snapshot_recv.py \
    -p /dev/cu.usbmodem101 -o screenshots/screenshot.jpg

# Watch logs
idf.py -p /dev/cu.usbmodem101 monitor
```

## What this skill does NOT cover

- Upstream xiaozhi audio pipeline (Opus / WebSocket / AEC) — see `main/application.cc` and `main/protocols/`
- Server-side MCP tool implementation (Python, not in this repo)
- Generic LVGL widget reference (the patterns doc only covers what this project uses)
- Other waveshare boards (only 1.85B is supported here)

## When the spec and code disagree

This happens often. Resolution order:

1. The code on disk (compile-clean is the bar)
2. This skill's `references/iteration-baseline.md` (re-baselined by humans)
3. `doc/ai_compass_product_and_tech_spec.md` (aspirational; sections 13.x are routinely out of date)

If you change code, update the baseline. If you change the spec, re-baseline. **Do not leave them in conflict.**

## Resources

### references/
- `hardware.md` — 1.85B pin map, I2C addresses, QSPI, audio, known wiring pitfalls
- `lvgl-patterns.md` — the 6 LVGL patterns this codebase actually uses
- `iteration-baseline.md` — what the code does today, layer by layer

### scripts/
- `snapshot_recv.py` — decode JPEG snapshots from USB-Serial/JTAG (115200 baud, base64 between sentinel markers)
