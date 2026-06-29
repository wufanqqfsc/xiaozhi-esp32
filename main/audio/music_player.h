#ifndef _MUSIC_PLAYER_H_
#define _MUSIC_PLAYER_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "esp_audio_dec.h"

class MusicPlayer {
public:
    enum class State {
        Idle,
        Loading,
        Playing,
        Paused,
        Error
    };

    enum class SourceType {
        None,
        File,
        Url,
    };

    using StateListener = std::function<void(State, float)>;

    static MusicPlayer& GetInstance();

    void PlayFile(const std::string& path, bool loop = false);
    void PlayUrl(const std::string& url, bool loop = false);
    void PlayPlaylist(const std::vector<std::string>& paths, bool loop = false);

    bool PlayNext();
    bool PlayPrev();

    void Pause();
    void Resume();
    void Stop();

    State GetState() const { return state_.load(); }
    float GetProgress() const { return progress_.load(); }
    std::string GetCurrentFile() const;
    std::string GetLastError() const { return last_error_; }
    const std::vector<std::string>& GetCurrentPlaylist() const { return current_playlist_; }
    size_t GetCurrentPlaylistIndex() const { return current_playlist_index_; }

    static void RegisterDecoderTask(void* arg);

    void SetStateListener(StateListener listener) {
        std::lock_guard<std::mutex> lock(listener_mutex_);
        state_listener_ = listener;
    }

private:
    MusicPlayer();
    ~MusicPlayer();

    void HandlePlayFile(const std::string& path, bool loop);
    void HandlePlayUrl(const std::string& url, bool loop);
    void HandlePause();
    void HandleResume();
    void HandleStop();
    void HandleError(const char* msg);

    static void PlayTask(void* arg);

    void NotifyState(State state, float progress);

    std::atomic<State> state_{State::Idle};
    std::atomic<float> progress_{0.0f};
    std::atomic<SourceType> source_type_{SourceType::None};

    std::string current_file_;
    std::string current_url_;
    std::string last_error_;

    std::vector<std::string> current_playlist_;
    size_t current_playlist_index_ = 0;
    bool playlist_loop_ = false;

    TaskHandle_t play_task_ = nullptr;

    std::atomic<bool> decoder_registered_{false};

    volatile bool paused_ = false;
    volatile bool stop_requested_ = false;
    volatile bool loop_ = false;

    StateListener state_listener_;
    std::mutex listener_mutex_;
};

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
