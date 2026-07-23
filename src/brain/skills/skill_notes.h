#pragma once
/**
 * 语音记事技能 — JSON 文件存储笔记
 *
 * 用法:
 *   "记一下：明天下午三点开会"
 *   "看看我的笔记"
 *   "删除第一条笔记"
 *   "有什么笔记"
 */

#include "skill_base.h"
#include <vector>
#include <string>

struct NoteEntry {
    int id;
    std::string time;
    std::string content;
};

class NotesSkill : public Skill {
public:
    NotesSkill() : Skill("notes") {}

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string execute(const std::string& text,
                        const nlohmann::json& args) override;
    std::string describe() const override {
        return "你可以帮用户记笔记和查看笔记。"
               "当用户说「记一下…」「看看笔记」时调用。";
    }

    FunctionDef get_function_def() const override {
        FunctionDef def;
        def.name = "notes";
        def.description = "添加笔记、查看笔记列表、删除笔记";
        def.parameters = nlohmann::json::parse(R"({
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "enum": ["add", "list", "delete"],
                    "description": "操作: add添加, list查看列表, delete删除"
                },
                "content": {
                    "type": "string",
                    "description": "笔记内容（add时必填）"
                },
                "index": {
                    "type": "integer",
                    "description": "要删除的笔记编号，从1开始（delete时必填）"
                }
            },
            "required": ["action"]
        })");
        return def;
    }

    /// 获取笔记文件的完整路径
    static std::string notes_path();

private:
    std::vector<NoteEntry> load_notes();
    void save_notes(const std::vector<NoteEntry>& notes);
    std::string list_notes(const std::vector<NoteEntry>& notes);
};
