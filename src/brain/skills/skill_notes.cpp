#include "logger.h"
/**
 * 语音记事技能 — 实现
 */

#include "skill_notes.h"
#include "skill_utils.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <ctime>
#include <chrono>
#include <nlohmann/json.hpp>
#include <sys/stat.h>

// ── 笔记文件路径 ────────────────────────────────────────

std::string NotesSkill::notes_path()
{
    // 存放在持久化目录（与 memory 同级）
    const char* home = getenv("HOME");
    if (home) {
        std::string dir = std::string(home) + "/.voice_notes";
        mkdir(dir.c_str(), 0755);
        return dir + "/notes.json";
    }
    return "voice_notes.json";
}

// ── 加载/保存 ───────────────────────────────────────────

std::vector<NoteEntry> NotesSkill::load_notes()
{
    std::vector<NoteEntry> notes;
    std::ifstream f(notes_path());
    if (!f.is_open()) return notes;

    try {
        nlohmann::json j = nlohmann::json::parse(f);
        for (const auto& item : j) {
            NoteEntry e;
            e.id      = item.value("id", 0);
            e.time    = item.value("time", "");
            e.content = item.value("content", "");
            notes.push_back(e);
        }
    } catch (...) {}

    return notes;
}

void NotesSkill::save_notes(const std::vector<NoteEntry>& notes)
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& n : notes) {
        nlohmann::json item;
        item["id"]      = n.id;
        item["time"]    = n.time;
        item["content"] = n.content;
        j.push_back(item);
    }
    std::ofstream f(notes_path());
    if (f.is_open()) {
        f << j.dump(2) << std::endl;
    }
}

std::string NotesSkill::list_notes(const std::vector<NoteEntry>& notes)
{
    std::ostringstream oss;
    if (notes.empty()) {
        oss << "目前没有笔记。";
        return oss.str();
    }
    for (size_t i = 0; i < notes.size(); ++i) {
        oss << "  " << (i + 1) << ". [" << notes[i].time << "] "
            << notes[i].content;
        if (i + 1 < notes.size()) oss << "\n";
    }
    return oss.str();
}

// ── 关键词匹配 ──────────────────────────────────────────

bool NotesSkill::match(const std::string& text)
{
    static const std::vector<std::string> keywords = {
        "记一下", "记笔记", "笔记", "备忘录",
        "查看笔记", "看看笔记", "有什么笔记",
        "删除笔记", "删掉笔记", "清空笔记"
    };
    return contains_any(text, keywords);
}

// ── 执行 ────────────────────────────────────────────────

std::string NotesSkill::execute(const std::string& text)
{
    return execute(text, nlohmann::json::object());
}

std::string NotesSkill::execute(const std::string& text,
                                 const nlohmann::json& args)
{
    std::string action;
    std::string content;
    int index = -1;

    // 优先使用 LLM 参数
    if (args.contains("action") && args["action"].is_string()) {
        action = args["action"].get<std::string>();
    }
    if (args.contains("content") && args["content"].is_string()) {
        content = args["content"].get<std::string>();
    }
    if (args.contains("index") && args["index"].is_number()) {
        index = args["index"].get<int>();
    }

    // 降级：从文本推断
    if (action.empty()) {
        if (text.find("记一下") != std::string::npos ||
            text.find("记笔记") != std::string::npos ||
            text.find("备忘") != std::string::npos) {
            action = "add";
            // 提取内容："记一下：XXX" / "记笔记 XXX"
            for (auto& delim : {"：", ":"}) {
                size_t p = text.find(delim);
                if (p != std::string::npos && p + 3 < text.size()) {
                    content = text.substr(p + 3);
                    break;
                }
            }
            if (content.empty()) {
                size_t p = text.find("记一下");
                if (p != std::string::npos) {
                    content = text.substr(p + 9);  // "记一下" 3 chars
                }
            }
        } else if (text.find("查看") != std::string::npos ||
                   text.find("看看") != std::string::npos ||
                   text.find("有什么") != std::string::npos) {
            action = "list";
        } else if (text.find("删除") != std::string::npos ||
                   text.find("删掉") != std::string::npos) {
            action = "delete";
            // 提取编号
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] >= '1' && text[i] <= '9') {
                    index = text[i] - '0';
                    break;
                }
            }
        }
    }

    std::cout << "   [Skill:笔记] action=" << action
              << " content=" << content << std::endl;

    auto notes = load_notes();

    // ── 执行操作 ────────────────────────────────────────
    if (action == "add") {
        if (content.empty()) {
            return "要记什么内容呢？";
        }

        // 生成时间戳
        auto now = std::chrono::system_clock::now();
        auto t   = std::chrono::system_clock::to_time_t(now);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%m-%d %H:%M", localtime(&t));

        NoteEntry e;
        e.id      = notes.empty() ? 1 : notes.back().id + 1;
        e.time    = time_buf;
        e.content = content;
        notes.push_back(e);
        save_notes(notes);

        std::ostringstream oss;
        oss << "已记下第" << notes.size() << "条笔记：「" << content << "」。";
        return oss.str();

    } else if (action == "list") {
        std::ostringstream oss;
        oss << "📝 你的笔记:\n" << list_notes(notes);
        return oss.str();

    } else if (action == "delete") {
        if (index <= 0) {
            return "要删除第几条笔记呢？";
        }
        if (index > (int)notes.size()) {
            return "只有" + std::to_string(notes.size()) + "条笔记哦。";
        }
        std::string deleted = notes[index - 1].content;
        notes.erase(notes.begin() + index - 1);
        // 重新编号为 1-based
        for (size_t i = 0; i < notes.size(); ++i) notes[i].id = (int)i + 1;
        save_notes(notes);

        std::ostringstream oss;
        oss << "已删除笔记「" << deleted << "」。还有"
            << notes.size() << "条笔记。";
        return oss.str();
    }

    return list_notes(notes);
}
