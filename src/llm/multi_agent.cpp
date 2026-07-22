/**
 * Multi-Agent 协作引擎 — 实现
 *
 * 学习要点:
 *   1. Multi-Agent = 多个 LLM 实例对话协作
 *   2. 角色分工: Generator 创造 + Critic 审查
 *   3. 不同模型: Generator 用大模型(3b)保证质量，Critic 用小模型(0.5b)节省延迟
 *   4. 涌现行为: 两个不同视角的碰撞产生更好的结果
 *
 * 对比:
 *   Reflection (2.3): 同一模型审视自己 → "当局者迷"
 *   MultiAgent (2.4): 不同角色互相批评 → "旁观者清"
 */

#include "multi_agent.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>

using json = nlohmann::json;

// ── curl 回调 ─────────────────────────────────────────

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* str = static_cast<std::string*>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

// ── 构造 ──────────────────────────────────────────────

MultiAgentEngine::MultiAgentEngine(const std::string& ollama_host)
    : host_(ollama_host)
{}

// ── 让一个 Agent 发言 ─────────────────────────────────

std::string MultiAgentEngine::ask_agent(
    const std::string& /*system_prompt*/,
    const std::string& model,
    const nlohmann::json& messages)
{
    json body;
    body["model"] = model;
    body["messages"] = messages;
    body["stream"] = false;
    body["options"] = {{"temperature", 0.2}};

    std::string response;
    try {
        response = http_post(body.dump());
    } catch (const std::exception& e) {
        std::cerr << "   [MultiAgent] ❌ HTTP 错误: " << e.what() << std::endl;
        return "";
    }

    try {
        json resp = json::parse(response);
        return resp.value("message", json::object()).value("content", "");
    } catch (...) {
        return "";
    }
}

// ── 主循环：两个 Agent 对话 ───────────────────────────

MultiAgentResult MultiAgentEngine::collaborate(
    const std::string& user_question,
    const std::string& initial_reply,
    const std::string& tool_context,
    const std::string& personality,
    const std::string& generator_model,
    const std::string& critic_model,
    int max_rounds)
{
    MultiAgentResult result;
    result.final_answer = initial_reply;

    std::string current_reply = initial_reply;

    // ── 构建 Agent 系统提示 ──────────────────────────

    // Generator: 基于初始回复 + 批评意见改进
    std::ostringstream gen_prompt;
    gen_prompt << "你是一个对话助手";
    if (!personality.empty()) {
        gen_prompt << "，" << personality;
    }
    gen_prompt << "。\n\n";
    gen_prompt << "用户问: " << user_question << "\n";
    if (!tool_context.empty()) {
        gen_prompt << "参考信息: " << tool_context << "\n";
    }
    gen_prompt << "\n你的任务是: 根据批评者的建议，改进你的回复，使其更准确、更自然、更有帮助。\n";
    gen_prompt << "如果批评者说\"OK\"或\"无问题\"，就保持当前回复不变。\n";
    gen_prompt << "只输出改进后的回复文本，不要加任何前缀或解释。";

    // Critic: 审查回复，找问题
    std::ostringstream crit_prompt;
    crit_prompt << "你是一个严格但公正的回复审查员。你的任务是审查 AI 助手的回复，找出可以改进的地方。\n\n";
    crit_prompt << "## 用户原始问题\n" << user_question << "\n\n";
    if (!tool_context.empty()) {
        crit_prompt << "## 工具查询结果（事实依据）\n" << tool_context << "\n\n";
    }
    crit_prompt << "## 审查标准\n";
    crit_prompt << "1. 事实是否与工具结果一致？有没有编造？\n";
    crit_prompt << "2. 语气是否自然、符合人设？\n";
    crit_prompt << "3. 是否完整回答了用户问题？\n";
    crit_prompt << "4. 长度是否合适？\n\n";
    crit_prompt << "## 审查要求\n";
    crit_prompt << "- 如果回复很好，只说\"OK\"\n";
    crit_prompt << "- 如果有问题，用 1-2 句简洁指出具体问题和改进建议\n";
    crit_prompt << "- 只输出审查意见，不要输出其他内容";

    std::cout << "   [MultiAgent] 🤝 双 Agent 协作开始 (最多 "
              << max_rounds << " 轮)..." << std::endl;

    // ── 对话循环 ──────────────────────────────────────
    for (int round = 0; round < max_rounds; ++round) {
        // ── Critic 审查当前回复 ──────────────────────
        json crit_msgs = json::array();
        crit_msgs.push_back({{"role", "system"}, {"content", crit_prompt.str()}});
        crit_msgs.push_back({{"role", "user"},
            {"content", "请审查以下回复:\n\n" + current_reply}});

        std::string feedback = ask_agent(crit_prompt.str(), critic_model, crit_msgs);

        if (feedback.empty()) {
            std::cout << "   [MultiAgent] ⚠️ Critic 无响应，保持当前回复" << std::endl;
            break;
        }

        // 检查是否需要改进
        bool is_ok = false;
        {
            std::string lower = feedback;
            for (auto& c : lower) c = std::tolower(c);
            // Critic 说 OK 或类似的话 → 不需要改进
            if (lower.find("ok") != std::string::npos ||
                lower.find("无问题") != std::string::npos ||
                lower.find("没问题") != std::string::npos ||
                lower.find("无需") != std::string::npos ||
                lower.find("很好") != std::string::npos ||
                lower.find("不错") != std::string::npos ||
                (lower.find("问题") == std::string::npos &&
                 lower.find("改进") == std::string::npos &&
                 lower.find("错误") == std::string::npos &&
                 lower.find("修正") == std::string::npos)) {
                is_ok = true;
            }
        }

        result.transcript.push_back({"Critic", feedback});
        std::cout << "   [MultiAgent] Round " << (round+1)
                  << " Critic: " << feedback.substr(0, 60)
                  << (feedback.size() > 60 ? "..." : "") << std::endl;

        if (is_ok) {
            result.final_answer = current_reply;
            result.rounds = round + 1;
            result.improved = (round > 0);
            std::cout << "   [MultiAgent] ✅ Critic 满意，"
                      << (result.improved ? "已优化" : "无需改进") << std::endl;
            return result;
        }

        // ── Generator 根据批评改进 ───────────────────
        json gen_msgs = json::array();
        gen_msgs.push_back({{"role", "system"}, {"content", gen_prompt.str()}});
        gen_msgs.push_back({{"role", "user"},
            {"content", "当前回复: " + current_reply
             + "\n\n批评者建议: " + feedback
             + "\n\n请给出改进后的回复:"}});

        std::string improved = ask_agent(gen_prompt.str(), generator_model, gen_msgs);

        if (improved.empty()) {
            std::cout << "   [MultiAgent] ⚠️ Generator 无响应，保持当前回复" << std::endl;
            break;
        }

        result.transcript.push_back({"Generator", improved});
        std::cout << "   [MultiAgent] Round " << (round+1)
                  << " Generator: " << improved.substr(0, 60)
                  << (improved.size() > 60 ? "..." : "") << std::endl;

        current_reply = improved;
    }

    result.final_answer = current_reply;
    result.rounds = max_rounds;
    result.improved = true;

    std::cout << "   [MultiAgent] 🏁 协作完成，共 "
              << result.rounds << " 轮" << std::endl;

    return result;
}

// ── HTTP POST ─────────────────────────────────────────

std::string MultiAgentEngine::http_post(const std::string& request_json) const
{
    std::string url = host_ + "/api/chat";
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init 失败");
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)request_json.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec_);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("curl 错误: ") + curl_easy_strerror(res));
    }

    return response;
}
