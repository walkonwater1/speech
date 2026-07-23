#pragma once
/**
 * 系统状态查询技能 — 读取 /proc 和系统命令
 *
 * 用法:
 *   "CPU温度多少" / "内存还剩多少" / "磁盘空间还有吗"
 *   "系统负载怎么样" / "现在有多少进程在运行"
 */

#include "skill_base.h"

class SystemSkill : public Skill {
public:
    SystemSkill() : Skill("system") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string execute(const std::string& text,
                        const nlohmann::json& args) override;
    std::string describe() const override {
        return "你可以查询系统状态：CPU温度、内存使用、磁盘空间、系统负载等。";
    }

    FunctionDef get_function_def() const override {
        FunctionDef def;
        def.name = "system";
        def.description = "查询系统状态（CPU温度、内存、磁盘空间等）";
        def.parameters = nlohmann::json::parse(R"({
            "type": "object",
            "properties": {
                "query_type": {
                    "type": "string",
                    "enum": ["cpu_temp", "memory", "disk", "load", "uptime", "all"],
                    "description": "查询类型: cpu_temp/内存/memory/磁盘/disk/负载/load/运行时间/uptime/整体/all"
                }
            }
        })");
        return def;
    }

private:
    static std::string get_cpu_temp();
    static std::string get_memory();
    static std::string get_disk();
    static std::string get_load();
    static std::string get_uptime();
    static std::string exec_cmd(const char* cmd);
};
