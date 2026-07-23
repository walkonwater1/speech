/**
 * UTF-8 字符串处理工具 — 消除各处重复的 UTF-8 字节判断逻辑
 *
 * 用于：sanitize_utf8 (llm_engine.cpp), add_breathing_pauses / replace_symbols
 *       (tts_engine.cpp), text_to_pinyin (wake_word.cpp)
 *
 * 用法：
 *   #include "utf8_utils.h"
 *   int len = utf8::char_len(byte);
 *   if (utf8::is_cn_punctuation(ptr)) { ... }
 */

#pragma once

#include <cstdint>
#include <string>

namespace utf8 {

/// 返回 UTF-8 字符的字节长度（1/2/3/4），非法字节返回 1（安全跳过）
inline int char_len(unsigned char c)
{
    if (c <= 0x7F) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; // 非法首字节，按单字节跳过
}

/// 判断 3 字节 UTF-8 序列是否为中文标点
/// @param p 指向 3 字节序列起始位置
inline bool is_cn_punctuation(const char* p)
{
    unsigned char a = static_cast<unsigned char>(p[0]);
    unsigned char b = static_cast<unsigned char>(p[1]);
    unsigned char c = static_cast<unsigned char>(p[2]);

    // 。 U+3002: E3 80 82
    if (a == 0xE3 && b == 0x80 && c == 0x82) return true;
    // 、 U+3001: E3 80 81
    if (a == 0xE3 && b == 0x80 && c == 0x81) return true;
    // ！ U+FF01: EF BC 81
    // ？ U+FF1F: EF BC 9F
    // ， U+FF0C: EF BC 8C
    // ； U+FF1B: EF BC 9B
    // ： U+FF1A: EF BC 9A
    if (a == 0xEF && b == 0xBC) {
        return (c == 0x81 || c == 0x9F || c == 0x8C || c == 0x9B || c == 0x9A);
    }
    return false;
}

/// 提取文本中的纯中文部分（去掉 ASCII 字母，保留中文 + 数字 + 标点）
inline std::string strip_ascii_letters(const std::string& text)
{
    std::string cleaned;
    cleaned.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        int clen = char_len(c);

        if (clen >= 2) {
            // 多字节字符（中文等）：整体保留
            if (i + clen <= text.size()) {
                cleaned.append(text, i, clen);
            }
            i += clen;
        } else if (std::isalpha(c)) {
            // ASCII 字母：整段跳过
            while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
        } else {
            cleaned += text[i];
            ++i;
        }
    }
    return cleaned;
}

/// 纯文本级垃圾检测（不依赖音频能量）
/// 用于过滤流式 ASR 部分结果中的噪声幻觉：
///   SenseVoice 对静音/噪声会输出 ".", "Okay.", "Thank.", "あ.", "그." 等
///
/// 检测维度：
///   1. 空文本 / 纯空白
///   2. 纯标点（ASCII + 中文标点）
///   3. 短纯 ASCII 文本（≤10 字符）→ 英文幻觉，中文模型不应输出纯英文
///   4. 日语假名（ひらがな E381-83 / カタカナ E382-83 / 半角 E38284-86）
///   5. 韩文音节（EA B0-BF / EB 80-BF / EC 80-BF / ED 80-9E）
inline bool is_garbage_text(const std::string& text)
{
    if (text.empty()) return true;

    // 去掉首尾空白
    size_t start = 0, end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    if (start >= end) return true;

    // ── 1. 纯标点检测 ────────────────────────────────
    bool has_content = false;
    for (size_t i = start; i < end; ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        int clen = char_len(c);
        if (clen >= 2) {
            has_content = true;   // 多字节字符 = 有实质内容
            i += clen;
        } else if (std::isalnum(c)) {
            has_content = true;   // ASCII 字母数字
            ++i;
        } else {
            ++i;  // 标点/空白，跳过
        }
    }
    if (!has_content) return true;  // 全是标点/空白

    // ── 2. 短纯 ASCII 文本（SenseVoice 英文幻觉）─────
    //    中文 TTS/LLM 模型无法处理纯英文，且噪声极少产生真实英文
    bool all_ascii = true;
    int ascii_alnum_count = 0;
    for (size_t i = start; i < end; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c > 0x7F) { all_ascii = false; break; }
        if (std::isalnum(c)) ++ascii_alnum_count;
    }
    if (all_ascii && ascii_alnum_count <= 10) return true;

    // ── 3. 日语假名检测（SenseVoice 误判语言）────────
    //    ひらがな U+3040-309F: E3 81 80 – E3 82 9F
    //    カタカナ U+30A0-30FF: E3 82 A0 – E3 83 BF
    //    半角カナ U+FF65-FF9F: EF BD A5 – EF BE 9F
    for (size_t i = start; i + 2 < end; ++i) {
        unsigned char b0 = static_cast<unsigned char>(text[i]);
        unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        if (b0 == 0xE3 && b1 >= 0x81 && b1 <= 0x83) return true;
        if (b0 == 0xEF && b1 >= 0xBD && b1 <= 0xBE) return true;
    }

    // ── 4. 韩文音节检测（SenseVoice 误判语言）────────
    //    한글 U+AC00-D7A3: EA B0 80 – ED 9E A3
    for (size_t i = start; i + 2 < end; ++i) {
        unsigned char b0 = static_cast<unsigned char>(text[i]);
        unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        if (b0 >= 0xEA && b0 <= 0xED) {
            // 韩文音节范围检查
            if (b0 == 0xEA && b1 >= 0xB0) return true;
            if (b0 == 0xEB) return true;
            if (b0 == 0xEC) return true;
            if (b0 == 0xED && b1 <= 0x9E) return true;
        }
    }

    return false;
}

} // namespace utf8
