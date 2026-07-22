#pragma once
/**
 * 语音情感分析器 (Voice Emotion Analyzer)
 *
 * Layer 3.3: 语音交互深化 — 从声学特征识别情绪
 *
 * 之前 (Layer 3.1): 纯文本关键词 → 情感（"难过" → SAD）
 *   问题: 用户用平静语气说"我好难过" → 检测为 SAD → 过度温柔回复
 *   问题: 用户用兴奋语气说"今天天气" → 检测为 NEUTRAL → 平淡回复
 *
 * 现在 (Layer 3.3): 声学特征 + 文本 = 融合情感
 *   - 语音声学信号: 音高、能量、语速、音质
 *   - 文本语义信号: 关键词匹配（已有 ProsodyController）
 *   - 融合策略: 声学为主（语气不会说谎），文本为辅（语义确认）
 *
 * 声学特征 → 情绪映射:
 *
 *   特征           高值              低值
 *   ─────────────────────────────────────────
 *   基频 (pitch)   兴奋/愤怒          悲伤/疲劳
 *   能量 (energy)  激动/愤怒          平静/悲伤
 *   语速 (rate)    急迫/兴奋          沉思/悲伤
 *   基频变化 (var) 情绪化/生动        单调/压抑
 *   过零率 (zcr)   清音/摩擦音        浊音/元音
 *
 * 融合决策矩阵:
 *
 *   声学\文本    NEUTRAL   HAPPY    SAD     URGENT
 *   ─────────────────────────────────────────────
 *   EXCITED      HAPPY    HAPPY    HAPPY   URGENT
 *   SAD          SAD      NEUTRAL  SAD     URGENT
 *   ANGRY        URGENT   HAPPY    SAD     URGENT
 *   CALM         NEUTRAL  HAPPY    EMPATH  NEUTRAL
 *   NEUTRAL      文本      文本      文本    文本
 *
 * 算法选择:
 *   不使用深度学习（无可用模型），采用经典信号处理:
 *   - 自相关法基频检测 (autocorrelation pitch detection)
 *   - RMS 能量计算
 *   - 过零率 (ZCR)
 *   - 能量包络峰值 → 语速估计
 *   这些特征虽简单，但能可靠区分基本的情绪维度
 */

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <cstring>
#include <cstdint>

// ── 声学特征 ──────────────────────────────────────────

struct AcousticFeatures {
    float avg_pitch_hz     = 0.0f;   // 平均基频 (Hz)，典型: 男85-180, 女165-255
    float pitch_variance   = 0.0f;   // 基频方差（情绪化程度）
    float avg_energy       = 0.0f;   // 平均 RMS 能量
    float energy_variance  = 0.0f;   // 能量方差
    float zero_cross_rate  = 0.0f;   // 平均过零率
    float speaking_rate    = 0.0f;   // 估计语速（音节/秒），中文典型: 3-6
    float voice_ratio      = 0.0f;   // 有声帧比例 (0-1)
    int   voiced_frames    = 0;      // 有声帧数
    int   total_frames     = 0;      // 总帧数
};

// ── 声学情绪类型 ──────────────────────────────────────

enum class VoiceEmotion {
    NEUTRAL,   // 中性
    EXCITED,   // 兴奋（高音高+高能量+快语速）
    SAD,       // 悲伤（低音高+低能量+慢语速）
    ANGRY,     // 愤怒（高能量+高音高变化）
    CALM,      // 平静（低能量+慢语速+稳定音高）
};

// ── 分析结果 ──────────────────────────────────────────

struct VoiceEmotionResult {
    VoiceEmotion  emotion    = VoiceEmotion::NEUTRAL;
    float         confidence = 0.0f;   // 置信度 0-1
    AcousticFeatures features;
    std::string   label;
    std::string   detail;              // 诊断信息

    /// 映射为 TTS 韵律层使用的 EmotionTone
    /// NEUTRAL/EXCITED/CALM/SAD/ANGRY → NEUTRAL/HAPPY/SAD/URGENT/EMPATHETIC
    int to_emotion_tone_id() const {
        switch (emotion) {
        case VoiceEmotion::EXCITED: return 1;  // HAPPY
        case VoiceEmotion::SAD:     return 2;  // SAD
        case VoiceEmotion::CALM:    return 3;  // EMPATHETIC
        case VoiceEmotion::ANGRY:   return 4;  // URGENT
        default:                    return 0;  // NEUTRAL
        }
    }
};

// ── 语音情感分析器 ────────────────────────────────────

class VoiceEmotionAnalyzer {
public:
    struct Config {
        int   sample_rate      = 16000;
        float frame_ms         = 25.0f;   // 帧长 (ms)
        float frame_shift_ms   = 10.0f;   // 帧移 (ms)

        // 基频范围 (Hz) — 涵盖男女老少
        float min_pitch = 60.0f;
        float max_pitch = 400.0f;

        // 有声/无声阈值
        float voicing_rms_threshold = 0.002f;  // RMS 低于此值视为无声
        float voicing_zcr_max       = 0.5f;    // ZCR 高于此值可能为清音

        // 情绪分类阈值
        float high_pitch_threshold    = 230.0f;  // 高于此 → excited
        float low_pitch_threshold     = 100.0f;  // 低于此 → sad/calm
        float high_energy_threshold   = 0.03f;   // 高于此 → excited/angry
        float low_energy_threshold    = 0.008f;  // 低于此 → sad/calm
        float fast_rate_threshold     = 4.0f;    // 音节/秒 高于此 → excited/angry
        float slow_rate_threshold     = 2.0f;    // 音节/秒 低于此 → sad/calm
        float high_pitch_var_threshold = 2000.0f; // 基频方差 高于此 + 高能量 → angry
    };

    VoiceEmotionAnalyzer() = default;
    explicit VoiceEmotionAnalyzer(const Config& cfg) : cfg_(cfg) {}

    /// 从音频样本分析情绪
    /// @param samples   float 样本, mono, [-1, 1]
    /// @param n         样本数
    /// @param sample_rate 采样率（覆盖配置中的值）
    VoiceEmotionResult analyze(const float* samples, int n,
                               int sample_rate = 0) const
    {
        VoiceEmotionResult result;
        if (!samples || n < sample_rate / 4) {  // 至少 0.25 秒
            result.label = "音频太短";
            return result;
        }

        int sr = sample_rate > 0 ? sample_rate : cfg_.sample_rate;

        // ── 1. 提取声学特征 ──────────────────────────────
        result.features = extract_features(samples, n, sr);
        auto& f = result.features;

        // ── 2. 基于特征的启发式情绪分类 ───────────────────
        int scores[5] = {0, 0, 0, 0, 0};  // NEUTRAL, EXCITED, SAD, ANGRY, CALM

        // 基频维度
        if (f.avg_pitch_hz > cfg_.high_pitch_threshold) {
            scores[(int)VoiceEmotion::EXCITED] += 2;
            scores[(int)VoiceEmotion::ANGRY]   += 1;
        } else if (f.avg_pitch_hz > 0 && f.avg_pitch_hz < cfg_.low_pitch_threshold) {
            scores[(int)VoiceEmotion::SAD]  += 2;
            scores[(int)VoiceEmotion::CALM] += 1;
        }

        // 能量维度
        if (f.avg_energy > cfg_.high_energy_threshold) {
            scores[(int)VoiceEmotion::EXCITED] += 2;
            scores[(int)VoiceEmotion::ANGRY]   += 2;
        } else if (f.avg_energy < cfg_.low_energy_threshold) {
            scores[(int)VoiceEmotion::SAD]  += 2;
            scores[(int)VoiceEmotion::CALM] += 2;
        }

        // 语速维度
        if (f.speaking_rate > cfg_.fast_rate_threshold) {
            scores[(int)VoiceEmotion::EXCITED] += 1;
            scores[(int)VoiceEmotion::ANGRY]   += 1;
        } else if (f.speaking_rate > 0 && f.speaking_rate < cfg_.slow_rate_threshold) {
            scores[(int)VoiceEmotion::SAD]  += 1;
            scores[(int)VoiceEmotion::CALM] += 2;
        }

        // 基频变化维度（区分愤怒 vs 兴奋）
        // 愤怒需要 高能量 + 高基频变异；单纯高变异可能是噪声
        if (f.pitch_variance > cfg_.high_pitch_var_threshold &&
            f.avg_energy > cfg_.high_energy_threshold) {
            scores[(int)VoiceEmotion::ANGRY]   += 3;  // 高能量+高变异 → 愤怒
            scores[(int)VoiceEmotion::EXCITED] -= 1;
        } else if (f.pitch_variance > 0 && f.pitch_variance < 300.0f) {
            scores[(int)VoiceEmotion::CALM] += 2;      // 低变异 → 平静
        }

        // 有声比例维度
        if (f.voice_ratio > 0.85f) {
            scores[(int)VoiceEmotion::EXCITED] += 1;
        } else if (f.voice_ratio > 0.0f && f.voice_ratio < 0.35f) {
            scores[(int)VoiceEmotion::SAD] += 1;
        }

        // 中性分数
        scores[(int)VoiceEmotion::NEUTRAL] = 2;

        // ── 3. 选最高分 ──────────────────────────────────
        int best = 0;
        for (int i = 0; i < 5; ++i) {
            if (scores[i] > scores[best]) best = i;
        }

        result.emotion = (VoiceEmotion)best;

        // 置信度: 最高分占总分的比例
        int total_score = 0;
        for (int i = 0; i < 5; ++i) total_score += scores[i];
        result.confidence = total_score > 0
            ? (float)scores[best] / (float)total_score : 0.0f;

        // ── 4. 生成标签和诊断信息 ─────────────────────────
        switch (result.emotion) {
        case VoiceEmotion::EXCITED: result.label = "兴奋";   break;
        case VoiceEmotion::SAD:     result.label = "悲伤";   break;
        case VoiceEmotion::ANGRY:   result.label = "愤怒";   break;
        case VoiceEmotion::CALM:    result.label = "平静";   break;
        default:                    result.label = "中性";   break;
        }

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "音高=%.0fHz σ²=%.0f 能量=%.4f 语速=%.1f音节/秒 有声=%.0f%%",
            f.avg_pitch_hz, f.pitch_variance, f.avg_energy,
            f.speaking_rate, f.voice_ratio * 100.0f);
        result.detail = buf;

        return result;
    }

private:
    Config cfg_;

    // ── 帧分析结构 ──────────────────────────────────────

    struct FrameFeatures {
        float rms       = 0.0f;
        float zcr       = 0.0f;   // 过零率
        float pitch     = 0.0f;   // 基频 (Hz), 0 = 无声
        bool  voiced    = false;
        int   peak_count = 0;     // 能量包络峰值计数
    };

    // ── 特征提取主函数 ──────────────────────────────────

    AcousticFeatures extract_features(const float* samples, int n,
                                       int sr) const
    {
        int frame_len   = (int)(cfg_.frame_ms * sr / 1000.0f);
        int frame_shift = (int)(cfg_.frame_shift_ms * sr / 1000.0f);
        int num_frames  = (n - frame_len) / frame_shift + 1;
        if (num_frames < 1) num_frames = 1;

        std::vector<FrameFeatures> frames(num_frames);

        std::vector<float> frame(frame_len);
        float total_rms  = 0.0f;
        float total_zcr  = 0.0f;
        int   voiced_cnt = 0;
        float pitch_sum  = 0.0f;
        int   pitch_cnt  = 0;

        // 能量包络（用于语速估计）
        std::vector<float> energy_envelope(num_frames);

        for (int i = 0; i < num_frames; ++i) {
            int offset = i * frame_shift;
            int actual_len = std::min(frame_len, n - offset);
            if (actual_len < frame_len / 2) break;

            auto& ff = frames[i];

            // 复制帧数据（带汉明窗）
            for (int j = 0; j < actual_len; ++j) {
                float w = 0.54f - 0.46f * std::cos(2.0f * M_PI * j / (actual_len - 1));
                frame[j] = samples[offset + j] * w;
            }

            // RMS 能量
            float sum_sq = 0.0f;
            for (int j = 0; j < actual_len; ++j) {
                sum_sq += frame[j] * frame[j];
            }
            ff.rms = std::sqrt(sum_sq / actual_len);
            total_rms += ff.rms;
            energy_envelope[i] = ff.rms;

            // 过零率
            int zc = 0;
            for (int j = 1; j < actual_len; ++j) {
                if ((frame[j] >= 0.0f) != (frame[j-1] >= 0.0f)) {
                    zc++;
                }
            }
            ff.zcr = (float)zc / actual_len;
            total_zcr += ff.zcr;

            // 有声判定
            ff.voiced = (ff.rms > cfg_.voicing_rms_threshold &&
                         ff.zcr < cfg_.voicing_zcr_max);

            if (ff.voiced) {
                voiced_cnt++;

                // 基频检测（自相关法）
                ff.pitch = detect_pitch(frame.data(), actual_len, sr);
                if (ff.pitch > 0.0f) {
                    pitch_sum += ff.pitch;
                    pitch_cnt++;
                }
            }
        }

        // ── 汇总统计 ──────────────────────────────────────
        AcousticFeatures af;
        af.total_frames = num_frames;
        af.voiced_frames = voiced_cnt;
        af.avg_energy    = num_frames > 0 ? total_rms / num_frames : 0.0f;
        af.zero_cross_rate = num_frames > 0 ? total_zcr / num_frames : 0.0f;
        af.voice_ratio   = num_frames > 0 ? (float)voiced_cnt / num_frames : 0.0f;

        // 平均基频
        af.avg_pitch_hz = pitch_cnt > 0 ? pitch_sum / pitch_cnt : 0.0f;

        // 基频方差
        if (pitch_cnt > 1) {
            float pitch_mean = af.avg_pitch_hz;
            float var_sum = 0.0f;
            int var_cnt = 0;
            for (auto& ff : frames) {
                if (ff.voiced && ff.pitch > 0.0f) {
                    float d = ff.pitch - pitch_mean;
                    var_sum += d * d;
                    var_cnt++;
                }
            }
            af.pitch_variance = var_sum / var_cnt;
        }

        // 能量方差
        {
            float mean_e = af.avg_energy;
            float var_sum = 0.0f;
            for (int i = 0; i < num_frames; ++i) {
                float d = energy_envelope[i] - mean_e;
                var_sum += d * d;
            }
            af.energy_variance = var_sum / num_frames;
        }

        // 语速估计（能量包络峰值法）
        // 对能量包络做平滑 → 找峰值 → 每个峰值 ≈ 一个音节
        af.speaking_rate = estimate_speaking_rate(
            energy_envelope.data(), num_frames, sr, frame_shift);

        return af;
    }

    // ── 基频检测（自相关法）───────────────────────────────

    float detect_pitch(const float* frame, int n, int sr) const
    {
        // 搜索范围对应的 lag
        int min_lag = sr / cfg_.max_pitch;  // e.g., 16000/400 = 40
        int max_lag = sr / cfg_.min_pitch;  // e.g., 16000/60  = 266
        if (max_lag >= n) max_lag = n - 1;
        if (min_lag < 1) min_lag = 1;
        if (min_lag >= max_lag) return 0.0f;

        // 计算自相关 R(k) = Σ x[i] * x[i+k]
        float best_r = -1.0f;
        int best_lag = min_lag;

        for (int k = min_lag; k < max_lag; ++k) {
            float r = 0.0f;
            for (int i = 0; i < n - k; ++i) {
                r += frame[i] * frame[i + k];
            }
            // 归一化
            if (r > best_r) {
                best_r = r;
                best_lag = k;
            }
        }

        // R(0) 用于归一化
        float r0 = 0.0f;
        for (int i = 0; i < n; ++i) r0 += frame[i] * frame[i];

        if (r0 <= 0.0f) return 0.0f;

        float confidence = best_r / r0;

        // 自相关置信度太低 → 可能不是周期信号
        if (confidence < 0.5f) return 0.0f;

        return (float)sr / best_lag;
    }

    // ── 语速估计（能量包络峰值）─────────────────────────

    float estimate_speaking_rate(const float* envelope, int n,
                                  int sr, int frame_shift) const
    {
        if (n < 3) return 0.0f;

        // 平滑能量包络
        std::vector<float> smoothed(n);
        for (int i = 0; i < n; ++i) {
            int w = 3;  // 平滑窗口半径
            float sum = 0.0f;
            int cnt = 0;
            for (int j = std::max(0, i-w); j <= std::min(n-1, i+w); ++j) {
                sum += envelope[j];
                cnt++;
            }
            smoothed[i] = cnt > 0 ? sum / cnt : envelope[i];
        }

        // 找局部峰值（比左右邻居都高）
        int peak_count = 0;
        float threshold = 0.0f;
        for (int i = 0; i < n; ++i) {
            if (smoothed[i] > threshold) threshold = smoothed[i];
        }
        threshold *= 0.3f;  // 30% 的全局最大值

        for (int i = 1; i < n - 1; ++i) {
            if (smoothed[i] > threshold &&
                smoothed[i] > smoothed[i-1] &&
                smoothed[i] > smoothed[i+1]) {
                // 与前一个峰值至少间隔 50ms（避免重复计数）
                peak_count++;
                i += (50 * sr / 1000) / frame_shift;  // 跳过 50ms
            }
        }

        // 音节/秒 = 峰值数 / 总时长
        float total_sec = (float)(n * frame_shift) / sr;
        return total_sec > 0.0f ? (float)peak_count / total_sec : 0.0f;
    }
};

// ── 情感融合器：声学 + 文本 → 最终情绪 ──────────────────

/// 融合声学情绪和文本情绪，产生最终决策
///
/// 策略:
///   - 声学信号权重高（语气是真实情感的窗口）
///   - 文本信号用于确认或覆盖（语义补全）
///   - 冲突时以声学为主（人会掩饰文字但难以掩饰语气）
///
/// 参数:
///   voice_emotion: 声学分析结果
///   text_tone_id:  文本情绪 (0=NEUTRAL,1=HAPPY,2=SAD,3=EMPATHETIC,4=URGENT)
///                  来自 ProsodyController::detect_tone()
///
/// 返回: 融合后的 EmotionTone ID (0-4)

struct EmotionFusion {
    int tone_id = 0;          // 最终 EmotionTone ID
    float voice_weight = 0.6f; // 声学权重
    float text_weight  = 0.4f; // 文本权重
    std::string source;        // 决策来源说明
    std::string diagnostic;    // 诊断日志
};

inline EmotionFusion fuse_emotions(const VoiceEmotionResult& voice,
                                    int text_tone_id)
{
    EmotionFusion result;

    // 声学情绪映射到 tone ID
    int voice_tone_id = voice.to_emotion_tone_id();

    // 声学置信度调整权重
    float vw = 0.6f * voice.confidence;  // 声学权重随置信度缩放
    float tw = 0.4f;
    float total = vw + tw;
    vw /= total;
    tw /= total;

    // 融合决策矩阵
    // voice \ text → final tone
    // NEUTRAL(0) HAPPY(1) SAD(2) EMPATH(3) URGENT(4)
    static const int matrix[5][5] = {
        // NEUT  HAPPY SAD   EMP   URG   ← text
        {  0,    1,    2,    3,    4  },  // voice NEUTRAL → 听文本的
        {  1,    1,    1,    1,    4  },  // voice EXCITED → 以兴奋为主
        {  2,    2,    2,    3,    4  },  // voice SAD → 以悲伤为主
        {  0,    1,    2,    3,    4  },  // voice CALM → 保持文本情绪
        {  4,    4,    4,    4,    4  },  // voice ANGRY → 急迫
    };

    // 如果声学置信度很低（<30%），以文本为准
    if (voice.confidence < 0.3f && text_tone_id != 0) {
        result.tone_id = text_tone_id;
        result.source = "文本为主（声学置信度低）";
        result.voice_weight = 0.2f;
        result.text_weight  = 0.8f;
    } else {
        result.tone_id = matrix[voice_tone_id][text_tone_id];
        if (voice_tone_id == text_tone_id || text_tone_id == 0) {
            result.source = "声学+文本一致";
        } else if (voice_tone_id == 0) {
            result.source = "文本驱动（声学中性）";
        } else {
            result.source = "声学驱动（语气主导）";
        }
        result.voice_weight = vw;
        result.text_weight  = tw;
    }

    // 诊断信息
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "声学=%s(%d,%.0f%%) 文本=%d → 融合=%d [%s]",
        voice.label.c_str(), voice_tone_id, voice.confidence * 100.0f,
        text_tone_id, result.tone_id, result.source.c_str());
    result.diagnostic = buf;

    return result;
}
