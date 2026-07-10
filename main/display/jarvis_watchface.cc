#include "jarvis_watchface.h"
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <esp_random.h>

#define TAG "JarvisWatchface"

namespace {

constexpr int W = 360;
constexpr int H = 360;
constexpr int CX = W / 2;
constexpr int CY = H / 2;

// RGB565 helpers
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

inline uint16_t hsvToRgb565(int hue, float sat, float val, float alpha) {
    float h = hue / 360.0f;
    float s = sat;
    float v = val;

    int i = static_cast<int>(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    float r, g, b;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }

    uint8_t cr = static_cast<uint8_t>(r * 255 * alpha);
    uint8_t cg = static_cast<uint8_t>(g * 255 * alpha);
    uint8_t cb = static_cast<uint8_t>(b * 255 * alpha);
    return rgb565(cr, cg, cb);
}

// Set pixel directly (no blend)
inline void SetPixelRGB565(uint16_t* buf, int x, int y, uint16_t color, int stride) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    buf[y * stride + x] = color;
}

// Draw filled circle with glow
void DrawGlowCircle(uint16_t* buf, int cx, int cy, int radius, int hue,
                    float intensity, int stride) {
    if (intensity <= 0.01f) return;

    int glowRadius = static_cast<int>(radius * 1.5f);
    for (int py = cy - glowRadius - 1; py <= cy + glowRadius + 1; ++py) {
        for (int px = cx - glowRadius - 1; px <= cx + glowRadius + 1; ++px) {
            float dist = std::sqrt(static_cast<float>((px - cx) * (px - cx) + (py - cy) * (py - cy)));

            if (dist <= radius) {
                float alpha = intensity * 0.95f;
                uint16_t col = hsvToRgb565(hue, 0.8f, 0.95f, alpha);
                SetPixelRGB565(buf, px, py, col, stride);
            } else if (dist <= glowRadius) {
                float falloff = 1.0f - (dist - radius) / (glowRadius - radius);
                falloff = falloff * falloff;
                float alpha = intensity * falloff * 0.4f;
                uint16_t col = hsvToRgb565(hue, 1.0f, 0.7f, alpha);
                SetPixelRGB565(buf, px, py, col, stride);
            }
        }
    }
}

// Draw ring with glow effect
void DrawGlowRing(uint16_t* buf, int cx, int cy, int radius, int width,
                  int hue, float intensity, float angleOffset, int stride) {
    if (intensity <= 0.01f) return;

    for (int py = cy - radius - width - 1; py <= cy + radius + width + 1; ++py) {
        for (int px = cx - radius - width - 1; px <= cx + radius + width + 1; ++px) {
            float dist = std::sqrt(static_cast<float>((px - cx) * (px - cx) + (py - cy) * (py - cy)));

            if (dist >= radius - width && dist <= radius + width) {
                float ringAlpha = intensity * 0.7f;
                float atanVal = std::atan2(static_cast<float>(py - cy), static_cast<float>(px - cx));
                float pulse = std::sin(angleOffset + atanVal * 3.0f) * 0.3f + 0.7f;
                uint16_t col = hsvToRgb565(hue, 1.0f, 0.8f, ringAlpha * pulse);
                SetPixelRGB565(buf, px, py, col, stride);
            }
        }
    }
}

// Draw particle with glow
void DrawParticle(uint16_t* buf, float angle, int ringRadius, float drift,
                  float size, float brightness, float phase, int hue, int stride) {
    int px = CX + static_cast<int>(std::cos(angle) * (ringRadius + static_cast<int>(drift * 20)));
    int py = CY + static_cast<int>(std::sin(angle) * (ringRadius + static_cast<int>(drift * 20)));

    float flicker = std::sin(phase * 5) * 0.3f + 0.7f;
    float alpha = brightness * flicker * 0.8f;

    if (alpha <= 0.1f) return;

    int r = static_cast<int>(size + 1);
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) {
                uint16_t col = hsvToRgb565(hue, 1.0f, 0.7f, alpha);
                SetPixelRGB565(buf, px + dx, py + dy, col, stride);
            }
        }
    }
}

// Draw tick marks (60 minor, 12 major)
void DrawTickMarks(uint16_t* buf, float energy, int stride) {
    if (energy < 0.1f) return;

    for (int i = 0; i < 60; ++i) {
        float angle = (i / 60.0f) * 2 * M_PI;
        bool isMajor = (i % 5 == 0);
        float len = isMajor ? 6.0f : 3.0f;
        float innerR = 165;
        float outerR = innerR + len;

        int x1 = CX + static_cast<int>(std::cos(angle) * innerR);
        int y1 = CY + static_cast<int>(std::sin(angle) * innerR);
        int x2 = CX + static_cast<int>(std::cos(angle) * outerR);
        int y2 = CY + static_cast<int>(std::sin(angle) * outerR);

        float alpha = energy * (isMajor ? 0.8f : 0.4f);
        uint16_t col = hsvToRgb565(180, 0.5f, 0.8f, alpha);

        int dx = x2 - x1, dy = y2 - y1;
        int steps = static_cast<int>(len);
        for (int s = 0; s <= steps; ++s) {
            int px = x1 + dx * s / steps;
            int py = y1 + dy * s / steps;
            SetPixelRGB565(buf, px, py, col, stride);
        }
    }

    // Major tick dots at r=176
    for (int i = 0; i < 12; ++i) {
        float angle = (i / 12.0f) * 2 * M_PI;
        int px = CX + static_cast<int>(std::cos(angle) * 176);
        int py = CY + static_cast<int>(std::sin(angle) * 176);
        uint16_t col = hsvToRgb565(180, 0.5f, 0.8f, energy * 0.7f);
        SetPixelRGB565(buf, px, py, col, stride);
        if (energy > 0.5f) {
            SetPixelRGB565(buf, px + 1, py, col, stride);
            SetPixelRGB565(buf, px, py + 1, col, stride);
        }
    }
}

// Draw listening concentric waves
void DrawListeningWaves(uint16_t* buf, uint32_t stateTime, float energy, int stride) {
    for (int wave = 0; wave < 4; ++wave) {
        float wavePhase = fmodf(stateTime * 0.001f + wave * 0.5f, 1.0f);
        int waveR = 45 + static_cast<int>(wavePhase * 125);
        float waveAlpha = (1.0f - wavePhase) * 0.5f * energy;

        if (waveAlpha < 0.05f) continue;

        uint16_t col = hsvToRgb565(180, 1.0f, 0.7f, waveAlpha);

        for (int py = CY - waveR - 2; py <= CY + waveR + 2; ++py) {
            for (int px = CX - waveR - 2; px <= CX + waveR + 2; ++px) {
                float dist = std::sqrt(static_cast<float>((px - CX) * (px - CX) + (py - CY) * (py - CY)));
                if (dist >= waveR - 1 && dist <= waveR + 1) {
                    SetPixelRGB565(buf, px, py, col, stride);
                }
            }
        }
    }
}

// Draw speaking rays with glow points
void DrawSpeakingRays(uint16_t* buf, uint32_t stateTime, float energy, int coreR, int stride) {
    for (int i = 0; i < 8; ++i) {
        float angle = i * M_PI * 2 / 8 + stateTime * 0.002f;
        float pulse = std::sin(stateTime * 0.005f + i) * 0.3f + 0.7f;
        int rayLen = 10 + static_cast<int>(pulse * (coreR - 15));

        uint16_t rayCol = hsvToRgb565(45, 1.0f, 0.8f, 0.5f * energy * pulse);

        for (int len = 10; len <= rayLen; ++len) {
            int px = CX + static_cast<int>(std::cos(angle) * len);
            int py = CY + static_cast<int>(std::sin(angle) * len);
            SetPixelRGB565(buf, px, py, rayCol, stride);
        }

        // Glow point at ray tip
        int tipX = CX + static_cast<int>(std::cos(angle) * rayLen);
        int tipY = CY + static_cast<int>(std::sin(angle) * rayLen);
        uint16_t tipCol = hsvToRgb565(45, 1.0f, 0.9f, 0.8f * energy * pulse);
        SetPixelRGB565(buf, tipX, tipY, tipCol, stride);
        SetPixelRGB565(buf, tipX + 1, tipY, tipCol, stride);
        SetPixelRGB565(buf, tipX, tipY + 1, tipCol, stride);
    }
}

} // anonymous namespace

JarvisWatchface& JarvisWatchface::GetInstance() {
    static JarvisWatchface instance;
    return instance;
}

JarvisWatchface::JarvisWatchface()
    : state_time_(0), global_energy_(0.05f), target_energy_(0.05f), breath_phase_(0) {
    for (int i = 0; i < RING_COUNT; ++i) {
        ring_angles_[i] = 0;
        ring_intensity_[i] = 0;
    }
    InitParticles();
}

JarvisWatchface::~JarvisWatchface() {
    DestroyUI();
}

void JarvisWatchface::InitParticles() {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        int ring_idx = i % RING_COUNT;
        particles_[i].ring_idx = ring_idx;
        particles_[i].angle = static_cast<float>(esp_random() % 100) / 100.0f * 2 * M_PI;
        float dir = RINGS[ring_idx].dir;
        particles_[i].speed = (0.002f + (esp_random() % 100) / 100.0f * 0.004f) * dir;
        particles_[i].size = 0.5f + (esp_random() % 100) / 100.0f * 1.5f;
        particles_[i].brightness = 0.3f + (esp_random() % 100) / 100.0f * 0.7f;
        particles_[i].drift = (static_cast<float>(esp_random() % 100) / 100.0f - 0.5f) * 0.4f;
        particles_[i].life = 1.0f;
        particles_[i].flicker = static_cast<float>(esp_random() % 100) / 100.0f * 2 * M_PI;
    }
}

void JarvisWatchface::CreateUI() {
    if (screen_ != nullptr) {
        return;
    }

    screen_ = lv_obj_create(NULL);
    lv_obj_set_size(screen_, W_, H_);
    lv_obj_set_style_radius(screen_, W_ / 2, 0);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(0x020a12), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Create canvas for drawing
    canvas_ = lv_canvas_create(screen_);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_set_size(canvas_, W, H);

    // Allocate canvas buffer (RGB565 for efficiency)
    size_t bufBytes = W * H * 2;  // RGB565 = 2 bytes per pixel
    canvas_buf_ = static_cast<uint16_t*>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM));
    if (canvas_buf_ == nullptr) {
        canvas_buf_ = static_cast<uint16_t*>(malloc(bufBytes));
    }

    if (canvas_buf_ != nullptr) {
        memset(canvas_buf_, 0, bufBytes);
        lv_canvas_set_buffer(canvas_, canvas_buf_, W, H, LV_COLOR_FORMAT_RGB565);
    }

    lv_obj_set_style_bg_opa(canvas_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(canvas_, 0, 0);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);

    timer_ = lv_timer_create(OnTimer, 33, this);

    ESP_LOGI(TAG, "JARVIS Watchface UI created (canvas %dx%d)", W, H);
}

void JarvisWatchface::DestroyUI() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }

    if (canvas_buf_ != nullptr) {
        free(canvas_buf_);
        canvas_buf_ = nullptr;
    }

    if (screen_ != nullptr) {
        lv_obj_del(screen_);
        screen_ = nullptr;
    }
}

void JarvisWatchface::Show() {
    if (!lvgl_port_lock(100)) {
        ESP_LOGW(TAG, "Show: LVGL lock timeout");
        return;
    }

    if (screen_ == nullptr) {
        CreateUI();
    }

    if (screen_ != nullptr) {
        lv_screen_load(screen_);
        visible_ = true;
        state_ = State::Starting;
        state_time_ = 0;
        InitParticles();
        ESP_LOGI(TAG, "JARVIS Watchface shown");
    }

    lvgl_port_unlock();
}

void JarvisWatchface::Hide() {
    if (!lvgl_port_lock(100)) {
        ESP_LOGW(TAG, "Hide: LVGL lock timeout");
        return;
    }

    visible_ = false;
    state_ = State::Sleep;
    state_time_ = 0;
    for (int i = 0; i < RING_COUNT; ++i) {
        ring_intensity_[i] = 0;
    }
    global_energy_ = 0.05f;

    if (screen_ != nullptr) {
        lv_obj_add_flag(screen_, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
    ESP_LOGI(TAG, "JARVIS Watchface hidden");
}

void JarvisWatchface::SetState(State state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    state_time_ = 0;
    ESP_LOGI(TAG, "JARVIS Watchface state changed to %d", static_cast<int>(state));
}

void JarvisWatchface::OnTimer(lv_timer_t* timer) {
    auto* self = static_cast<JarvisWatchface*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateAnimation();
        self->RedrawCanvas();
    }
}

void JarvisWatchface::RedrawCanvas() {
    if (canvas_buf_ == nullptr || screen_ == nullptr) {
        return;
    }

    // Clear to background color (dark blue-black)
    uint32_t bgColor = 0x020a12;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            canvas_buf_[y * W + x] = bgColor;
        }
    }

    // Draw tick marks
    DrawTickMarks(canvas_buf_, global_energy_, W);

    // Draw rings (from outer to inner)
    for (int i = 0; i < RING_COUNT; ++i) {
        DrawGlowRing(canvas_buf_, CX, CY, RINGS[i].r, RINGS[i].width,
                   RINGS[i].hue, ring_intensity_[i] * global_energy_,
                   ring_angles_[i], W);
    }

    // Draw particles
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        const auto& p = particles_[i];
        int r = RINGS[p.ring_idx].r;
        DrawParticle(canvas_buf_, p.angle, r, p.drift, p.size,
                   p.brightness, breath_phase_ + p.flicker,
                   RINGS[p.ring_idx].hue, W);
    }

    // Draw core
    float breath = std::sin(breath_phase_) * 0.5f + 0.5f;
    int coreR = 78 + static_cast<int>(breath * 5 + global_energy_ * 5);
    DrawGlowCircle(canvas_buf_, CX, CY, coreR, 190, global_energy_, W);

    // Draw listening effect
    if (state_ == State::Listening) {
        DrawListeningWaves(canvas_buf_, state_time_, global_energy_, W);
    }

    // Draw speaking effect
    if (state_ == State::Speaking) {
        DrawSpeakingRays(canvas_buf_, state_time_, global_energy_, coreR, W);
    }
}

void JarvisWatchface::UpdateAnimation() {
    if (!visible_ || screen_ == nullptr) {
        return;
    }

    state_time_ += 33;

    // Update state machine
    switch (state_) {
        case State::Sleep:
            target_energy_ = 0.05f;
            for (int i = 0; i < RING_COUNT; ++i) {
                ring_intensity_[i] = 0;
            }
            break;

        case State::Starting: {
            float progress = std::fmin(state_time_ / 3000.0f, 1.0f);
            target_energy_ = progress;

            for (int i = 0; i < RING_COUNT; ++i) {
                float delay = i * 600.0f;
                float ring_progress = std::fmax(0.0f, std::fmin((state_time_ - delay) / 800.0f, 1.0f));
                ring_intensity_[i] = ring_progress;
            }

            if (state_time_ > 3000) {
                SetState(State::Active);
            }
            break;
        }

        case State::Active: {
            float breath = std::sin(breath_phase_) * 0.5f + 0.5f;
            target_energy_ = 0.85f + breath * 0.1f;
            for (int i = 0; i < RING_COUNT; ++i) {
                ring_intensity_[i] = 0.9f + std::sin(breath_phase_ + i) * 0.1f;
            }
            break;
        }

        case State::Listening: {
            float listen_breath = std::sin(state_time_ * 0.003f) * 0.5f + 0.5f;
            target_energy_ = 0.7f + listen_breath * 0.2f;
            for (int i = 0; i < RING_COUNT; ++i) {
                ring_intensity_[i] = 0.6f + listen_breath * 0.3f;
            }
            break;
        }

        case State::Speaking: {
            float speak_pulse = std::sin(state_time_ * 0.005f) * 0.3f + 0.7f;
            target_energy_ = 0.8f + speak_pulse * 0.2f;
            for (int i = 0; i < RING_COUNT; ++i) {
                ring_intensity_[i] = 0.8f + speak_pulse * 0.2f;
            }
            break;
        }
    }

    // Smooth energy transition
    global_energy_ += (target_energy_ - global_energy_) * 0.05f;

    // Update ring angles
    for (int i = 0; i < RING_COUNT; ++i) {
        ring_angles_[i] += RINGS[i].speed * 0.001f * (0.5f + global_energy_);
    }

    // Update breath phase
    breath_phase_ += 0.02f;

    // Update particles
    UpdateParticles(33);
}

void JarvisWatchface::UpdateParticles(uint32_t dt) {
    if (global_energy_ < 0.1f) return;

    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particles_[i].angle += particles_[i].speed * (1.0f + global_energy_ * 2.0f);
        particles_[i].flicker += 0.05f;
    }
}

// Static member initialization
uint16_t* JarvisWatchface::canvas_buf_ = nullptr;
