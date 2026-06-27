#ifndef _MUSIC_PLAYER_H_
#define _MUSIC_PLAYER_H_

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "esp_audio_dec.h"

// 音乐播放器核心单例
//   - 接受本地路径或 HTTP URL
//   - 异步下载（如 URL）→ 临时文件 → esp_audio_codec 流式解码 → AudioCodec I2S 输出
//   - 状态机: Idle → Loading → Playing → Paused/Stopped/Error
//   - LVGL 主线程安全：所有 LVGL 操作通过 View 层在主线程执行
class MusicPlayer {
public:
    enum class State {
        Idle,       // 无播放任务
        Loading,    // 下载中（URL 模式）或初始化解码器
        Playing,    // 正在播放
        Paused,     // 已暂停
        Error       // 出错（查看 last_error_）
    };

    enum class SourceType {
        None,
        File,       // SD 卡本地文件
        Url,        // HTTP/HTTPS URL
    };

    // 状态变化回调：参数是 state + 进度（0.0~1.0，Paused/Error 时 = 0）
    using StateListener = std::function<void(State, float)>;

    static MusicPlayer& GetInstance();

    // 启动播放（线程安全，可从 httpd handler 调用）
    //   - path: SD 卡文件绝对路径（/sdcard/xxx.mp3）
    //   - url: HTTP URL（与 path 二选一）
    //   - loop: 是否循环播放
    void PlayFile(const std::string& path, bool loop = false);
    void PlayUrl(const std::string& url, bool loop = false);

    // 启动播放列表（多首连续播放，播放顺序按 paths 数组）
    //   - 内部存为 current_playlist_，每首播完后自动投递下一首
    //   - loop: 整个列表播完后是否从头重新开始
    //   - 单曲数量 0 = 忽略
    void PlayPlaylist(const std::vector<std::string>& paths, bool loop = false);

    // 列表导航（仅在 playlist 模式下有意义；单曲模式忽略）
    //   - PlayNext: 跳到列表下一首（末尾→0 循环）
    //   - PlayPrev: 跳到列表上一首（首部→末尾 循环）
    //   - 返回 true 表示成功切换，false 表示无 playlist
    bool PlayNext();
    bool PlayPrev();

    // 播放控制
    void Pause();
    void Resume();
    void Stop();

    // 状态查询
    State GetState() const { return state_.load(); }
    float GetProgress() const { return progress_.load(); }
    std::string GetCurrentFile() const;
    std::string GetLastError() const { return last_error_; }
    const std::vector<std::string>& GetCurrentPlaylist() const { return current_playlist_; }
    size_t GetCurrentPlaylistIndex() const { return current_playlist_index_; }

    // 状态监听（LVGL UI 用）
    void SetStateListener(StateListener listener) {
        std::lock_guard<std::mutex> lock(listener_mutex_);
        state_listener_ = listener;
    }

    // LVGL 主线程 timer 调用：消费请求队列、更新进度
    void OnMainThreadTick();

private:
    MusicPlayer();
    ~MusicPlayer();

    // 请求类型
    enum class RequestType { Play, Pause, Resume, Stop };
    struct Request {
        RequestType type;
        char param[512];   // path 或 url
        bool loop;
    };

    // 内部实现（在主线程调用）
    void HandlePlayFile(const std::string& path, bool loop);
    void HandlePlayUrl(const std::string& url, bool loop);
    void HandlePause();
    void HandleResume();
    void HandleStop();
    void HandleLoadingComplete();
    void HandlePlayingComplete();
    void HandleError(const char* msg);

    // 异步任务
    static void PlayTask(void* arg);  // 下载+解码+播放后台任务

    // 状态广播
    void NotifyState(State state, float progress);

    // 成员
    std::atomic<State> state_{State::Idle};
    std::atomic<float> progress_{0.0f};
    std::atomic<SourceType> source_type_{SourceType::None};

    std::string current_file_;   // 当前播放的本地文件路径
    std::string current_url_;    // 当前播放的 URL（仅 Url 模式）
    std::string last_error_;

    // 播放列表（连续播放）
    std::vector<std::string> current_playlist_;
    size_t current_playlist_index_ = 0;
    bool playlist_loop_ = false;

    QueueHandle_t request_queue_ = nullptr;
    TaskHandle_t play_task_ = nullptr;

    // 播放控制
    volatile bool paused_ = false;
    volatile bool stop_requested_ = false;
    volatile bool loop_ = false;

    // 状态监听
    StateListener state_listener_;
    std::mutex listener_mutex_;

    friend class MusicPlayerView;  // 允许 View 访问内部状态
};

// 状态转字符串（用于日志和 HTTP 响应）
inline const char* MusicPlayerStateToString(MusicPlayer::State s) {
    switch (s) {
        case MusicPlayer::State::Idle:    return "idle";
        case MusicPlayer::State::Loading: return "loading";
        case MusicPlayer::State::Playing: return "playing";
        case MusicPlayer::State::Paused:  return "paused";
        case MusicPlayer::State::Error:   return "error";
    }
    return "unknown";
}

#endif  // _MUSIC_PLAYER_H_
