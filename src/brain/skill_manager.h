#pragma once
/**
 * 技能管理器 — 给 LLM 增加外部能力（天气、时间、搜索、RAG 等）
 *
 * 混合调度策略:
 *
 *   用户文本 → SkillManager::detect_and_execute()
 *                │
 *                ├─ [Primary] Function Calling (LLM 驱动)
 *                │    LLM 收到所有工具的 JSON Schema →
 *                │    自主选择工具 + 提取参数 →
 *                │    "外面冷不冷" → LLM 理解语义 → 选 weather 工具 ✅
 *                │
 *                ├─ [Fallback] Keyword Match (关键字降级)
 *                │    当 LLM 不可用 / 模型太小 / JSON 解析失败时
 *                │    "天气" → match("天气") → WeatherSkill::execute()
 *                │
 *                └─ 未命中 → 直接发给 LLM 生成回复
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
class FunctionCaller;

// ── 技能管理器 ────────────────────────────────────────

class SkillManager {
public:
    SkillManager();

    /// 添加技能（接管所有权）
    void add_skill(std::unique_ptr<Skill> skill);

    /// 注册 RAG 技能（需要 EmbeddingEngine，独立方法）
    void register_rag(std::shared_ptr<EmbeddingEngine> embed,
                      const std::string& docs_dir = "knowledge_base");

    /// 设置 Function Calling 引擎（启用 LLM 驱动工具选择）
    /// @param fc FunctionCaller 实例（可为 nullptr 禁用 function calling）
    void set_function_caller(std::shared_ptr<FunctionCaller> fc) {
        function_caller_ = std::move(fc);
    }

    /// 启用/禁用 Function Calling（不影响关键字匹配降级）
    void set_function_calling_enabled(bool v) { fc_enabled_ = v; }
    bool function_calling_enabled() const { return fc_enabled_; }

    /// 启用/禁用某个技能
    void set_enabled(const std::string& name, bool enabled);

    /// 检测意图 + 执行技能（混合调度）
    /// @return 如果命中则返回结果，否则 hit=false
    SkillResult detect_and_execute(const std::string& user_text);

    /// 获取所有已启用技能的能力描述（注入 system prompt）
    std::string get_skills_context() const;

    /// 获取系统信息上下文（时间等总是有用的信息）
    static std::string get_system_context();

private:
    std::vector<std::unique_ptr<Skill>> skills_;
    std::shared_ptr<FunctionCaller> function_caller_;  // LLM 工具选择器
    bool fc_enabled_ = true;                           // function calling 开关

    /// 收集所有已启用技能的函数定义
    std::vector<FunctionDef> collect_function_defs() const;

    /// 按名称查找技能
    Skill* find_skill(const std::string& name);
};
