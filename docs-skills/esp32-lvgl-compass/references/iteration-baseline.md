# Iteration Baseline (code, not spec)

> **Last rebaselined:** 2026-06-14 (initial capture).
> **Spec being audited:** `doc/ai_compass_product_and_tech_spec.md` v1.3 (2026-06-13).
>
> The spec's §13 audit claims most things are "已实现并真机验证". **They are not.** This document is the *code reality*. Update it whenever the code changes; the spec is then re-derived from it.

## Layer-by-layer status (360x360, 4-layer concentric layout)

| Layer | r | Spec claim | Code reality |
|---|---|---|---|
| 0 — Taiji (yin-yang) | 44 (was 72) | ✅ 30s/圈, 144x144 | ✅ present in `compass_taiji.cc` via `CompassTaiji::Create(attitude_container_, 180, 180, 44)`. Rotation period: **30 s/圈** (`StartAutoRotation(30000)`) — fixed 2026-06-14, see Hard rule 10 history. |
| 1 — 4 direction points | 72 | "北/东/南/西" 48x48 gold labels, r=158 | ❌ only **6x6 gold dots** via `CreateCompassPoints()` at r=72. No text labels are created or rendered. |
| 2 — 8 bagua names (乾兑离震巽坎艮坤) | 88 (spec) | ✅ 8 labels, 45s/圈 | ❌ **not implemented**. `CreateLayer1Bagua` does not exist. (Spec calls it "Bagua names at r=88"; see `attitude_display.cc` L142-147 for the comment trail.) |
| 2.5 — 8 trigram symbols (☰☱☲☳☴☵☶☷) | 115 (spec) | ✅ 8 canvases, 45s/圈 | ❌ not implemented. |
| 2.7 — 10 天干 (甲乙…) | 100 (Target.png) | n/a (removed) | ❌ explicitly removed in iter 20: `// ~~CreateLayer3Tiangan();~~` |
| 2.8 — 12 地支 (子丑寅卯…) | 128 (Target.png) | n/a (removed) | ❌ explicitly removed in iter 20: `// ~~CreateLayer2Dizhi();~~` |
| 2.9 — 28 星宿 | 170+ (Target.png) | n/a | ❌ not implemented. |
| 3 — Status progress arc | 90-144 | ✅ 5-state color | ✅ `CreateLayer3StatusProgress` exists, drives from `AttitudeTheme::GetStateColor(level)` via `UpdateStateColor(int level)`. |
| 4 — Outer boundary | 144-178 | ✅ 3px gold | ✅ `CreateLayer4Boundary` exists. |
| 4a — Direction text "北/东/南/西" | 158 (spec) | ✅ 60x60 gold | ❌ **NOT rendered**. Member pointers `dir_n_label_` etc. are declared in `attitude_display.h` but **never assigned** anywhere in the codebase. |

## Functionality

| Capability | Status | Evidence |
|---|---|---|
| LVGL 9.x on ST77916 | ✅ | `lvgl_display.cc` boots, `st77916: LCD panel create success` in log |
| Round-screen mask | ✅ | `attitude_display.cc:SetupUI` builds the 180-radius mask |
| Taiji auto-rotation | ✅ (30s) | `StartAutoRotation(30000)` |
| Bagua/trigram rotation | ❌ | not implemented |
| Theme switching (AURORA ↔ TAIJI) | ✅ | `attitude_theme.{h,cc}` + `SwitchTheme()` |
| Snapshot (USB-Serial/JTAG → JPEG) | ✅ | `main/display/snapshot/snapshot_service.*` works; `scripts/snapshot_recv.py` (in this skill) decodes |
| **Fisheye status icons (WiFi/BLE)** | ❌ | **zero matches for `Fisheye`, `fisheye`, `FISHEYE` in `main/`** |
| **AI Fortune engine (Idle/Animating/Result)** | ❌ | **zero matches for `Fortune`, `fortune`, `ShowFortune`, `EnterAnimatingState`, `EnterResultState` in `main/`** |
| **Result card 200x240** | ❌ | not implemented |
| **Highlight by color-pulse** | ❌ | not implemented |
| **Touch-to-close on result card** | ❌ | not implemented |
| WiFi event → fisheye | ❌ | not implemented |
| BLE event → fisheye | ❌ | not implemented |
| MCP `fortunes:*` tools | ❌ | server-side, not in this repo |

## Iteration numbers found in the code

`attitude_display.cc` and `compass_taiji.cc` carry comments like "迭代 13 / 18 / 19 / 20" indicating an in-house iteration counter. The current visible iteration appears to be **20** (most recent: removed 12 dizhi + 10 tiangan to follow Target2 style). Documenting here so the next maintainer doesn't re-add what iter 19/20 deliberately removed.

## Rejected designs (do not re-propose without strong reason)

From `doc/ai_compass_product_and_tech_spec.md` §7 ★已废弃方案:

- 8 always-on function icons around the bagua ring (visual density / overlap)
- Size-scale highlight (raced with 50 ms rotation tick → flicker)
- Taiji accelerating to 10s/圈 in Animating state (visual fatigue + CPU)
- Fisheye icons rotating with the taiji (symbols went upside down)
- TCA9554 IO-expander init (chip not present on 1.85B)

## What to do next (suggested, in priority order)

1. **P0** — Either commit to the `target.png` 5-layer (28星宿/天干/卦象/地支/太极) design, or pin down the simplified 3-layer (taiji + bagua + direction) design. The two are in tension.
2. ~~**P0** — Fix the 60s/圈 vs 30s/圈 mismatch.~~ **Done 2026-06-14**: now 30s/圈, synced across code + baseline + spec.
3. **P0** — Either implement or formally drop the "北/东/南/西" 60x60 gold text direction labels. Current state (6x6 dots) is below the spec's bar but matches no explicit decision.
4. **P1** — Implement the bagua (8 names) and trigram (8 symbols) rings, both at 45s/圈, on `screen` with `lv_obj_move_foreground()`.
5. **P1** — Implement the Fisheye status icons (stage 2 in the spec).
6. **P1** — Implement the AI Fortune three-state machine + 200x240 result card (stage 3 in the spec).
7. **P2** — Wire WiFi events to fisheye, then BLE.
8. **P2** — MCP `fortunes:*` tools on the server side.
9. **P3** — 节气/历史/IMU/手势 enhancements (stage 6 in the spec).

## How to use this file

When you start a new task, **read this first** to know what is actually on disk, then open the spec for the *design intent* behind the missing pieces. If the spec and this file disagree, this file wins until you re-baseline.
