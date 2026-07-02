/**
 * 对话记忆管理
 *
 * Python 对应: src/memory.py → ChatMemory
 * C++ 实现:   std::deque<std::pair<std::string, std::string>>
 */

#include "chat_memory.h"
#include <algorithm>

ChatMemory::ChatMemory(int max_rounds, int max_tokens)
    : max_rounds_(max_rounds)
    , max_tokens_(max_tokens)
{}

void ChatMemory::add(const std::string& user_msg, const std::string& assistant_msg)
{
    history_.emplace_back(user_msg, assistant_msg);
    trim();
}

std::string ChatMemory::get_context() const
{
    if (history_.empty()) return "";

    // 取最近 max_rounds_ 轮
    int start = std::max(0, (int)history_.size() - max_rounds_);

    std::string result;
    for (int i = start; i < (int)history_.size(); ++i) {
        result += "User: " + history_[i].first + "\n";
        result += "Assistant: " + history_[i].second + "\n";
    }
    return result;
}

void ChatMemory::clear()
{
    history_.clear();
}

void ChatMemory::trim()
{
    // 1) 限制轮数
    while ((int)history_.size() > max_rounds_) {
        history_.pop_front();
    }

    // 2) 限制 token 数（粗略：1 token ≈ 4 字符）
    while (!history_.empty()) {
        int total_chars = 0;
        for (const auto& [u, a] : history_) {
            total_chars += u.size() + a.size();
        }
        if (total_chars * 4 <= max_tokens_) break;
        history_.pop_front();
    }
}
