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

} // namespace utf8
