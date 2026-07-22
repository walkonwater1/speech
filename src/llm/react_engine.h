#pragma once
/**
 * ReAct 推理引擎 — Reasoning + Acting 多步循环
 *
 * 学习要点（Agent 演进第二步）:
 *   之前: Function Calling → 单步工具调用 → LLM 生成回复
 *   现在: ReAct → 多步推理循环 → think → act → observe → think ...
 *
 * 核心循环:
 *   ┌─────────────────────────────────────────────────┐
 *   │  User: "杭州明天天气怎么样，适合出去玩吗"        │
 *   │    ↓                                            │
 *   │  Think: 需要查杭州天气                           │
 *   │    ↓                                            │
 *   │  Act:  weather({city:"杭州"})                   │
 *   │    ↓                                            │
 *   │  Observe: "杭州明天晴天，25°C"                   │
 *   │    ↓                                            │
 *   │  Think: 天气很好，可以建议出去玩                  │
 *   │    ↓                                            │
 *   │  Final: "杭州明天晴天25°C，非常适合出去玩！"      │
 *   └─────────────────────────────────────────────────┘
 *
 * 为什么需要 ReAct:
 *   - 复杂问题需要多步推理（查天气 + 搜索景点 → 综合建议）
 *   - 工具结果可能触发新的工具调用（先查航班 → 再查酒店）
 *   - LLM 能根据中间结果调整策略
 */

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include <nlohmann/json.hpp>
#include "skill_base.h"  // FunctionDef

// ── ReAct 执行结果 ────────────────────────────────────

struct ReActStep {
    std::string thought;       // LLM 推理过程
    std::string action;        // 调用的工具名（空 = 最终答案）
    nlohmann::json args;       // 工具参数
    std::string observation;   // 工具返回结果
};

struct ReActResult {
    std::string final_answer;           // 最终回答
    std::vector<ReActStep> steps;       // 推理步骤（日志/调试用）
    int total_steps = 0;                // 总步数
    bool success = false;               // 是否成功完成
};

// ── ReAct 引擎 ────────────────────────────────────────

class ReActEngine {
public:
    /// @param ollama_host  Ollama 服务地址
    /// @param model        用于推理的模型
    /// @param system_prompt 系统人设（会被追加 ReAct 指令）
    ReActEngine(const std::string& ollama_host,
                const std::string& model,
                const std::string& system_prompt = "");

    /// 运行 ReAct 循环
    /// @param user_question  用户问题
    /// @param tools          可用工具列表
    /// @param execute_tool   工具执行回调 (tool_name, args) → result_text
    /// @param history_context 历史对话上下文（可选）
    /// @param max_steps      最大推理步数（防止死循环）
    /// @return 包含最终答案和推理步骤的完整结果
    ReActResult run(const std::string& user_question,
                    const std::vector<FunctionDef>& tools,
                    std::function<std::string(const std::string&, const nlohmann::json&)> execute_tool,
                    const std::string& history_context = "",
                    int max_steps = 5);

private:
    std::string host_;
    std::string model_;
    std::string system_prompt_;
    long timeout_sec_ = 60;

    /// 构建包含 ReAct 指令的完整 system prompt
    static std::string build_react_prompt(
        const std::string& base_prompt,
        const std::vector<FunctionDef>& tools);

    /// 解析 LLM 的单步输出
    /// LLM 输出 JSON: {"thought":"...","action":"tool","args":{...}}
    ///         或:  {"thought":"...","final":"回答文本"}
    struct ParsedStep {
        std::string thought;
        std::string action;        // 空 = 最终答案
        nlohmann::json args;
        std::string final_answer;  // action 为空时使用
        bool parse_error = false;
    };
    static ParsedStep parse_step(const std::string& llm_output);

    /// 单次 LLM HTTP 调用
    std::string http_post(const std::string& request_json) const;
};
