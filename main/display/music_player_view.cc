// 黑胶唱片音乐播放器 UI 实现
//   - 仿 DebugInfoCard 风格的圆形浮层（300x300）
//   - 中心"唱片"用 lv_canvas 预渲染：黑色圆盘 + 同心圆纹路 + 红色中心标签
//   - lv_anim 实现旋转效果（33⅓ RPM）
//   - 控件：title / progress bar / play-pause btn / close btn / status

#include "music_player_view.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"

static const char* TAG = "MusicPlayerView";

// 圆形浮层常量（与 attitude_display 保持风格一致）
#define VINYL_CARD_SIZE 300
#define VINYL_CARD_X    ((LV_HOR_RES - VINYL_CARD_SIZE) / 2)
#define VINYL_CARD_Y    ((LV_VER_RES - VINYL_CARD_SIZE) / 2)
#define VINYL_CARD_BG   lv_color_hex(0x0A1414)
#define VINYL_CARD_BORDER lv_color_hex(0x00C8C8)  // 青色描边（与 image_overlay_card_ 一致）
#define VINYL_CARD_OPA  LV_OPA_90
#define VINYL_CARD_RADIUS (VINYL_CARD_SIZE / 2)

// 唱片 canvas 配置（200x200，居中在 300x300 浮层）
// 旋转动画：33⅓ RPM = 0.555 圈/秒 = 360°/1.8s
// LVGL lv_anim 单位是 0.1°，所以一次循环 3600 单位，duration 1800ms
#define VINYL_ROTATION_PERIOD_MS  1800
#define VINYL_ROTATION_ANGLE_MAX  3600

// 进度条更新周期（与 MusicPlayer::PROGRESS_NOTIFY_MS 一致）
#define PROGRESS_TICK_MS  250

MusicPlayerView& MusicPlayerView::GetInstance() {
    static MusicPlayerView instance;
    return instance;
}

// ========== 创建 / 销毁 ==========

void MusicPlayerView::Create(lv_obj_t* parent) {
    if (overlay_card_ != nullptr) {
        ESP_LOGW(TAG, "Create: already created, skip");
        return;
    }
    parent_ = parent;

    // 1. 浮层容器
    overlay_card_ = lv_obj_create(parent);
    lv_obj_set_size(overlay_card_, VINYL_CARD_SIZE, VINYL_CARD_SIZE);
    lv_obj_set_pos(overlay_card_, VINYL_CARD_X, VINYL_CARD_Y);
    lv_obj_set_style_radius(overlay_card_, VINYL_CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(overlay_card_, true, 0);
    lv_obj_set_style_bg_color(overlay_card_, VINYL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(overlay_card_, VINYL_CARD_OPA, 0);
    lv_obj_set_style_border_color(overlay_card_, VINYL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(overlay_card_, 2, 0);
    lv_obj_set_style_pad_all(overlay_card_, 0, 0);
    lv_obj_clear_flag(overlay_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay_card_, LV_OBJ_FLAG_HIDDEN);

    // 2. 唱片 label（MVP：旋转一个大字符 ♪ 当作唱片）
    // 2. 黑胶唱片 canvas（200x200 RGB565 预渲染）
    //   - 用 lv_draw_buf_create 分配 buffer
    //   - lv_canvas_set_draw_buf 绑定 buffer
    //   - RenderVinylRecord() 画图
    //   - lv_anim 旋转（33⅓ RPM = 1800ms/圈）
    const int canvas_size = 200;
    static lv_draw_buf_t* vinyl_buf = nullptr;
    if (vinyl_buf == nullptr) {
        // stride = 0 让 LVGL 自动计算（按 color_format + width 对齐）
        vinyl_buf = lv_draw_buf_create(canvas_size, canvas_size, LV_COLOR_FORMAT_RGB565, 0);
    }
    if (vinyl_buf != nullptr) {
        vinyl_canvas_ = lv_canvas_create(overlay_card_);
        lv_canvas_set_draw_buf(vinyl_canvas_, vinyl_buf);
        lv_obj_set_size(vinyl_canvas_, canvas_size, canvas_size);
        lv_obj_center(vinyl_canvas_);
        lv_obj_clear_flag(vinyl_canvas_, LV_OBJ_FLAG_CLICKABLE);
        // 先填充黑色背景
        lv_canvas_fill_bg(vinyl_canvas_, lv_color_hex(0x000000), LV_OPA_COVER);
        // 画唱片内容
        RenderVinylRecord();
    } else {
        // fallback：画一个文字（buffer 分配失败）
        vinyl_canvas_ = lv_label_create(overlay_card_);
        lv_obj_set_size(vinyl_canvas_, canvas_size, canvas_size);
        lv_obj_center(vinyl_canvas_);
        lv_label_set_text(vinyl_canvas_, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_font(vinyl_canvas_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(vinyl_canvas_, lv_color_hex(0xD4AF37), 0);
        lv_obj_set_style_text_align(vinyl_canvas_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_clear_flag(vinyl_canvas_, LV_OBJ_FLAG_CLICKABLE);
    }

    // 3. 标题 label（顶部）
    title_label_ = lv_label_create(overlay_card_);
    lv_obj_set_size(title_label_, VINYL_CARD_SIZE - 40, 24);
    lv_obj_set_pos(title_label_, 20, 12);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xC0C0C0), 0);  // 银灰
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title_label_, "Music");
    lv_obj_clear_flag(title_label_, LV_OBJ_FLAG_CLICKABLE);

    // 4. 状态 label（顶部下）
    status_label_ = lv_label_create(overlay_card_);
    lv_obj_set_size(status_label_, VINYL_CARD_SIZE - 40, 20);
    lv_obj_set_pos(status_label_, 20, 32);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, "Ready");
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_CLICKABLE);

    // 5. 进度条
    progress_bar_ = lv_bar_create(overlay_card_);
    lv_obj_set_size(progress_bar_, VINYL_CARD_SIZE - 60, 8);
    lv_obj_set_pos(progress_bar_, 30, VINYL_CARD_SIZE - 60);
    lv_bar_set_range(progress_bar_, 0, 1000);  // 0.1% 精度
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_, lv_color_hex(0x00C8C8), LV_PART_INDICATOR);

    // 6. 播放/暂停按钮（底部中间）
    play_pause_btn_ = lv_btn_create(overlay_card_);
    lv_obj_set_size(play_pause_btn_, 50, 50);
    lv_obj_set_pos(play_pause_btn_, (VINYL_CARD_SIZE - 50) / 2, VINYL_CARD_SIZE - 45);
    lv_obj_set_style_bg_color(play_pause_btn_, lv_color_hex(0xD4AF37), 0);
    lv_obj_set_style_radius(play_pause_btn_, 25, 0);
    lv_obj_add_event_cb(play_pause_btn_, PlayPauseBtnEventCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* play_pause_label = lv_label_create(play_pause_btn_);
    lv_obj_set_style_text_font(play_pause_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(play_pause_label, lv_color_hex(0x0A1414), 0);
    lv_label_set_text(play_pause_label, LV_SYMBOL_PLAY);
    lv_obj_center(play_pause_label);

    // 7. 上一首按钮（左）
    prev_btn_ = lv_btn_create(overlay_card_);
    lv_obj_set_size(prev_btn_, 44, 44);
    lv_obj_set_pos(prev_btn_, 35, VINYL_CARD_SIZE - 42);
    lv_obj_set_style_bg_color(prev_btn_, lv_color_hex(0x303030), 0);
    lv_obj_set_style_radius(prev_btn_, 22, 0);
    lv_obj_add_event_cb(prev_btn_, PrevBtnEventCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* prev_label = lv_label_create(prev_btn_);
    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(prev_label, lv_color_hex(0xD4AF37), 0);
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);
    lv_obj_center(prev_label);

    // 8. 下一首按钮（右）
    next_btn_ = lv_btn_create(overlay_card_);
    lv_obj_set_size(next_btn_, 44, 44);
    lv_obj_set_pos(next_btn_, VINYL_CARD_SIZE - 79, VINYL_CARD_SIZE - 42);
    lv_obj_set_style_bg_color(next_btn_, lv_color_hex(0x303030), 0);
    lv_obj_set_style_radius(next_btn_, 22, 0);
    lv_obj_add_event_cb(next_btn_, NextBtnEventCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* next_label = lv_label_create(next_btn_);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(next_label, lv_color_hex(0xD4AF37), 0);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_center(next_label);

    // 9. 关闭按钮（右上角小）
    close_btn_ = lv_btn_create(overlay_card_);
    lv_obj_set_size(close_btn_, 32, 32);
    lv_obj_set_pos(close_btn_, VINYL_CARD_SIZE - 40, 8);
    lv_obj_set_style_bg_color(close_btn_, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(close_btn_, 16, 0);
    lv_obj_add_event_cb(close_btn_, CloseBtnEventCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close_label = lv_label_create(close_btn_);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);

    // 10. 唱片中心标题（在唱片 canvas 之上，不旋转）
    //   - 中心 60x20 区域
    //   - 显示短文件名（如 "song1.mp3"）
    track_label_ = lv_label_create(overlay_card_);
    lv_obj_set_size(track_label_, 80, 18);
    lv_obj_set_pos(track_label_, (VINYL_CARD_SIZE - 80) / 2, (VINYL_CARD_SIZE - 18) / 2);
    lv_obj_set_style_text_font(track_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(track_label_, lv_color_hex(0x0A0A0A), 0);  // 黑字（红色标签上）
    lv_obj_set_style_text_align(track_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(track_label_, "");
    lv_obj_clear_flag(track_label_, LV_OBJ_FLAG_CLICKABLE);

    // 8. 订阅 MusicPlayer 状态
    MusicPlayer::GetInstance().SetStateListener(
        [this](MusicPlayer::State state, float progress) {
            OnPlayerStateChanged(state, progress);
        });

    ESP_LOGI(TAG, "MusicPlayerView created (parent=%p)", parent);
}

void MusicPlayerView::Destroy() {
    if (overlay_card_) {
        lv_obj_del(overlay_card_);
        overlay_card_ = nullptr;
    }
    vinyl_canvas_ = nullptr;
    title_label_ = nullptr;
    status_label_ = nullptr;
    progress_bar_ = nullptr;
    play_pause_btn_ = nullptr;
    close_btn_ = nullptr;
    rotation_running_ = false;
}

// ========== 显示 / 隐藏 ==========

void MusicPlayerView::Show() {
    if (overlay_card_ == nullptr) {
        ESP_LOGW(TAG, "Show: not created");
        return;
    }
    lv_obj_remove_flag(overlay_card_, LV_OBJ_FLAG_HIDDEN);
    StartRotation();
    ESP_LOGI(TAG, "Show");
}

void MusicPlayerView::Hide() {
    if (overlay_card_ == nullptr) return;
    lv_obj_add_flag(overlay_card_, LV_OBJ_FLAG_HIDDEN);
    StopRotation();
    ESP_LOGI(TAG, "Hide");
}

bool MusicPlayerView::IsVisible() const {
    if (overlay_card_ == nullptr) return false;
    return (lv_obj_has_flag(overlay_card_, LV_OBJ_FLAG_HIDDEN) == false);
}

// ========== 旋转动画 ==========

void MusicPlayerView::RotationAnimCb(void* var, int32_t v) {
    lv_obj_t* label = static_cast<lv_obj_t*>(var);
    if (label == nullptr) return;
    // LVGL transform angle 单位：0.1°，顺时针为正
    lv_obj_set_style_transform_angle(label, v, 0);
}

void MusicPlayerView::StartRotation() {
    if (rotation_running_ || vinyl_canvas_ == nullptr) return;
    lv_anim_init(&rotation_anim_);
    lv_anim_set_var(&rotation_anim_, vinyl_canvas_);
    lv_anim_set_exec_cb(&rotation_anim_, RotationAnimCb);
    lv_anim_set_values(&rotation_anim_, 0, VINYL_ROTATION_ANGLE_MAX);
    lv_anim_set_time(&rotation_anim_, VINYL_ROTATION_PERIOD_MS);
    lv_anim_set_repeat_count(&rotation_anim_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_playback_time(&rotation_anim_, 0);
    lv_anim_start(&rotation_anim_);
    rotation_running_ = true;
    ESP_LOGI(TAG, "Rotation started (33⅓ RPM = %d ms/turn)", VINYL_ROTATION_PERIOD_MS);
}

void MusicPlayerView::StopRotation() {
    if (!rotation_running_) return;
    lv_anim_del(vinyl_canvas_, RotationAnimCb);
    rotation_running_ = false;
    ESP_LOGI(TAG, "Rotation stopped");
}

// ========== 按钮事件 ==========

void MusicPlayerView::PlayPauseBtnEventCb(lv_event_t* e) {
    (void)e;
    MusicPlayer& mp = MusicPlayer::GetInstance();
    auto state = mp.GetState();
    if (state == MusicPlayer::State::Playing) {
        mp.Pause();
    } else if (state == MusicPlayer::State::Paused) {
        mp.Resume();
    } else if (state == MusicPlayer::State::Idle || state == MusicPlayer::State::Error) {
        // 没在播放，按钮重置
        ESP_LOGI(TAG, "PlayPause clicked in idle/error state, no-op");
    } else {
        // Loading 时忽略
        ESP_LOGI(TAG, "PlayPause clicked in loading state, ignored");
    }
}

void MusicPlayerView::CloseBtnEventCb(lv_event_t* e) {
    (void)e;
    MusicPlayer::GetInstance().Stop();
    MusicPlayerView::GetInstance().Hide();
    ESP_LOGI(TAG, "Close clicked, stopped and hidden");
}

void MusicPlayerView::PrevBtnEventCb(lv_event_t* e) {
    (void)e;
    MusicPlayer::GetInstance().PlayPrev();
}

void MusicPlayerView::NextBtnEventCb(lv_event_t* e) {
    (void)e;
    MusicPlayer::GetInstance().PlayNext();
}

// ========== 状态同步 ==========

void MusicPlayerView::OnPlayerStateChanged(MusicPlayer::State state, float progress) {
    UpdatePlayPauseBtn(state);

    // 状态文字（带 playlist 位置，如 "Playing [3/10]"）
    char status_buf[64] = {0};
    const char* state_text = "Ready";
    switch (state) {
        case MusicPlayer::State::Idle:    state_text = "Ready"; break;
        case MusicPlayer::State::Loading: state_text = "Loading..."; break;
        case MusicPlayer::State::Playing: state_text = "Playing"; break;
        case MusicPlayer::State::Paused:  state_text = "Paused"; break;
        case MusicPlayer::State::Error:   state_text = "Error"; break;
    }
    auto& mp_ref = MusicPlayer::GetInstance();
    const auto& pl = mp_ref.GetCurrentPlaylist();
    if (!pl.empty()) {
        snprintf(status_buf, sizeof(status_buf), "%s [%zu/%zu]",
                 state_text, mp_ref.GetCurrentPlaylistIndex() + 1, pl.size());
    } else {
        snprintf(status_buf, sizeof(status_buf), "%s", state_text);
    }
    if (status_label_ != nullptr) {
        lv_label_set_text(status_label_, status_buf);
        // 错误状态：变红；其他状态：保持灰色
        if (state == MusicPlayer::State::Error) {
            lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFF4444), 0);
        } else {
            lv_obj_set_style_text_color(status_label_, lv_color_hex(0x808080), 0);
        }
    }

    // Error 状态：3 秒后自动 Hide 整个 UI（让用户看到错误信息但不阻塞）
    if (state == MusicPlayer::State::Error) {
        // 删除旧 timer（如果有）
        if (error_hide_timer_ != nullptr) {
            lv_timer_del(error_hide_timer_);
            error_hide_timer_ = nullptr;
        }
        error_hide_timer_ = lv_timer_create([](lv_timer_t* t) {
            (void)t;
            MusicPlayerView::GetInstance().Hide();
        }, 3000, nullptr);
        // 把 last_error 显示在 title_label（让用户能看到具体原因）
        if (title_label_ != nullptr) {
            std::string err = MusicPlayer::GetInstance().GetLastError();
            if (err.empty()) err = "Error";
            lv_label_set_text(title_label_, err.c_str());
        }
    } else {
        // 非 error 状态：取消隐藏 timer
        if (error_hide_timer_ != nullptr) {
            lv_timer_del(error_hide_timer_);
            error_hide_timer_ = nullptr;
        }
    }

    // 标题 = 文件名
    if (title_label_ != nullptr) {
        std::string file = mp_ref.GetCurrentFile();
        if (file.empty()) {
            lv_label_set_text(title_label_, "Music");
        } else {
            // 取最后 30 字符
            std::string short_name = file;
            // 取 basename（去路径）
            size_t last_slash = short_name.rfind('/');
            if (last_slash != std::string::npos) {
                short_name = short_name.substr(last_slash + 1);
            }
            if (short_name.size() > 30) {
                short_name = "..." + short_name.substr(short_name.size() - 27);
            }
            lv_label_set_text(title_label_, short_name.c_str());
        }
    }

    // 唱片中心标题：更短（basename，不超过 12 字符）
    if (track_label_ != nullptr) {
        std::string file = mp_ref.GetCurrentFile();
        if (file.empty()) {
            lv_label_set_text(track_label_, "");
        } else {
            size_t last_slash = file.rfind('/');
            if (last_slash != std::string::npos) {
                file = file.substr(last_slash + 1);
            }
            // 去扩展名
            size_t last_dot = file.rfind('.');
            if (last_dot != std::string::npos) {
                file = file.substr(0, last_dot);
            }
            if (file.size() > 12) {
                file = file.substr(0, 12);
            }
            lv_label_set_text(track_label_, file.c_str());
        }
    }

    // 显示/隐藏 prev/next 按钮（仅 playlist 模式显示）
    bool has_playlist = !pl.empty();
    if (prev_btn_ != nullptr) {
        if (has_playlist) lv_obj_remove_flag(prev_btn_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(prev_btn_, LV_OBJ_FLAG_HIDDEN);
    }
    if (next_btn_ != nullptr) {
        if (has_playlist) lv_obj_remove_flag(next_btn_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(next_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    UpdateProgress(progress);

    // Playing/Loading/Paused 时自动 Show，Idle/Error 时不自动隐藏
    if (state == MusicPlayer::State::Playing ||
        state == MusicPlayer::State::Paused ||
        state == MusicPlayer::State::Loading) {
        if (!IsVisible()) {
            Show();
        }
    }

    // Playing 时启动旋转，其他状态停止
    if (state == MusicPlayer::State::Playing) {
        StartRotation();
    } else {
        StopRotation();
    }
}

void MusicPlayerView::UpdatePlayPauseBtn(MusicPlayer::State state) {
    if (play_pause_btn_ == nullptr) return;
    lv_obj_t* label = lv_obj_get_child(play_pause_btn_, 0);
    if (label == nullptr) return;
    const char* sym = LV_SYMBOL_PLAY;
    if (state == MusicPlayer::State::Playing) {
        sym = LV_SYMBOL_PAUSE;
    } else if (state == MusicPlayer::State::Loading) {
        sym = LV_SYMBOL_REFRESH;  // 加载中显示刷新图标
    }
    lv_label_set_text(label, sym);
}

void MusicPlayerView::UpdateTitle(const char* text) {
    if (title_label_ != nullptr) {
        lv_label_set_text(title_label_, text);
    }
}

void MusicPlayerView::UpdateProgress(float progress) {
    if (progress_bar_ == nullptr) return;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    lv_bar_set_value(progress_bar_, (int32_t)(progress * 1000), LV_ANIM_OFF);
}

// ========== Tick（外部 timer 调用） ==========

void MusicPlayerView::OnMainThreadTick() {
    if (!IsVisible()) return;
    // 同步最新 progress
    float prog = MusicPlayer::GetInstance().GetProgress();
    UpdateProgress(prog);
}

// ========== 全局 init 函数（被 sdcard_log_http.cc 调用） ==========
//
// 作用：
//   1. 在屏幕顶层创建 MusicPlayerView UI 树（隐藏）
//   2. 启动一个 LVGL timer（100ms 周期）持续 tick：
//        - 消费 MusicPlayer 请求队列（Play/Pause/Resume/Stop）
//        - 同步 MusicPlayerView 的 progress
//   3. MusicPlayer::GetInstance() 单例在首次访问时自动注册默认 decoder
//
// 调用时机：sdcard_log_http.cc::start_http_server 内部，httpd_start 之前
//   - 此时 lvgl_port_init 已完成
//   - 屏幕 lv_obj_t* 可通过 lv_scr_act() 获取
//
// 注意：此函数必须从 LVGL 主线程调用（httpd_start 之前一定是主线程上下文）
static lv_timer_t* g_music_player_tick_timer = nullptr;
static void music_player_tick_cb(lv_timer_t* t) {
    (void)t;
    MusicPlayer::GetInstance().OnMainThreadTick();
    MusicPlayerView::GetInstance().OnMainThreadTick();
}

extern "C" void init_music_player_view(void) {
    static bool inited = false;
    if (inited) return;
    inited = true;

    lv_obj_t* screen = lv_scr_act();
    if (screen == nullptr) {
        ESP_LOGE(TAG, "init_music_player_view: lv_scr_act() returned nullptr");
        return;
    }

    // 1. Create UI 树
    MusicPlayerView::GetInstance().Create(screen);

    // 2. 启动 tick timer（100ms 周期；LVGL 主线程）
    g_music_player_tick_timer = lv_timer_create(music_player_tick_cb, 100, nullptr);
    if (g_music_player_tick_timer == nullptr) {
        ESP_LOGE(TAG, "init_music_player_view: failed to create tick timer");
    } else {
        ESP_LOGI(TAG, "init_music_player_view: UI created, tick timer started (100ms)");
    }
}

// ========== 黑胶唱片预渲染 ==========
//
// 视觉结构（200x200 画布）：
//   - 黑色圆盘（半径 98，几乎占满）
//   - 鎏金描边外圈（半径 98，宽 2px）
//   - 5 圈灰色沟纹（半径 80/65/50/35/20，1px）
//   - 中心红色标签（半径 24）
//   - 中心黑色轴孔（半径 4）
//
// 用 Bresenham 圆算法 + lv_canvas_set_px
void MusicPlayerView::RenderVinylRecord() {
    if (vinyl_canvas_ == nullptr) return;
    const int w = 200, h = 200;
    const int cx = w / 2, cy = h / 2;
    lv_color_t black = lv_color_hex(0x000000);
    lv_color_t gray = lv_color_hex(0x404040);
    lv_color_t gold = lv_color_hex(0xD4AF37);
    lv_color_t red = lv_color_hex(0xB82601);
    lv_color_t bg_dark = lv_color_hex(0x0A0A0A);

    // 1. 黑色圆盘（用 lv_canvas_fill_bg 已经在 Create 中做了）
    //    这里额外用近似填充（防止 fill_bg 透明区域被覆盖）

    // 2. 鎏金外圈（半径 98）
    auto draw_circle = [&](int r, lv_color_t color) {
        int x = 0;
        int y = r;
        int d = 1 - r;
        while (x <= y) {
            // 8 个对称点
            lv_canvas_set_px(vinyl_canvas_, cx + x, cy + y, color, LV_OPA_COVER);
            lv_canvas_set_px(vinyl_canvas_, cx - x, cy + y, color, LV_OPA_COVER);
            lv_canvas_set_px(vinyl_canvas_, cx + x, cy - y, color, LV_OPA_COVER);
            lv_canvas_set_px(vinyl_canvas_, cx - x, cy - y, color, LV_OPA_COVER);
            lv_canvas_set_px(vinyl_canvas_, cx + y, cy + x, color, LV_OPA_COVER);
            lv_canvas_set_px(vinyl_canvas_, cx - y, cy + x, color, LV_OPA_COVER);
            lv_canvas_set_px(vinyl_canvas_, cx + y, cy - x, color, LV_OPA_COVER);
            lv_canvas_set_px(vinyl_canvas_, cx - y, cy - x, color, LV_OPA_COVER);
            if (d < 0) {
                d += 2 * x + 3;
            } else {
                d += 2 * (x - y) + 5;
                y--;
            }
            x++;
        }
    };

    // 鎏金外圈（粗 2 像素：画 r=98 和 r=97）
    draw_circle(98, gold);
    draw_circle(97, gold);

    // 3. 同心圆沟纹（5 圈，灰色）
    int grooves[] = {85, 75, 65, 55, 45};
    for (int r : grooves) {
        draw_circle(r, gray);
    }

    // 4. 中心红色标签（实心圆 + 鎏金边）
    //    用多个同心圆近似填充（半径 30）
    for (int r = 30; r >= 22; r--) {
        draw_circle(r, red);
    }
    // 鎏金边
    draw_circle(30, gold);
    draw_circle(31, gold);

    // 5. 中心黑色轴孔（半径 5 实心）
    for (int r = 5; r >= 0; r--) {
        draw_circle(r, black);
    }

    // 6. 高光（左上 1/4 处一条弧线 - 简化：画一个圆点）
    //    高光是黑胶唱片标志性反光，MVP 简化：两个亮点
    int hx1 = cx - 35, hy1 = cy - 35;
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx*dx + dy*dy <= 9) {  // r=3
                lv_canvas_set_px(vinyl_canvas_, hx1 + dx, hy1 + dy,
                                 lv_color_hex(0x606060), LV_OPA_70);
            }
        }
    }

    (void)bg_dark;  // 备用，避免未使用警告
    ESP_LOGI(TAG, "Vinyl record rendered: %dx%d canvas", w, h);
}
