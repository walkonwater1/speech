#pragma once
/**
 * 唤醒词检测 (KWS)
 *
 * Python 对应: src/kws.py → WakeWordDetector
 * 依赖:      纯 C++，无外部依赖
 *
 * 实现方式:
 *   1. 汉字 → 拼音查表（静态 unordered_map）
 *   2. 子串匹配唤醒词拼音
 *
 * 不需要 ML 模型，只需要一张汉字→拼音对应表。
 */

#include <string>
#include <unordered_map>

class WakeWordDetector {
public:
    /// @param wake_word 唤醒词拼音，如 "zhan qi lai"，空字符串 = 关闭
    explicit WakeWordDetector(const std::string& wake_word = "");

    bool enabled() const { return !wake_word_.empty(); }

    /// 检查 ASR 文本是否包含唤醒词
    /// @param asr_text ASR 识别出的中文文本
    /// @return true = 检测到唤醒词 或 未启用
    bool detect(const std::string& asr_text);

private:
    std::string wake_word_;   // 拼音

    /// 中文文本 → 拼音字符串（如 "你好" → "ni hao"）
    static std::string text_to_pinyin(const std::string& text);

    /// 单个汉字 → 拼音（查表）
    static const std::unordered_map<std::string, std::string>& pinyin_table();
};
