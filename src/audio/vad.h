#pragma once
/**
 * 语音活动检测器 (VAD) — 两种实现
 *
 * EnergyVAD:   固定 RMS 阈值（简单快速，安静环境可用）
 * AdaptiveVAD: 自适应噪声基线 + 动态阈值（推荐，自动适应环境噪声）
 *
 * 用于:
 *   1. 持续监听 — 检测语音段的起止
 *   2. 打断检测 — TTS 播放期间检测到语音 → 立即停止播放
 *
 * Python 对应: run_realtime.py → check_vad_activity() (webrtcvad)
 */

#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

struct VADConfig {
    int   sample_rate        = 16000;
    float energy_threshold   = 0.003f;   // EnergyVAD: 固定 RMS 阈值
    int   min_speech_frames  = 8;        // 最小语音帧数 (~160ms @20ms/frame)
    int   min_silence_frames = 30;       // 静音多少帧后判断语音结束 (~600ms)
    int   pre_speech_frames  = 15;       // 保留语音开始前的音频帧 (~300ms)
    int   frame_size_samples = 320;      // 每帧采样数 (20ms @16kHz)

    // AdaptiveVAD 专用
    float adaptive_factor      = 3.0f;   // 阈值 = 噪声基线 × factor
    float noise_update_rate    = 0.02f;  // 噪声基线 EMA 更新速率
    float min_energy_threshold = 0.002f; // 绝对最小能量阈值，低于此值永不为语音

    // 语音段结束后强制静音帧数（防止回声/残响被误识别为新语音）
    int silence_cooldown_frames = 25;    // 500ms @20ms/frame

    // 安全限制
    int max_speech_samples = 480000;     // 硬上限 30s@16kHz，防止 OOM
};

// ── 公共枚举 ──────────────────────────────────────────

enum class VADState { SILENCE = 0, SPEECH_START = 1, SPEECH_ONGOING = 2, SPEECH_END = 3 };

// ── EnergyVAD（固定阈值）───────────────────────────────

class EnergyVAD {
public:
    explicit EnergyVAD(const VADConfig& cfg = VADConfig());

    /// 处理一帧音频 (16kHz, mono, float, 归一化到 [-1, 1])
    /// 状态机逻辑在此处理；子类通过 decide_frame() 覆写阈值策略
    virtual VADState process_frame(const float* samples, int n);

    bool in_speech() const;
    bool segment_ready() const { return segment_ready_; }

    std::vector<float> pop_segment();
    void feed_pre_buffer(const float* samples, int n);
    void reset();
    int speech_sample_count() const { return (int)speech_buffer_.size(); }

protected:
    struct FrameDecision {
        bool is_speech;
        float rms;
    };

    VADConfig cfg_;
    VADState state_ = VADState::SILENCE;

    int speech_frames_   = 0;
    int silence_frames_  = 0;
    int cooldown_remaining_ = 0;  // 语音段结束后的强制静音帧数
    bool segment_ready_  = false;

    std::vector<float> speech_buffer_;
    std::vector<float> pre_buffer_;
    int pre_buffer_pos_ = 0;

    float compute_rms(const float* samples, int n) const;

    /// 子类覆写此方法以改变语音/静音判定逻辑
    virtual FrameDecision decide_frame(const float* samples, int n);
};

// ── AdaptiveVAD（自适应噪声基线 — 推荐）─────────────────

class AdaptiveVAD : public EnergyVAD {
public:
    explicit AdaptiveVAD(const VADConfig& cfg = VADConfig());

    /// 当前噪声基线 RMS 值（用于调试）
    float noise_floor() const { return noise_floor_; }

protected:
    FrameDecision decide_frame(const float* samples, int n) override;

private:
    float noise_floor_ = 0.0f;
    bool  noise_floor_initialized_ = false;
};

// ── 工厂函数 ──────────────────────────────────────────

/// 根据 backend 名称创建 VAD 实例
inline std::unique_ptr<EnergyVAD> create_vad(const std::string& backend, const VADConfig& cfg)
{
    if (backend == "adaptive") {
        return std::make_unique<AdaptiveVAD>(cfg);
    }
    return std::make_unique<EnergyVAD>(cfg);
}
