/**
 * Prompt 构建器 — 实现
 */

#include "prompt_builder.h"
#include <sstream>
#include <ctime>
#include <cstdio>

// ── 模板变量 ──────────────────────────────────────────

PromptBuilder& PromptBuilder::set_system(const std::string& system_prompt)
{
    system_template_ = system_prompt;
    return *this;
}

PromptBuilder& PromptBuilder::set(const std::string& key, const std::string& value)
{
    variables_[key] = value;
    return *this;
}

PromptBuilder& PromptBuilder::set(const std::string& key, int value)
{
    variables_[key] = std::to_string(value);
    return *this;
}

std::string PromptBuilder::expand(const std::string& text) const
{
    std::string result;
    result.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{' && i + 1 < text.size()) {
            // 查找匹配的 }
            size_t close = text.find('}', i + 1);
            if (close != std::string::npos) {
                std::string key = text.substr(i + 1, close - i - 1);
                auto it = variables_.find(key);
                if (it != variables_.end()) {
                    result += it->second;
                } else {
                    // 未找到变量 → 保留原样（方便调试）
                    result += '{' + key + '}';
                }
                i = close + 1;
                continue;
            }
        }
        result += text[i];
        ++i;
    }

    return result;
}

// ── 构建 ──────────────────────────────────────────────

std::string PromptBuilder::system_prompt() const
{
    return expand(system_template_);
}

std::string PromptBuilder::build_user_message(
    const std::string& user_message,
    const std::string& history_text,
    const std::string& extra_context) const
{
    std::ostringstream oss;

    // 1. 额外上下文（技能结果、系统信息等）
    if (!extra_context.empty()) {
        oss << "[系统信息]\n" << expand(extra_context) << "\n\n";
    }

    // 2. 对话历史（前面几轮的 user/assistant 记录）
    if (!history_text.empty()) {
        oss << history_text << "\n";
    }

    // 3. 当前用户消息
    oss << "User: " << user_message << "\nAssistant:";

    return oss.str();
}

nlohmann::json PromptBuilder::build_messages(
    const std::string& user_message,
    const std::string& history_text,
    const std::string& extra_context) const
{
    using json = nlohmann::json;

    std::string sys = system_prompt();
    std::string user = build_user_message(user_message, history_text, extra_context);

    return json::array({
        {{"role", "system"}, {"content", sys}},
        {{"role", "user"},   {"content", user}}
    });
}
