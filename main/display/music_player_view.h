#ifndef _MUSIC_PLAYER_VIEW_H_
#define _MUSIC_PLAYER_VIEW_H_

#include <lvgl.h>
#include "music_player.h"

// 黑胶唱片音乐播放器 UI 组件
//   - 仿 image_overlay_card_ / DebugInfoCard 风格的圆形浮层
//   - 居中渲染旋转的"唱片"（MVP：旋转文字图标；后续可替换为 canvas 预渲染图）
//   - 控件：播放/暂停、关闭、进度条、标题
//   - 通过 SetStateListener 与 MusicPlayer 通信
//   - 必须在 LVGL 主线程调用 Create/Show/Hide
class MusicPlayerView {
public:
    static MusicPlayerView& GetInstance();

    // 在 parent（通常是 screen）上创建 UI 树（不显示）
    void Create(lv_obj_t* parent);

    // 显示/隐藏（叠加在 attitude 主界面上）
    void Show();
    void Hide();
    bool IsVisible() const;

    // 销毁（应用退出时）
    void Destroy();

    // LVGL 主线程 tick：每 100ms 由外部 timer 调用，更新进度条
    void OnMainThreadTick();

private:
    MusicPlayerView() = default;
    ~MusicPlayerView() = default;

    // 事件回调
    static void PlayPauseBtnEventCb(lv_event_t* e);
    static void CloseBtnEventCb(lv_event_t* e);
    static void PrevBtnEventCb(lv_event_t* e);
    static void NextBtnEventCb(lv_event_t* e);

    // 旋转动画 callback
    static void RotationAnimCb(void* var, int32_t v);

    // MusicPlayer 状态变化回调
    void OnPlayerStateChanged(MusicPlayer::State state, float progress);

    // UI 状态同步
    void UpdatePlayPauseBtn(MusicPlayer::State state);
    void UpdateTitle(const char* text);
    void UpdateProgress(float progress);

    // 旋转控制
    void StartRotation();
    void StopRotation();

    // 渲染黑胶唱片（黑底 + 同心圆沟纹 + 中心金色/红色标签）
    void RenderVinylRecord();

    // UI 组件
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* overlay_card_ = nullptr;     // 300x300 圆形浮层
    lv_obj_t* vinyl_canvas_ = nullptr;     // 旋转的"唱片"（lv_canvas 预渲染）
    lv_obj_t* title_label_ = nullptr;      // 顶部音乐名
    lv_obj_t* progress_bar_ = nullptr;     // 进度条
    lv_obj_t* play_pause_btn_ = nullptr;   // 播放/暂停按钮
    lv_obj_t* close_btn_ = nullptr;        // 关闭按钮
    lv_obj_t* prev_btn_ = nullptr;         // 上一首按钮（playlist 模式）
    lv_obj_t* next_btn_ = nullptr;         // 下一首按钮（playlist 模式）
    lv_obj_t* status_label_ = nullptr;     // 状态文字（loading/error）
    lv_obj_t* track_label_ = nullptr;      // 唱片中心标题（旋转层之上）

    // 旋转动画
    lv_anim_t rotation_anim_;
    bool rotation_running_ = false;

    // 错误状态自动隐藏 timer
    lv_timer_t* error_hide_timer_ = nullptr;
};

#endif  // _MUSIC_PLAYER_VIEW_H_
