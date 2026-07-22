#pragma once
/**
 * Multi-Agent 协作引擎 — 两个 Agent 对话迭代优化回复
 *
 * 学习要点（Agent 演进第四步 — 最高级模式）:
 *   之前: 单个 Agent → 生成 → 反思（独自完成）
 *   现在: 两个 Agent → 对话 → 批评 → 改进（协作完成）
 *
 * 为什么要 Multi-Agent:
 *   - 单 Agent 有盲区: 同一个人既写又改，容易"当局者迷"
 *   - 两个不同角色的 Agent 互相制衡:
 *     Generator (生成者): 专注创造，风格活泼
 *     Critic   (批评者): 专注审查，严格挑剔
 *   - 涌现行为: 两个 Agent 对话可能产生单 Agent 无法达到的质量
 *
 * 对话流程:
 *   ┌────────────────────────────────────────────────────┐
 *   │  User: "杭州明天天气怎么样"                          │
 *   │    ↓                                               │
 *   │  Generator: "杭州明天雷阵雨，31°C，记得带伞哦！"     │
 *   │    ↓                                               │
 *   │  Critic: "回复不错，但太简短。可加上湿度信息和出行建议" │
 *   │    ↓                                               │
 *   │  Generator: "杭州明天雷阵雨⛈️ 31°C，湿度82%...       │
 *   │              建议室内活动，如需出门务必带伞！"        │
 *   │    ↓                                               │
 *   │  → 最终优化回复                                     │
 *   └────────────────────────────────────────────────────┘
 */

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ── Agent 配置 ─────────────────────────────────────────

struct AgentConfig {
    std::string name;           // Agent 名称（日志用）
    std::string system_prompt;  // 角色设定
    std::string model;          // 使用哪个 LLM 模型
};

// ── 一轮对话 ──────────────────────────────────────────

struct AgentMessage {
    std::string agent;   // 发言人
    std::string content; // 内容
};

// ── Multi-Agent 结果 ──────────────────────────────────

struct MultiAgentResult {
    std::string final_answer;              // 最终优化后的回复
    std::vector<AgentMessage> transcript;  // 完整对话记录（调试用）
    int rounds = 0;                        // 对话轮数
    bool improved = false;                 // 是否有改进
};

// ── Multi-Agent 引擎 ──────────────────────────────────

class MultiAgentEngine {
public:
    /// @param ollama_host  Ollama 服务地址
    MultiAgentEngine(const std::string& ollama_host);

    /// 两个 Agent 协作优化回复
    /// @param user_question  用户原始问题
    /// @param initial_reply  初始回复（由 ReAct/FC 生成）
    /// @param tool_context   工具查询结果（供事实核查）
    /// @param personality    Generator 的人设（用于生成回复）
    /// @param generator_model 生成者使用的模型
    /// @param critic_model    批评者使用的模型
    /// @param max_rounds      最大对话轮数
    MultiAgentResult collaborate(const std::string& user_question,
                                 const std::string& initial_reply,
                                 const std::string& tool_context,
                                 const std::string& personality,
                                 const std::string& generator_model,
                                 const std::string& critic_model,
                                 int max_rounds = 2);

private:
    std::string host_;
    long timeout_sec_ = 60;

    /// 让某个 Agent 发言
    std::string ask_agent(const std::string& system_prompt,
                          const std::string& model,
                          const nlohmann::json& messages);

    /// HTTP POST
    std::string http_post(const std::string& request_json) const;
};
