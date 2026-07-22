#pragma once
/**
 * 天气查询技能 — 通过 wttr.in API（免费，无需注册）
 */

#include "skill_base.h"

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
};
