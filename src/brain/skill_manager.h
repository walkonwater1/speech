#pragma once
/**
 * 技能管理器 — 给 LLM 增加外部能力（天气、时间、搜索、RAG 等）
 *
 * 设计思路:
 *   小模型 (qwen2.5:0.5b) 做 function calling 不稳定，
 *   改用「意图检测 + 技能执行 + 上下文注入」方案：
 *
 *   用户文本 → SkillManager::detect_and_execute()
 *                │
 *                ├─ 命中技能 → 执行 → 结果作为 extra_context 注入 LLM prompt
 *                └─ 未命中   → 直接发给 LLM
 *
 *   新增技能只需继承 Skill 基类，注册到 SkillManager 即可。
 */

#include <string>
#include <vector>
#include <memory>
#include <ctime>

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

// ── 具体技能 ──────────────────────────────────────────

/// 天气查询 — 通过 wttr.in API
class WeatherSkill : public Skill {
public:
    WeatherSkill() : Skill("weather") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string describe() const override {
        return "你可以查询天气，当用户问到天气相关问题时直接回答，不要说你不能查。";
    }

private:
    /// 从文本中提取城市名
    static std::string extract_city(const std::string& text);
    /// HTTP GET 请求 (wttr.in)
    static std::string http_get(const std::string& url);
};

/// 时间/日期查询 — 本地计算
class TimeSkill : public Skill {
public:
    TimeSkill() : Skill("time") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string describe() const override {
        return "你可以获取当前时间和日期。";
    }
};

/// 网页搜索 — 通过 DuckDuckGo (可选)
class WebSearchSkill : public Skill {
public:
    WebSearchSkill() : Skill("web_search") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string describe() const override {
        return "你可以搜索互联网获取最新信息。";
    }

private:
    static std::string http_get(const std::string& url);
};

// ── 技能管理器 ────────────────────────────────────────

class SkillManager {
public:
    SkillManager();

    /// 添加技能（接管所有权）
    void add_skill(std::unique_ptr<Skill> skill);

    /// 启用/禁用某个技能
    void set_enabled(const std::string& name, bool enabled);

    /// 检测意图 + 执行技能
    /// @return 如果命中则返回结果，否则 hit=false
    SkillResult detect_and_execute(const std::string& user_text);

    /// 获取所有已启用技能的能力描述（注入 system prompt）
    std::string get_skills_context() const;

    /// 获取系统信息上下文（时间等总是有用的信息）
    static std::string get_system_context();

private:
    std::vector<std::unique_ptr<Skill>> skills_;
};
