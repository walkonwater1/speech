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
    std::string execute(const std::string& text,
                        const nlohmann::json& args) override;
    std::string describe() const override {
        return "你可以搜索互联网获取最新信息。";
    }

    FunctionDef get_function_def() const override {
        FunctionDef def;
        def.name = "web_search";
        def.description = "在互联网上搜索最新信息，用于查找实时新闻、百科知识等";
        def.parameters = nlohmann::json::parse(R"({
            "type": "object",
            "properties": {
                "query": {
                    "type": "string",
                    "description": "搜索关键词，如'今天新闻'、'人工智能最新进展'"
                }
            },
            "required": ["query"]
        })");
        return def;
    }
};
