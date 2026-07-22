#pragma once
/**
 * 轻量级音频预处理 — 去直流偏置 + 自适应噪声门
 *
 * 解决廉价 USB 麦克风的常见问题:
 *   1. DC offset — 信号基线偏离零点，影响 RMS 计算和 ASR 精度
 *   2. 环境底噪 — 风扇、空调、电流声等持续低频噪声
 *
 * 处理流程:
 *   原始帧 → DC 高通滤波 → 噪声门衰减 → 输出干净帧
 *
 * 使用方式（在 capture_loop 中，int16→float 之后、VAD 之前）:
 *   Denoiser denoiser(16000);
 *   denoiser.process_frame(float_buf.data(), frame_samples);
 */

#include <cmath>
#include <algorithm>
#include <vector>

class Denoiser {
public:
    explicit Denoiser(int sample_rate = 16000, float gate_suppression_db = -20.0f)
        : sample_rate_(sample_rate)
    {
        // DC 阻断器系数 (一阶高通, 截止频率 ~80Hz @16kHz)
        dc_alpha_ = std::exp(-2.0f * 3.14159265f * 80.0f / sample_rate_);
        dc_prev_in_  = 0.0f;
        dc_prev_out_ = 0.0f;

        // 噪声门: 将抑制分贝转为线性增益
        gate_gain_ = std::pow(10.0f, gate_suppression_db / 20.0f);  // -20dB → 0.1
    }

    /// 原地处理一帧音频
    /// @param samples  帧样本 (float, [-1, 1])
    /// @param n        样本数
    void process_frame(float* samples, int n)
    {
        // ── 第 1 步: DC 阻断（一阶高通滤波）────────────────
        for (int i = 0; i < n; ++i) {
            float x = samples[i];
            // y[n] = x[n] - x[n-1] + α * y[n-1]
            float y = x - dc_prev_in_ + dc_alpha_ * dc_prev_out_;
            dc_prev_in_  = x;
            dc_prev_out_ = y;
            samples[i] = y;
        }

        // ── 第 2 步: 计算 RMS ────────────────────────────
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) {
            sum += samples[i] * samples[i];
        }
        float rms = std::sqrt(sum / n);

        // ── 第 3 步: 更新噪声基线（EMA）───────────────────
        if (!noise_floor_init_) {
            noise_floor_ = rms;
            noise_floor_init_ = true;
        } else if (rms < noise_floor_ * 2.0f) {
            // 只在「可能是静音」时更新（避免语音拉高噪声基线）
            noise_floor_ = 0.98f * noise_floor_ + 0.02f * rms;
        }

        // ── 第 4 步: 噪声门 ──────────────────────────────
        // 帧 RMS 低于阈值 → 大幅衰减（推开底噪）
        float gate_threshold = noise_floor_ * 3.0f;
        if (rms < gate_threshold) {
            for (int i = 0; i < n; ++i) {
                samples[i] *= gate_gain_;
            }
        }
        // 高于阈值 → 原样通过（保留语音细节）
    }

    /// 噪声基线 RMS（调试用）
    float noise_floor() const { return noise_floor_; }

    /// 重置状态
    void reset()
    {
        dc_prev_in_  = 0.0f;
        dc_prev_out_ = 0.0f;
        noise_floor_init_ = false;
        noise_floor_ = 0.0f;
    }

private:
    int   sample_rate_;

    // DC 阻断器状态
    float dc_alpha_;
    float dc_prev_in_;
    float dc_prev_out_;

    // 噪声门状态
    float noise_floor_      = 0.0f;
    bool  noise_floor_init_ = false;
    float gate_gain_        = 0.1f;   // 默认 -20dB
};
