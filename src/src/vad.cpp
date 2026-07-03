/**
 * 基于能量的语音活动检测器 (Energy-based VAD)
 *
 * Python 对应: run_realtime.py → check_vad_activity() (webrtcvad)
 *
 * 原理:
 *   计算每帧音频的 RMS（均方根），超过阈值 → 说话中；低于阈值 → 静音。
 *   通过 min_speech_frames / min_silence_frames 实现去抖（debounce）。
 */

#include "vad.h"
#include <cmath>
#include <algorithm>
#include <cstring>

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

EnergyVAD::State EnergyVAD::process_frame(const float* samples, int n)
{
    float rms = compute_rms(samples, n);
    bool is_speech_frame = (rms > cfg_.energy_threshold);

    segment_ready_ = false;

    switch (state_) {
    case SILENCE:
        if (is_speech_frame) {
            speech_frames_ = 1;
            silence_frames_ = 0;
            state_ = SPEECH_START;

            // 把语音开始前的预录音频 prepend 到 speech buffer
            int pre_size = (int)pre_buffer_.size();
            speech_buffer_.clear();
            speech_buffer_.reserve(pre_size + n * 10);
            // 从环形缓冲区按时间顺序取出预录数据
            for (int i = 0; i < pre_size; ++i) {
                int idx = (pre_buffer_pos_ + i) % pre_size;
                speech_buffer_.push_back(pre_buffer_[idx]);
            }
            speech_buffer_.insert(speech_buffer_.end(), samples, samples + n);
            return SPEECH_START;
        }
        return SILENCE;

    case SPEECH_START:
    case SPEECH_ONGOING:
        if (is_speech_frame) {
            speech_frames_++;
            silence_frames_ = 0;
            state_ = SPEECH_ONGOING;
            speech_buffer_.insert(speech_buffer_.end(), samples, samples + n);
            return SPEECH_ONGOING;
        } else {
            silence_frames_++;
            speech_buffer_.insert(speech_buffer_.end(), samples, samples + n);

            // 静音帧足够多 → 语音结束
            if (silence_frames_ >= cfg_.min_silence_frames
                && speech_frames_ >= cfg_.min_speech_frames)
            {
                segment_ready_ = true;
                state_ = SILENCE;
                return SPEECH_END;
            }
            return SPEECH_ONGOING;
        }

    default:
        return SILENCE;
    }
}

bool EnergyVAD::in_speech() const
{
    return state_ == SPEECH_START || state_ == SPEECH_ONGOING;
}

std::vector<float> EnergyVAD::pop_segment()
{
    segment_ready_ = false;
    auto result = std::move(speech_buffer_);
    speech_buffer_.clear();
    speech_frames_ = 0;
    silence_frames_ = 0;
    state_ = SILENCE;
    return result;
}

void EnergyVAD::feed_pre_buffer(const float* samples, int n)
{
    // 环形写入
    int pre_size = (int)pre_buffer_.size();
    if (pre_size == 0) return;

    for (int i = 0; i < n; ++i) {
        pre_buffer_[pre_buffer_pos_] = samples[i];
        pre_buffer_pos_ = (pre_buffer_pos_ + 1) % pre_size;
    }
}

void EnergyVAD::reset()
{
    state_ = SILENCE;
    speech_frames_ = 0;
    silence_frames_ = 0;
    segment_ready_ = false;
    speech_buffer_.clear();
    pre_buffer_pos_ = 0;
    std::fill(pre_buffer_.begin(), pre_buffer_.end(), 0.0f);
}
