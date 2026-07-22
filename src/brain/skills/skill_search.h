#pragma once
/**
 * 网页搜索技能 — 通过 DuckDuckGo Lite（HTML 抓取）
 */

#include "skill_base.h"

class WebSearchSkill : public Skill {
public:
    WebSearchSkill() : Skill("web_search") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string describe() const override {
        return "你可以搜索互联网获取最新信息。";
    }
};
