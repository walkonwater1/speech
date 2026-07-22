#pragma once
/**
 * 技能基类 — 所有 Skill 的抽象接口
 *
 * 两层调度策略（混合模式）:
 *
 *   1. Function Calling (LLM 驱动，优先):
 *      LLM 收到所有工具的 JSON Schema → LLM 自主选择工具 + 提取参数
 *      "外面冷不冷" → LLM 理解语义 → {"tool":"weather","args":{"city":"北京"}}
 *
 *   2. Keyword Match (关键字匹配，降级):
 *      用户文本 → Skill::match() → 命中 → execute()
 *      当 LLM function calling 失败或模型太小时自动降级
 *
 *   新增技能只需继承 Skill，在 SkillManager 构造函数中注册即可。
 */

#include <string>
#include <nlohmann/json.hpp>

// ── 函数定义（供 LLM function calling 使用）──────────

struct FunctionDef {
    std::string name;              // 函数名
    std::string description;       // 功能描述（LLM 用此判断何时调用）
    nlohmann::json parameters;     // JSON Schema 参数定义
};

// ── 技能执行结果 ──────────────────────────────────────

struct SkillResult {
    bool        hit        = false;  // 是否命中某技能
    std::string skill_name;          // 命中技能名（日志用）
    std::string result_text;         // 执行结果，将注入 LLM 上下文
};

// ── 技能基类 ──────────────────────────────────────────

class Skill {
public:
    Skill(const std::string& name, bool enabled = true)
        : name_(name), enabled_(enabled) {}

    virtual ~Skill() = default;

    /// 意图匹配：检查用户输入是否触发该技能（关键字降级方案）
    /// @param text 用户原文（ASR 识别结果）
    virtual bool match(const std::string& text) = 0;

    /// 执行技能：调用外部 API / 本地计算
    /// @param text 用户原文（用于提取参数，如城市名）
    /// @return 技能结果文本
    virtual std::string execute(const std::string& text) = 0;

    /// 该技能的能力描述（会注入 system prompt，让 LLM 知道它的存在）
    virtual std::string describe() const { return ""; }

    /// 返回 Function Calling 的函数定义（JSON Schema）
    /// 用于 LLM 驱动的工具选择
    virtual FunctionDef get_function_def() const { return {}; }

    const std::string& name()    const { return name_; }
    bool               enabled() const { return enabled_; }
    void               set_enabled(bool v) { enabled_ = v; }

protected:
    std::string name_;
    bool        enabled_;
};
