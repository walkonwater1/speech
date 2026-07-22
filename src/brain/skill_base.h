#pragma once
/**
 * 技能基类 — 所有 Skill 的抽象接口
 *
 * 小模型 (qwen2.5:0.5b) 做 function calling 不稳定，
 * 改用「意图检测 + 技能执行 + 上下文注入」方案：
 *
 *   用户文本 → Skill::match()
 *                │
 *                ├─ 命中 → Skill::execute() → 结果注入 LLM 上下文
 *                └─ 未命中 → 直接发给 LLM
 *
 *   新增技能只需继承 Skill，在 SkillManager 构造函数中注册即可。
 */

#include <string>

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

    /// 意图匹配：检查用户输入是否触发该技能
    /// @param text 用户原文（ASR 识别结果）
    virtual bool match(const std::string& text) = 0;

    /// 执行技能：调用外部 API / 本地计算
    /// @param text 用户原文（用于提取参数，如城市名）
    /// @return 技能结果文本
    virtual std::string execute(const std::string& text) = 0;

    /// 该技能的能力描述（会注入 system prompt，让 LLM 知道它的存在）
    virtual std::string describe() const { return ""; }

    const std::string& name()    const { return name_; }
    bool               enabled() const { return enabled_; }
    void               set_enabled(bool v) { enabled_ = v; }

protected:
    std::string name_;
    bool        enabled_;
};
