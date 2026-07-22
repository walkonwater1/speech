/**
 * 语音活动检测器 (VAD) 实现
 *
 * EnergyVAD:   固定 RMS 阈值
 * AdaptiveVAD: 自适应噪声基线 + 动态阈值
 */

#include "vad.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// ════════════════════════════════════════════════════════════════
// EnergyVAD（固定阈值）
// ════════════════════════════════════════════════════════════════

EnergyVAD::EnergyVAD(const VADConfig& cfg)
    : cfg_(cfg)
{
    int pre_buf_size = cfg_.pre_speech_frames * cfg_.frame_size_samples;
    pre_buffer_.resize(pre_buf_size, 0.0f);
    pre_buffer_pos_ = 0;
}

float EnergyVAD::compute_rms(const float* samples, int n) const
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / n);
}

EnergyVAD::FrameDecision EnergyVAD::decide_frame(const float* samples, int n)
{
    float rms = compute_rms(samples, n);
    return {rms > cfg_.energy_threshold, rms};
}

VADState EnergyVAD::process_frame(const float* samples, int n)
{
    FrameDecision d = decide_frame(samples, n);

    segment_ready_ = false;

    switch (state_) {
    case VADState::SILENCE:
        // 冷却期：语音段刚结束，强制静音 N 帧防止回声误触发
        if (cooldown_remaining_ > 0) {
            cooldown_remaining_--;
            return VADState::SILENCE;
        }
        if (d.is_speech) {
            speech_frames_ = 1;
            silence_frames_ = 0;
            state_ = VADState::SPEECH_START;

            // 从环形缓冲区按时间顺序取出预录数据
            int pre_size = (int)pre_buffer_.size();
            speech_buffer_.clear();
            speech_buffer_.reserve(pre_size + n * 10);
            for (int i = 0; i < pre_size; ++i) {
                int idx = (pre_buffer_pos_ + i) % pre_size;
                speech_buffer_.push_back(pre_buffer_[idx]);
            }
            speech_buffer_.insert(speech_buffer_.end(), samples, samples + n);
            return VADState::SPEECH_START;
        }
        return VADState::SILENCE;

    case VADState::SPEECH_START:
    case VADState::SPEECH_ONGOING:
        // 安全上限：防止无限增长导致 OOM
        if (speech_buffer_.size() + n > (size_t)cfg_.max_speech_samples) {
            segment_ready_ = true;
            state_ = VADState::SILENCE;
            speech_frames_ = 0;
            silence_frames_ = 0;
            return VADState::SPEECH_END;
        }

        if (d.is_speech) {
            speech_frames_++;
            silence_frames_ = 0;
            state_ = VADState::SPEECH_ONGOING;
            speech_buffer_.insert(speech_buffer_.end(), samples, samples + n);
            return VADState::SPEECH_ONGOING;
        } else {
            silence_frames_++;
            speech_buffer_.insert(speech_buffer_.end(), samples, samples + n);

            if (silence_frames_ >= cfg_.min_silence_frames
                && speech_frames_ >= cfg_.min_speech_frames)
            {
                segment_ready_ = true;
                state_ = VADState::SILENCE;
                return VADState::SPEECH_END;
            }
            return VADState::SPEECH_ONGOING;
        }

    default:
        return VADState::SILENCE;
    }
}

bool EnergyVAD::in_speech() const
{
    return state_ == VADState::SPEECH_START || state_ == VADState::SPEECH_ONGOING;
}

std::vector<float> EnergyVAD::pop_segment()
{
    segment_ready_ = false;
    auto result = std::move(speech_buffer_);
    speech_buffer_.clear();
    speech_frames_ = 0;
    silence_frames_ = 0;
    cooldown_remaining_ = cfg_.silence_cooldown_frames;  // 启动冷却
    state_ = VADState::SILENCE;
    return result;
}

void EnergyVAD::feed_pre_buffer(const float* samples, int n)
{
    int pre_size = (int)pre_buffer_.size();
    if (pre_size == 0) return;

    for (int i = 0; i < n; ++i) {
        pre_buffer_[pre_buffer_pos_] = samples[i];
        pre_buffer_pos_ = (pre_buffer_pos_ + 1) % pre_size;
    }
}

void EnergyVAD::reset()
{
    state_ = VADState::SILENCE;
    speech_frames_ = 0;
    silence_frames_ = 0;
    cooldown_remaining_ = 0;
    segment_ready_ = false;
    speech_buffer_.clear();
    pre_buffer_pos_ = 0;
    std::fill(pre_buffer_.begin(), pre_buffer_.end(), 0.0f);
}

// ════════════════════════════════════════════════════════════════
// AdaptiveVAD（自适应噪声基线）
// ════════════════════════════════════════════════════════════════

AdaptiveVAD::AdaptiveVAD(const VADConfig& cfg)
    : EnergyVAD(cfg)
{}

EnergyVAD::FrameDecision AdaptiveVAD::decide_frame(const float* samples, int n)
{
    float rms = compute_rms(samples, n);

    // 只在静音时更新噪声基线
    if (state_ == VADState::SILENCE) {
        float alpha = cfg_.noise_update_rate;
        if (!noise_floor_initialized_) {
            noise_floor_ = rms;
            noise_floor_initialized_ = true;
        } else {
            noise_floor_ = (1.0f - alpha) * noise_floor_ + alpha * rms;
        }
    }

    float threshold = noise_floor_initialized_
        ? std::max(noise_floor_ * cfg_.adaptive_factor, cfg_.min_energy_threshold)
        : cfg_.energy_threshold;

    return {rms > threshold, rms};
}
