#include "logger.h"
/**
 * 对话记忆管理 — Token 感知 + O(1) 截断
 */

#include "chat_memory.h"
#include "token_counter.h"
#include <iostream>
#include <algorithm>

ChatMemory::ChatMemory(int max_rounds, int max_tokens)
    : max_rounds_(max_rounds)
    , max_tokens_(max_tokens)
{}

void ChatMemory::add(const std::string& user_msg, const std::string& assistant_msg)
{
    int tokens = TokenCounter::estimate(user_msg)
               + TokenCounter::estimate(assistant_msg);

    history_.push_back({user_msg, assistant_msg, tokens});
    total_tokens_ += tokens;

    trim();

    // 可观测: 显示上下文窗口使用率
    std::cout << "   [Memory] " << size() << " rounds, ~"
              << total_tokens_ << "/" << max_tokens_ << " tokens ("
              << (int)(usage() * 100) << "%)" << std::endl;
}

std::string ChatMemory::get_context() const
{
    if (history_.empty()) return "";

    std::string result;
    for (const auto& e : history_) {
        result += "User: " + e.user + "\n";
        result += "Assistant: " + e.assistant + "\n";
    }
    return result;
}

void ChatMemory::clear()
{
    history_.clear();
    total_tokens_ = 0;
}

void ChatMemory::set_limits(int max_rounds, int max_tokens)
{
    max_rounds_ = max_rounds;
    max_tokens_ = max_tokens;
    trim();
}

void ChatMemory::pop_front()
{
    if (history_.empty()) return;
    total_tokens_ -= history_.front().tokens;
    history_.pop_front();
}

void ChatMemory::trim()
{
    // 1) 限制轮数
    while ((int)history_.size() > max_rounds_) {
        pop_front();
    }

    // 2) 限制 token 数 — O(1) 判断 + 从队头逐轮弹出
    while (total_tokens_ > max_tokens_ && !history_.empty()) {
        pop_front();
    }
}
