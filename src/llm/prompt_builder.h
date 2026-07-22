#pragma once
/**
 * Prompt 构建器 — 模板变量注入 + 结构化 prompt 组装
 *
 * 学习要点:
 *   1. 模板变量替换: "{time}" → "2026-07-22 14:30"
 *   2. System / User / Assistant 角色分离
 *   3. 上下文窗口管理: 合理分配 system / history / user 的比例
 *
 * 使用方式:
 *   PromptBuilder pb;
 *   pb.set_system("你是小千，一个18岁女大学生。当前时间: {time}")
 *     .set("time", "2026-07-22 14:30")
 *     .set("weather", "北京 晴 15°C");
 *
 *   // 构建 Ollama messages
 *   json messages = pb.build(user_msg, history, extra_info);
 */

#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

class PromptBuilder {
public:
    PromptBuilder() = default;

    /// 设置 system prompt 模板（支持 {变量名} 占位符）
    PromptBuilder& set_system(const std::string& system_prompt);

    /// 设置模板变量
    PromptBuilder& set(const std::string& key, const std::string& value);
    PromptBuilder& set(const std::string& key, int value);

    /// 获取展开后的 system prompt（变量已替换为实际值）
    std::string system_prompt() const;

    /// 构建 Ollama /api/chat 的 messages 数组
    /// @param user_message   用户当前输入
    /// @param history_text   对话历史文本（可选，拼入 user message）
    /// @param extra_context  额外上下文（技能结果等，拼入 user message）
    nlohmann::json build_messages(const std::string& user_message,
                                  const std::string& history_text = "",
                                  const std::string& extra_context = "") const;

    /// 构建完整的 user message（合并 history + extra + 当前消息）
    std::string build_user_message(const std::string& user_message,
                                   const std::string& history_text = "",
                                   const std::string& extra_context = "") const;

private:
    std::string system_template_;
    std::map<std::string, std::string> variables_;

    /// 替换文本中的 {变量名} 占位符
    std::string expand(const std::string& text) const;
};
