/**
 * 文档切分器 — 实现
 */

#include "document_chunker.h"
#include <algorithm>
#include <iostream>

std::vector<std::string> DocumentChunker::split(
    const std::string& text,
    int chunk_size,
    int overlap,
    const std::vector<std::string>& separators)
{
    std::vector<std::string> chunks;
    if (text.empty()) return chunks;

    size_t pos = 0;
    while (pos < text.size()) {
        // 确保不超出文本末尾
        if (pos + chunk_size >= text.size()) {
            // 最后一段
            std::string chunk = text.substr(pos);
            if (!chunk.empty()) {
                // 去除首尾空白
                auto start = chunk.find_first_not_of(" \t\r\n");
                auto end   = chunk.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    chunks.push_back(chunk.substr(start, end - start + 1));
                }
            }
            break;
        }

        // 在 chunk_size 附近找最佳断句点
        size_t split_at = find_split_point(text, pos + chunk_size, separators);

        std::string chunk = text.substr(pos, split_at - pos);

        // 清洗
        auto start = chunk.find_first_not_of(" \t\r\n");
        auto end   = chunk.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            chunks.push_back(chunk.substr(start, end - start + 1));
        }

        // 前进（减去 overlap）
        if (split_at >= (size_t)overlap) {
            pos = split_at - overlap;
        } else {
            pos = split_at;
        }
    }

    return chunks;
}

size_t DocumentChunker::find_split_point(
    const std::string& text,
    size_t target,
    const std::vector<std::string>& separators)
{
    if (target >= text.size()) return text.size();

    // 在 target 前后 ±25% 范围内搜索最佳断句点
    size_t search_window = (size_t)(target * 0.25);
    if (search_window < 10) search_window = 10;

    size_t search_end   = std::min(target + search_window, text.size());

    // 对每个分隔符，在搜索窗口内从 target 附近向两侧找
    for (const auto& sep : separators) {
        // 找 target 右侧最近的匹配
        size_t best = std::string::npos;
        size_t pos = target;

        while (pos < search_end) {
            pos = text.find(sep, pos);
            if (pos == std::string::npos || pos >= search_end) break;
            // 找到 — 切在分隔符之后
            best = pos + sep.size();
            break;
        }

        if (best != std::string::npos && best < search_end) {
            return best;
        }
    }

    // 没找到分隔符 → 直接在 target 处硬切（避免切到 UTF-8 中间）
    // 向前调整到 UTF-8 字符边界
    size_t result = target;
    while (result < text.size() && (static_cast<unsigned char>(text[result]) & 0xC0) == 0x80) {
        ++result;
    }
    return result;
}
