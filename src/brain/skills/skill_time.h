#pragma once
/**
 * 时间/日期查询技能 — 本地 localtime 计算
 */

#include "../skill_base.h"

class TimeSkill : public Skill {
public:
    TimeSkill() : Skill("time") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string describe() const override {
        return "你可以获取当前时间和日期。";
    }
};
