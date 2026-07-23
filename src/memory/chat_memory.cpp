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

// ── 持久化 ────────────────────────────────────────

#include <fstream>
#include <nlohmann/json.hpp>

bool ChatMemory::save_to_file(const std::string& path) const
{
    nlohmann::json j;
    nlohmann::json rounds = nlohmann::json::array();

    for (const auto& e : history_) {
        nlohmann::json entry;
        entry["user"]      = e.user;
        entry["assistant"] = e.assistant;
        entry["tokens"]    = e.tokens;
        rounds.push_back(entry);
    }

    j["max_rounds"] = max_rounds_;
    j["max_tokens"] = max_tokens_;
    j["rounds"]     = rounds;

    try {
        std::ofstream f(path);
        if (!f) return false;
        f << j.dump(2);  // 缩进 2 空格，方便人工查看
        return true;
    } catch (...) {
        return false;
    }
}

bool ChatMemory::load_from_file(const std::string& path)
{
    try {
        std::ifstream f(path);
        if (!f) return false;

        nlohmann::json j = nlohmann::json::parse(f);

        // 恢复限制参数
        if (j.contains("max_rounds")) max_rounds_ = j["max_rounds"].get<int>();
        if (j.contains("max_tokens")) max_tokens_ = j["max_tokens"].get<int>();

        // 加载对话轮次
        if (j.contains("rounds") && j["rounds"].is_array()) {
            for (const auto& entry : j["rounds"]) {
                Entry e;
                e.user      = entry.value("user", "");
                e.assistant = entry.value("assistant", "");
                e.tokens    = entry.value("tokens", 0);
                history_.push_back(e);
                total_tokens_ += e.tokens;
            }
        }

        // 加载后裁剪（如果限制变了）
        trim();

        std::cout << "   [Memory] 从文件加载了 " << size()
                  << " 轮对话 (~" << total_tokens_ << " tokens)" << std::endl;
        return true;
    } catch (...) {
        std::cerr << "   [Memory] ⚠️ 加载记忆文件失败: " << path << std::endl;
        return false;
    }
}
