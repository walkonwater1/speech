#pragma once
/**
 * Reflection 反思引擎 — LLM 自我审查与纠错
 *
 * 学习要点（Agent 演进第三步）:
 *   之前: ReAct → LLM 生成回复 → 直接输出（可能有事实错误）
 *   现在: ReAct → LLM 生成回复 → Reflection 审查 → 修正 → 输出
 *
 * 核心思想:
 *   "The first draft of anything is shit." — Hemingway
 *   LLM 的第一版回复常有这些问题:
 *     - 事实错误（幻觉）: "杭州明天晴天"（实际工具返回的是雨天）
 *     - 不完整: 漏了用户问的部分问题
 *     - 语气不当: 太正式/太随意
 *     - 冗长啰嗦: 超过字数限制
 *
 * 流程:
 *   ┌─────────────────────────────────────────────┐
 *   │  原始回复: "明天天气很好，适合出去玩！"       │
 *   │     ↓                                       │
 *   │  Reflection: "工具返回的是雷阵雨，你却说晴天" │
 *   │     ↓                                       │
 *   │  修正: "明天有雷阵雨，建议带伞，室内活动为主"  │
 *   └─────────────────────────────────────────────┘
 *
 * 两层反思:
 *   1. 事实核查 — 回复是否与工具结果一致？
 *   2. 质量审核 — 语气、长度、完整性是否合适？
 */

#include <string>
#include <nlohmann/json.hpp>

// ── 反思结果 ──────────────────────────────────────────

struct ReflectionResult {
    std::string original;     // 原始回复
    std::string improved;     // 修正后的回复（无问题则与 original 相同）
    bool had_issues = false;  // 是否发现问题
    std::string critique;     // LLM 的批评意见（调试用）
};

// ── Reflection 引擎 ───────────────────────────────────

class ReflectionEngine {
public:
    /// @param ollama_host  Ollama 服务地址
    /// @param model        用于反思的模型（可与主模型不同）
    ReflectionEngine(const std::string& ollama_host,
                     const std::string& model);

    /// 审查并修正回复
    /// @param user_question  用户原始问题
    /// @param original_reply LLM 生成的原始回复
    /// @param tool_context   使用的工具结果（供事实核查，可选）
    /// @param personality    人格描述（供语气检查，可选）
    /// @return 修正后的结果
    ReflectionResult reflect(const std::string& user_question,
                             const std::string& original_reply,
                             const std::string& tool_context = "",
                             const std::string& personality = "");

    /// 设置超时（秒）
    void set_timeout(long sec) { timeout_sec_ = sec; }

private:
    std::string host_;
    std::string model_;
    long timeout_sec_ = 30;

    /// 构建反思 prompt
    static std::string build_reflection_prompt(
        const std::string& user_question,
        const std::string& original_reply,
        const std::string& tool_context,
        const std::string& personality);

    /// HTTP POST 到 Ollama
    std::string http_post(const std::string& request_json) const;
};
