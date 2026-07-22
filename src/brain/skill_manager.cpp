/**
 * 技能管理器 — 实现
 *
 * 注册所有可用技能。新增技能时：
 *   1. 在 brain/skills/ 下创建 skill_xxx.h + skill_xxx.cpp
 *   2. 在这里 #include 并 add_skill 注册
 *   3. 在 CMakeLists.txt 中添加源文件
 */

#include "skill_manager.h"

// ── 所有技能头文件 ────────────────────────────────────
#include "skills/skill_weather.h"
#include "skills/skill_time.h"
#include "skills/skill_search.h"
#include "skills/skill_rag.h"
#include "embedding_engine.h"

#include <sstream>
#include <iostream>
#include <ctime>
#include <cstdio>

// ── 构造函数：注册所有技能 ────────────────────────────

SkillManager::SkillManager()
{
    add_skill(std::make_unique<WeatherSkill>());
    add_skill(std::make_unique<TimeSkill>());
    add_skill(std::make_unique<WebSearchSkill>());
}

void SkillManager::add_skill(std::unique_ptr<Skill> skill)
{
    skills_.push_back(std::move(skill));
}

void SkillManager::register_rag(std::shared_ptr<EmbeddingEngine> embed,
                                const std::string& docs_dir)
{
    auto rag = std::make_unique<RAGSkill>(embed, docs_dir);
    // 默认启用状态由 config 决定，这里先添加，外部再 set_enabled
    add_skill(std::move(rag));
}

void SkillManager::set_enabled(const std::string& name, bool enabled)
{
    for (auto& s : skills_) {
        if (s->name() == name) {
            s->set_enabled(enabled);
            return;
        }
    }
}

SkillResult SkillManager::detect_and_execute(const std::string& user_text)
{
    for (auto& s : skills_) {
        if (!s->enabled()) continue;

        if (s->match(user_text)) {
            std::string result = s->execute(user_text);
            if (!result.empty()) {
                return {true, s->name(), result};
            }
        }
    }
    return {false, "", ""};
}

std::string SkillManager::get_skills_context() const
{
    std::stringstream ss;
    for (auto& s : skills_) {
        if (!s->enabled()) continue;
        std::string desc = s->describe();
        if (!desc.empty()) {
            ss << "- " << desc << "\n";
        }
    }
    return ss.str();
}

std::string SkillManager::get_system_context()
{
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);

    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "当前时间: %d-%02d-%02d %02d:%02d",
        local->tm_year + 1900,
        local->tm_mon + 1,
        local->tm_mday,
        local->tm_hour,
        local->tm_min);

    return std::string(buf);
}
