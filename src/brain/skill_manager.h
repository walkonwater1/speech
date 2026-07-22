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
 *   新增技能只需继承 Skill 基类，在 SkillManager 构造函数中注册即可。
 *   每个技能放在 brain/skills/ 目录下，独立 .h/.cpp 文件。
 */

#include "skill_base.h"
#include <string>
#include <vector>
#include <memory>
#include <ctime>

// 前向声明
class EmbeddingEngine;

// ── 技能管理器 ────────────────────────────────────────

class SkillManager {
public:
    SkillManager();

    /// 添加技能（接管所有权）
    void add_skill(std::unique_ptr<Skill> skill);

    /// 注册 RAG 技能（需要 EmbeddingEngine，独立方法）
    void register_rag(std::shared_ptr<EmbeddingEngine> embed,
                      const std::string& docs_dir = "knowledge_base");

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
