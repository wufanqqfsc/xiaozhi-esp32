#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace drum {

/** 鼓件 ID（按 8 扇区 + 中心映射） */
enum class Piece : uint8_t {
    KICK = 0,         // 底鼓（中心）
    SNARE,            // 军鼓
    HIHAT_CLOSED,     // 闭镲
    HIHAT_OPEN,       // 开镲
    TOM_HI,           // 高通鼓
    TOM_MID,          // 中通鼓
    CRASH,            // 强音钹
    RIDE,             // 叮叮镲
    COUNT = 8
};

/** 触发回调（用于 UI 视觉反馈） */
using TriggerCallback = std::function<void(Piece)>;

/**
 * 架子鼓合成器：单例，挂在 Application 生命周期内。
 * 核心：触摸 → 解码对应 ogg → 入播放队列 → 喇叭发声。
 * 设计目标：低延迟（< 30ms 首次，< 5ms 命中缓存），与语音系统 Idle 态共存。
 */
class DrumSynth {
public:
    static DrumSynth& GetInstance();

    /** 初始化（指向 ogg 数据 + 注册回调钩子） */
    void Init();

    /** 触发一个鼓声
     *  - piece: 鼓件 ID
     *  - velocity: 力度 0-127（暂未使用，预留）
     * 返回: 是否成功入队
     */
    bool Trigger(Piece piece, uint8_t velocity = 100);

    /** 停止所有正在播放的鼓声（清空队列） */
    void StopAll();

    /** 进入/退出架子鼓模式（用于唤醒词控制） */
    void SetActive(bool active);
    bool IsActive() const { return active_; }

    /** 注册 UI 反馈回调（按下闪烁等） */
    void SetTriggerCallback(TriggerCallback cb) { trigger_callback_ = std::move(cb); }

private:
    DrumSynth() = default;
    bool EnsureOggAvailable(Piece piece);
    void CallTriggerCallback(Piece p);

    bool initialized_ = false;
    bool active_ = false;
    TriggerCallback trigger_callback_;
};

}  // namespace drum
