#pragma once
/**
 * WebSocket 推流会话 — 每连接独立的实时音频处理
 *
 * 封装 capture_loop 前半段逻辑（Denoiser → VAD → StreamingASR → 垃圾过滤），
 * 供 WsVoiceServer 每连接创建一个实例。
 *
 * 完全复用现有类：Denoiser、EnergyVAD/AdaptiveVAD、StreamingASR
 */

#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "denoiser.h"
#include "vad.h"
#include "streaming_asr.h"
#include "utf8_utils.h"

class StreamingSession {
public:
    StreamingSession(const VADConfig& vad_cfg, const StreamingASRConfig& asr_cfg)
        : vad_cfg_(vad_cfg)
        , vad_(create_vad("adaptive", vad_cfg))
        , denoiser_(16000)
        , float_buf_(vad_cfg.frame_size_samples)
    {
        stream_asr_.initialize(asr_cfg);
    }

    ~StreamingSession() = default;

    StreamingSession(const StreamingSession&) = delete;
    StreamingSession& operator=(const StreamingSession&) = delete;

    /// 处理一帧原始 int16 PCM 音频 (16kHz mono)
    /// @return JSON 事件字符串 (partial / speech_end)，无事件时为空
    std::string process_audio_frame(const int16_t* raw_data, int n)
    {
        // 转换为 float [-1, 1]
        if (n > (int)float_buf_.size()) {
            float_buf_.resize(n);
        }
        for (int i = 0; i < n; ++i) {
            float_buf_[i] = raw_data[i] / 32768.0f;
        }

        // 音频预处理：去直流 + 噪声门
        denoiser_.process_frame(float_buf_.data(), n);

        // VAD 预处理缓冲区（保留语音开始前的音频）
        vad_->feed_pre_buffer(float_buf_.data(), n);

        // VAD 状态机
        VADState vad_state = vad_->process_frame(float_buf_.data(), n);

        // 流式 ASR：语音开始时启动新 utterance
        if (vad_state == VADState::SPEECH_START) {
            stream_asr_.start_utterance();
            partial_text_.clear();
        }

        // 语音进行中：喂帧 → 检查部分结果
        if (vad_->in_speech()) {
            stream_asr_.feed(float_buf_.data(), n);

            const char* p = stream_asr_.partial();
            if (p && p[0] != '\0') {
                std::string cur(p);
                // 过滤噪声幻觉（纯标点/短ASCII/日韩文）
                if (utf8::is_garbage_text(cur)) return "";
                if (cur != partial_text_) {
                    partial_text_ = cur;
                    nlohmann::json j;
                    j["type"] = "partial";
                    j["text"] = partial_text_;
                    return j.dump();
                }
            }
        }

        // 安全阀：语音超过 10 秒强制截断
        bool speech_timeout = false;
        if (vad_->in_speech() && vad_->speech_sample_count() > 10 * 16000) {
            speech_timeout = true;
        }

        // 语音段结束 → 最终识别 + 垃圾过滤
        if (vad_->segment_ready() || speech_timeout) {
            auto segment = vad_->pop_segment();

            // 最短 0.5 秒
            float duration = (float)segment.size() / 16000.0f;
            if (duration < 0.5f) {
                stream_asr_.cancel();
                return "";
            }

            final_text_ = stream_asr_.finalize();

            // 垃圾过滤
            if (is_garbage(segment, final_text_, vad_cfg_)) {
                final_text_.clear();
                return "";
            }

            segment_complete_ = true;

            nlohmann::json j;
            j["type"] = "speech_end";
            j["text"] = final_text_;
            j["duration"] = duration;
            return j.dump();
        }

        return "";
    }

    /// 客户端主动结束语音 (stream_end 消息)
    /// @return JSON 最终识别结果，无为 ""
    std::string force_end_utterance()
    {
        if (final_text_.empty() && !segment_complete_) {
            auto segment = vad_->pop_segment();
            if (segment.empty()) {
                stream_asr_.cancel();
                return "";
            }
            final_text_ = stream_asr_.finalize();
            if (final_text_.empty() || is_garbage(segment, final_text_, vad_cfg_)) {
                final_text_.clear();
                return "";
            }
            segment_complete_ = true;

            float duration = (float)segment.size() / 16000.0f;
            nlohmann::json j;
            j["type"] = "speech_end";
            j["text"] = final_text_;
            j["duration"] = duration;
            return j.dump();
        }
        return "";
    }

    bool in_speech() const { return vad_->in_speech(); }
    const std::string& final_text() const { return final_text_; }
    bool segment_complete() const { return segment_complete_; }

    void reset()
    {
        denoiser_.reset();
        vad_->reset();
        stream_asr_.cancel();
        float_buf_.assign(float_buf_.size(), 0.0f);
        partial_text_.clear();
        final_text_.clear();
        segment_complete_ = false;
    }

    /// 提取自 capture_loop 的垃圾过滤逻辑（音频能量 + 文本内容双重检测）
    static bool is_garbage(const std::vector<float>& segment,
                           const std::string& text,
                           const VADConfig& vad_cfg)
    {
        // 文本级检测（纯标点/短ASCII/日韩文 → 肯定是噪声幻觉）
        if (utf8::is_garbage_text(text)) return true;

        // 计算平均 RMS 能量
        float sum_sq = 0.0f;
        for (auto s : segment) sum_sq += s * s;
        float avg_rms = std::sqrt(sum_sq / segment.size());

        // 能量极低 (< 0.005) + 短文本 → 回声/静音幻觉
        if (avg_rms < 0.005f && text.size() <= 10) return true;

        // 短文本 + 低于 min_energy_threshold → SenseVoice 静音幻觉
        if (avg_rms < vad_cfg.min_energy_threshold && text.size() <= 6) return true;

        return false;
    }

private:
    VADConfig vad_cfg_;
    std::unique_ptr<EnergyVAD> vad_;
    Denoiser denoiser_;
    StreamingASR stream_asr_;

    std::vector<float> float_buf_;
    std::string partial_text_;
    std::string final_text_;
    bool segment_complete_ = false;
};
