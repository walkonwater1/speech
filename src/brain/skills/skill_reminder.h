#pragma once
/**
 * 提醒/计时技能 — 后台定时器触发通知
 *
 * 用法:
 *   "5分钟后提醒我出门"
 *   "设一个10分钟计时器"
 *   "30秒后叫我"
 *   "下午3点提醒我开会"
 */

#include "skill_base.h"

class ReminderSkill : public Skill {
public:
    ReminderSkill() : Skill("reminder") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string execute(const std::string& text,
                        const nlohmann::json& args) override;
    std::string describe() const override {
        return "你可以设置提醒和计时器。"
               "例如当用户说「5分钟后提醒我出门」时调用。";
    }

    FunctionDef get_function_def() const override {
        FunctionDef def;
        def.name = "reminder";
        def.description = "设置一个定时提醒或倒计时";
        def.parameters = nlohmann::json::parse(R"({
            "type": "object",
            "properties": {
                "seconds": {
                    "type": "integer",
                    "description": "延迟秒数，5分钟=300，10分钟=600"
                },
                "message": {
                    "type": "string",
                    "description": "提醒内容"
                }
            },
            "required": ["seconds"]
        })");
        return def;
    }

private:
    /// 从中文文本中解析延迟秒数
    /// 支持: "N秒后", "N分钟后", "N小时后", "N分钟", "N秒"
    static int parse_delay_seconds(const std::string& text);
};
