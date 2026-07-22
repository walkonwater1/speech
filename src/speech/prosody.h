#pragma once
/**
 * 韵律控制器 — LLM 驱动的情感 → TTS 语速映射
 *
 * Layer 3.1: 语音交互深化 — 让合成语音有情感
 *
 * 设计思路:
 *   之前: 所有回复用相同语速朗读 → 机械感
 *   现在: 分析用户输入的情感 → 决定回复语调 → 调整 TTS 语速
 *
 *   为什么分析用户输入而不是 LLM 回复?
 *     - 用户说"我好难过" → 回复应该是温柔慢速的（安抚）
 *     - 用户说"太棒了"   → 回复应该是欢快快速的（共鸣）
 *     - LLM 回复本身不含明确情感标记，分析不准确
 *
 * 情感 → 语速:
 *   悲伤/难过 → -25% 语速（温柔安抚）
 *   开心/兴奋 → +20% 语速（欢快共鸣）
 *   急迫/紧张 → +15% 语速（快速响应）
 *   普通       → 默认语速
 */

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

// ── 韵律配置 ──────────────────────────────────────────

struct ProsodyConfig {
    int base_rate = 200;      // 基础语速 (espeak: 词/分钟)
    int min_rate  = 140;      // 最慢语速
    int max_rate  = 280;      // 最快语速
};

// ── 情感类型 ──────────────────────────────────────────

enum class EmotionTone {
    NEUTRAL,      // 中性
    HAPPY,        // 开心、兴奋
    SAD,          // 悲伤、难过、郁闷
    EMPATHETIC,   // 温柔、安慰、关心
    URGENT,       // 紧急、警告
};

// ── 韵律分析结果 ──────────────────────────────────────

struct ProsodyResult {
    EmotionTone tone = EmotionTone::NEUTRAL;
    int adjusted_rate = 200;
    std::string enhanced_text;
    std::string tone_label;
};

// ── 韵律控制器 ────────────────────────────────────────

class ProsodyController {
public:
    explicit ProsodyController(const ProsodyConfig& cfg = {})
        : config_(cfg) {}

    /// 分析用户输入的文本情感，返回对应的韵律参数
    /// @param user_text    用户输入的文字
    /// @param reply_text   LLM 的回复（用于增强标点，可选）
    ProsodyResult analyze(const std::string& user_text,
                          const std::string& reply_text = "") const
    {
        ProsodyResult result;

        // 1. 情感检测：用户输入为主（权重高），回复内容为辅
        EmotionTone user_tone   = detect_tone(user_text);
        EmotionTone reply_tone  = reply_text.empty()
            ? EmotionTone::NEUTRAL : detect_tone(reply_text);

        // 合并：回复中有开心内容时升级语调
        if (user_tone == EmotionTone::NEUTRAL && reply_tone == EmotionTone::HAPPY) {
            result.tone = EmotionTone::HAPPY;  // 讲笑话 → 检测到回复有"哈哈"等
        } else if (user_tone == EmotionTone::NEUTRAL && reply_tone == EmotionTone::SAD) {
            result.tone = EmotionTone::EMPATHETIC;  // 回复有安慰内容
        } else {
            result.tone = user_tone;
        }

        // 2. 语速调整
        result.adjusted_rate = rate_for_tone(result.tone);

        // 3. 文本增强（标点、语气）
        std::string base = reply_text.empty() ? user_text : reply_text;
        result.enhanced_text = enhance_text(base, result.tone);

        // 4. 日志标签
        result.tone_label = label_for_tone(result.tone);

        return result;
    }

    /// 只做情感检测（不生成增强文本）
    static EmotionTone detect_tone(const std::string& text)
    {
        int sad_score    = 0;
        int happy_score  = 0;
        int emp_score    = 0;
        int urgent_score = 0;

        // ── 悲伤关键词（权重最高）──────────────────────
        static const std::vector<std::string> sad_kw = {
            "难过", "伤心", "不开心", "郁闷", "沮丧", "失望",
            "哭", "累了", "好累", "崩溃", "烦躁", "焦虑", "压力",
            "失败", "失去", "孤独", "寂寞", "无聊", "好烦", "真烦",
            "心疼", "难受", "痛苦", "悲哀", "低落", "抑郁", "丧",
            "不舒服", "不好受"
        };
        for (auto& kw : sad_kw) {
            size_t p = 0;
            while ((p = text.find(kw, p)) != std::string::npos) {
                sad_score += 5; p += kw.size();
            }
        }

        // ── 开心/兴奋关键词 ────────────────────────────
        static const std::vector<std::string> happy_kw = {
            "哈哈", "太好", "太棒", "开心", "好玩", "厉害", "真棒",
            "哇", "耶", "嘿嘿", "嘻嘻", "赞", "牛", "绝了", "爱了",
            "高兴", "快乐", "兴奋", "期待", "笑死", "逗", "有趣",
            "好消息", "恭喜", "庆祝",
            // 内容类型 → 语调提示
            "笑话", "讲个故事", "听听这个", "猜谜", "幽默", "好玩的"
        };
        for (auto& kw : happy_kw) {
            size_t p = 0;
            while ((p = text.find(kw, p)) != std::string::npos) {
                happy_score += 4; p += kw.size();
            }
        }
        // 感叹号增强开心
        for (char c : text) {
            if (c == '!') happy_score += 2;
        }

        // ── 共情/温柔关键词 ────────────────────────────
        static const std::vector<std::string> emp_kw = {
            "别担心", "没事", "抱抱", "辛苦了", "慢慢来", "好好休息",
            "心疼", "关心", "温柔", "温暖", "陪着你", "没关系", "理解你",
            "别急", "放轻松", "注意身体", "谢谢", "感谢", "感恩",
            "想你", "爱你", "喜欢"
        };
        for (auto& kw : emp_kw) {
            size_t p = 0;
            while ((p = text.find(kw, p)) != std::string::npos) {
                emp_score += 3; p += kw.size();
            }
        }

        // ── 紧急/警告关键词 ────────────────────────────
        static const std::vector<std::string> urgent_kw = {
            "注意", "警告", "赶紧", "立刻", "马上", "危险",
            "紧急", "不要", "千万别", "小心", "快跑", "救命",
            "着火", "地震", "报警", "急救"
        };
        for (auto& kw : urgent_kw) {
            size_t p = 0;
            while ((p = text.find(kw, p)) != std::string::npos) {
                urgent_score += 5; p += kw.size();
            }
        }

        // ── 决定情感（优先级从高到低）───────────────────
        if (urgent_score >= 5)   return EmotionTone::URGENT;
        if (sad_score >= 5)      return EmotionTone::SAD;
        if (emp_score >= 3)      return EmotionTone::EMPATHETIC;
        if (happy_score >= 4)    return EmotionTone::HAPPY;

        // 弱信号
        if (sad_score >= 2)      return EmotionTone::SAD;
        if (happy_score >= 2)    return EmotionTone::HAPPY;

        return EmotionTone::NEUTRAL;
    }

    // ── 语速映射 ──────────────────────────────────────

    int rate_for_tone(EmotionTone tone) const
    {
        int r = config_.base_rate;
        switch (tone) {
        case EmotionTone::HAPPY:      r = r * 120 / 100; break;  // +20%
        case EmotionTone::URGENT:     r = r * 115 / 100; break;  // +15%
        case EmotionTone::SAD:        r = r * 75  / 100; break;  // -25%
        case EmotionTone::EMPATHETIC: r = r * 80  / 100; break;  // -20%
        default: break;
        }
        if (r < config_.min_rate) r = config_.min_rate;
        if (r > config_.max_rate) r = config_.max_rate;
        return r;
    }

    // ── 文本增强 ──────────────────────────────────────

    static std::string enhance_text(const std::string& text, EmotionTone tone)
    {
        if (text.empty()) return text;

        std::string enhanced = text;

        switch (tone) {
        case EmotionTone::SAD:
        case EmotionTone::EMPATHETIC:
            // 温柔结尾加 ~
            if (!ends_with_punct(text)) enhanced += "~";
            break;

        case EmotionTone::HAPPY:
            // 确保以感叹号结尾
            if (ends_with_period(text))
                enhanced = enhanced.substr(0, enhanced.size() - 3) + "\xEF\xBC\x81";
            else if (!ends_with_punct(text))
                enhanced += "！";
            break;

        default:
            break;
        }

        return enhanced;
    }

    // ── 日志标签 ──────────────────────────────────────

    static std::string label_for_tone(EmotionTone tone)
    {
        switch (tone) {
        case EmotionTone::HAPPY:      return "开心";
        case EmotionTone::SAD:        return "悲伤";
        case EmotionTone::EMPATHETIC: return "温柔";
        case EmotionTone::URGENT:     return "急迫";
        default:                      return "中性";
        }
    }

private:
    ProsodyConfig config_;

    static bool ends_with_punct(const std::string& text)
    {
        if (text.size() < 3) return false;
        auto c0 = (unsigned char)text[text.size()-3];
        auto c1 = (unsigned char)text[text.size()-2];
        auto c2 = (unsigned char)text[text.size()-1];
        // 。！？
        return (c0 == 0xE3 && c1 == 0x80 && c2 == 0x82) ||
               (c0 == 0xEF && c1 == 0xBC && (c2 == 0x81 || c2 == 0x9F));
    }

    static bool ends_with_period(const std::string& text)
    {
        if (text.size() < 3) return false;
        auto c0 = (unsigned char)text[text.size()-3];
        auto c1 = (unsigned char)text[text.size()-2];
        auto c2 = (unsigned char)text[text.size()-1];
        return c0 == 0xE3 && c1 == 0x80 && c2 == 0x82; // 。
    }
};
