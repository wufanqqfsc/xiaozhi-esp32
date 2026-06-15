# LVGL Patterns used in this project

The 6 patterns the compass UI is built from. Mirror them exactly — do not invent alternatives without a written reason in `doc/ai_compass_product_and_tech_spec.md`.

## §1 — Round-screen mask

The panel is 360x360 *rectangular*; the app presents a *circular* UI.

```cpp
auto screen = lv_screen_active();
lv_obj_t* round_mask = lv_obj_create(screen);
lv_obj_set_size(round_mask, 360, 360);
lv_obj_set_pos(round_mask, 0, 0);
lv_obj_set_style_radius(round_mask, 180, 0);
lv_obj_set_style_bg_color(round_mask, theme_colors.bg_outer, 0);
lv_obj_set_style_border_width(round_mask, 0, 0);
lv_obj_set_style_clip_corner(round_mask, true, 0);
lv_obj_move_background(round_mask);
```

All UI must lie within `r <= 178` from `(180,180)` to avoid the mask corners.

## §2 — Labels and overlays live on `screen`, not on `attitude_container_`

The `attitude_container_` is a child of `screen` that holds background / boundary arcs. **Any text label or small icon that must stay visible on top of the rotating arc/ring must be created on `screen` directly**, then brought to the top:

```cpp
lv_obj_t* label = lv_label_create(screen);
lv_obj_set_pos(label, cx - w/2, cy - h/2);
lv_obj_move_foreground(label);  // critical: else covered by arcs/rings
```

This is why `attitude_display.h` has `dir_n_label_` / `dir_e_label_` / `dir_s_label_` / `dir_w_label_` as members even though `attitude_container_` is a single child.

## §3 — `DisplayLockGuard` around every LVGL mutation

Multi-task (audio, network, MCP, rotation) UI crashes are the #1 bug class here. Wrap **every** LVGL call that runs off the LVGL task:

```cpp
void AttitudeDisplay::UpdateStateColor(int level) {
    DisplayLockGuard lock(this);   // RAII mutex
    if (layer3_progress_arc_) {
        lv_obj_set_style_arc_color(layer3_progress_arc_, state_color, LV_PART_INDICATOR);
    }
}
```

The lock is provided by `main/display/display.h`. The mutex is held by `LvglDisplay`. The `LvglDisplay::Lock/Unlock` virtual pair is exposed via `friend class DisplayLockGuard`.

## §4 — `AttitudeTheme` singleton for colors

```cpp
const auto& c = AttitudeTheme::GetInstance().GetColors();
lv_obj_set_style_bg_color(obj, c.bg_outer, 0);
lv_color_t state = AttitudeTheme::GetInstance().GetStateColor(level);  // 0..4
```

Theme switches propagate via `SwitchTheme(AttitudeThemeType)`, which sets the singleton and re-applies via `ApplyCurrentTheme()`. Two themes are defined: `THEME_AURORA` (light/minimal) and `THEME_TAIJI` (default, dark + gold).

## §5 — Auto-rotation task model

`CompassTaiji::StartAutoRotation(period_ms)` spawns a FreeRTOS task that ticks every 50 ms and calls `lv_image_set_rotation(canvas_, angle)` (units = 0.1°). Default period is **30 000 ms** (30s/圈).

For new animated layers:
- **Prefer** `lv_anim_t` (LVGL handles the tick) with `lv_anim_set_repeat_count(LV_ANIM_REPEAT_INFINITE)`.
- If you need a custom 20 Hz tick driving position updates, reuse the existing 50 ms task or add a `lv_timer_t` (LVGL-managed). **Do not** spawn a third FreeRTOS task without a documented reason.
- Step = `3600 / (period_ms / 50)`. For 30s/圈: 6 (0.6°/step). For 45s/圈: 4 (0.4°/step). For 60s/圈: 3 (0.3°/step).

### Fisheye icons: pseudo-rotation

WiFi and BLE status icons are *fixed at screen coordinates* (pos(162,126) and pos(162,198)) and only change **color** as state changes. The original design had them rotate with the taiji and was rejected because symbols went upside-down. See `doc/ai_compass_product_and_tech_spec.md` §2.2.3.

## §6 — Snapshot pipeline (USB-Serial/JTAG, base64 JPEG)

1. `SnapshotService::GetInstance().TakeSnapshot()` from any task.
2. `SnapshotService::CaptureAndEncode` calls `lv_snapshot_take` → JPEG encoder → base64.
3. Streamed on UART0 at 115200 between sentinels:
   ```
   ===SCREENSHOT_START===
   <base64>
   ===SCREENSHOT_END===
   ```
4. Decode on host with `scripts/snapshot_recv.py` (this skill).

Default cadence (from `main/main.cc`): 3 shots, 2s apart, starting 2s after boot.

## LVGL version & gotchas

- LVGL 9.5.0+ via idf component manager.
- `lv_image_set_rotation` uses 0.1° units, range 0..3600.
- `lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0)` for perfect circles.
- Animations: `lv_anim_set_exec_cb` for properties, `lv_anim_set_values` for from/to.
- `lv_obj_move_foreground` is cheap (just reorders a z-list). Call it after any property change that might reparent the object.
- Avoid `border_width` animation in tight loops (triggers layout reflow) — pulse `border_color` or `opa` instead.
