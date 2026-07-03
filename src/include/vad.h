#pragma once
/**
 * 基于能量的语音活动检测器 (Energy-based VAD)
 *
 * 无需模型，通过 RMS 能量阈值判断是否有人说话。
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

struct VADConfig {
    int   sample_rate        = 16000;
    float energy_threshold   = 0.003f;   // RMS 阈值，低于此值视为静音
    int   min_speech_frames  = 8;        // 最小语音帧数 (~160ms @20ms/frame)
    int   min_silence_frames = 30;       // 静音多少帧后判断语音结束 (~600ms)
    int   pre_speech_frames  = 15;       // 保留语音开始前的音频帧 (~300ms)
    int   frame_size_samples = 320;      // 每帧采样数 (20ms @16kHz)
};

class EnergyVAD {
public:
    enum State { SILENCE = 0, SPEECH_START = 1, SPEECH_ONGOING = 2, SPEECH_END = 3 };

    explicit EnergyVAD(const VADConfig& cfg = VADConfig());

    /// 处理一帧音频 (16kHz, mono, float, 归一化到 [-1, 1])
    /// @return 当前 VAD 状态
    State process_frame(const float* samples, int n);

    /// 是否正在说话中
    bool in_speech() const;

    /// 是否刚完成一个语音段（可取出）
    bool segment_ready() const { return segment_ready_; }

    /// 取出已完成的语音段（含语音前的预录音频）
    /// 调用后自动 reset
    std::vector<float> pop_segment();

    /// 添加音频到语音前环形缓冲区
    void feed_pre_buffer(const float* samples, int n);

    /// 重置状态
    void reset();

    /// 获取当前累积的语音采样数
    int speech_sample_count() const { return (int)speech_buffer_.size(); }

private:
    VADConfig cfg_;
    State state_ = SILENCE;

    int speech_frames_  = 0;
    int silence_frames_ = 0;
    bool segment_ready_ = false;

    std::vector<float> speech_buffer_;          // 当前语音段样本
    std::vector<float> pre_buffer_;             // 环形缓冲区
    int pre_buffer_pos_ = 0;

    float compute_rms(const float* samples, int n) const;
};
