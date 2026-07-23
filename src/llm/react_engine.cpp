/**
 * ReAct 推理引擎 — 实现
 *
 * 学习要点:
 *   1. ReAct = Reasoning + Acting — Agent 完成复杂任务的核心模式
 *   2. 多轮对话 — 每步工具调用 = 一轮 LLM 对话
 *   3. 安全上限 — max_steps 避免死循环
 *   4. Prompt Engineering — 清晰的结构化输出格式是关键
 *
 * 对比:
 *   Function Calling (2.1):  User → [选工具] → 执行 → LLM回复（单步）
 *   ReAct          (2.2):  User → think→act→observe→think...→final（多步）
 *
 *   单步只能: "查天气" → 回复
 *   多步可以: "查天气" → 发结果 → "天气好，再查景点" → 综合建议
 */

#include "react_engine.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>
#include <algorithm>
#include "logger.h"

using json = nlohmann::json;

// ── curl 回调 ─────────────────────────────────────────

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* str = static_cast<std::string*>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

// ── 构造 ──────────────────────────────────────────────

ReActEngine::ReActEngine(const std::string& ollama_host,
                         const std::string& model,
                         const std::string& system_prompt)
    : host_(ollama_host)
    , model_(model)
    , system_prompt_(system_prompt)
{}

// ── 主循环 ────────────────────────────────────────────

ReActResult ReActEngine::run(
    const std::string& user_question,
    const std::vector<FunctionDef>& tools,
    std::function<std::string(const std::string&, const nlohmann::json&)> execute_tool,
    const std::string& history_context,
    int max_steps)
{
    ReActResult result;

    if (tools.empty()) {
        result.success = false;
        result.final_answer = "";
        return result;
    }

    // 构建 ReAct system prompt
    std::string sys_prompt = build_react_prompt(system_prompt_, tools);

    // 构建消息历史（多轮对话）
    json messages = json::array();

    // System prompt
    messages.push_back({{"role", "system"}, {"content", sys_prompt}});

    // 第一个 user 消息：历史上下文 + 当前问题
    std::string first_msg;
    if (!history_context.empty()) {
        first_msg = "对话历史:\n" + history_context + "\n\n用户当前问题: " + user_question;
    } else {
        first_msg = user_question;
    }
    messages.push_back({{"role", "user"}, {"content", first_msg}});

    std::cout << "   [ReAct] 🧠 开始推理 (最多 " << max_steps << " 步)..." << std::endl;

    // ── 推理循环 ──────────────────────────────────────
    for (int step = 0; step < max_steps; ++step) {
        // 1. 调用 LLM
        json body;
        body["model"] = model_;
        body["messages"] = messages;
        body["stream"] = false;
        body["options"] = {{"temperature", 0.1}};  // 低温度 → 更确定

        std::string response;
        try {
            response = http_post(body.dump());
        } catch (const std::exception& e) {
            std::cerr << "   [ReAct] ❌ HTTP 错误 (step " << (step+1)
                      << "): " << e.what() << std::endl;
            result.success = false;
            return result;
        }

        // 2. 解析 LLM 输出
        std::string llm_content;
        try {
            json resp = json::parse(response);
            llm_content = resp.value("message", json::object())
                               .value("content", "");
        } catch (...) {
            std::cerr << "   [ReAct] ❌ 响应解析失败 (step " << (step+1) << ")" << std::endl;
            continue;  // 尝试下一步
        }

        auto parsed = parse_step(llm_content);

        if (parsed.parse_error) {
            // 纯文本无 JSON → 大概率是闲聊回复，当作 final answer
            if (llm_content.find('{') == std::string::npos) {
                result.success = true;
                result.final_answer = llm_content;
                result.total_steps = step + 1;
                std::cout << "   [ReAct] 💬 闲聊模式 → \""
                          << llm_content.substr(0, 50)
                          << (llm_content.size() > 50 ? "..." : "") << "\"" << std::endl;
                return result;
            }

            std::cerr << "   [ReAct] ⚠️ JSON 格式错误 (step " << (step+1)
                      << "): " << llm_content.substr(0, 80)
                      << (llm_content.size() > 80 ? "..." : "") << std::endl;
            // 重试
            messages.push_back({{"role", "user"},
                {"content", "请严格按 JSON 格式输出。需要工具时: {\"thought\":\"推理\",\"action\":\"工具名\",\"args\":{参数}}。可回答时: {\"thought\":\"推理\",\"final\":\"回复\"}"}});
            continue;
        }

        // 3. 判断：最终答案 or 继续调用工具？
        if (!parsed.final_answer.empty()) {
            // ✅ 最终答案
            result.success = true;
            result.final_answer = parsed.final_answer;
            result.total_steps = step + 1;

            std::cout << "   [ReAct] ✅ 完成 ("
                      << result.total_steps << " 步) → \""
                      << result.final_answer.substr(0, 60)
                      << (result.final_answer.size() > 60 ? "..." : "")
                      << "\"" << std::endl;
            return result;
        }

        if (parsed.action.empty()) {
            // 既没有 action 也没有 final → 可能是闲聊
            // 取 thought 作为回复
            result.success = true;
            result.final_answer = parsed.thought.empty()
                ? llm_content
                : parsed.thought;
            result.total_steps = step + 1;
            LOG_INFO("   [ReAct] ⚠️ 无 action/final，当作闲聊回复");
            return result;
        }

        // 4. 🔧 调用工具
        std::cout << "   [ReAct] Step " << (step+1)
                  << ": " << parsed.thought.substr(0, 40)
                  << " → " << parsed.action
                  << "(" << parsed.args.dump() << ")" << std::endl;

        std::string obs;
        try {
            obs = execute_tool(parsed.action, parsed.args);
        } catch (const std::exception& e) {
            obs = std::string("工具执行失败: ") + e.what();
        }

        if (obs.empty()) {
            obs = "工具执行完成，但无返回结果。";
        }

        // 记录步骤
        ReActStep rs;
        rs.thought = parsed.thought;
        rs.action = parsed.action;
        rs.args = parsed.args;
        rs.observation = obs;
        result.steps.push_back(rs);

        // 5. 将 assistant 输出 + 工具结果加入对话历史
        messages.push_back({
            {"role", "assistant"},
            {"content", llm_content}
        });

        messages.push_back({
            {"role", "user"},
            {"content", "[工具 " + parsed.action + " 的执行结果]:\n" + obs
                      + "\n\n请继续推理。如果信息足够回答用户，输出 final；否则继续调用工具。"}
        });
    }

    // 达到最大步数但仍无最终答案 → 强制要求 LLM 总结
    std::cout << "   [ReAct] ⚠️ 达到最大步数 (" << max_steps
              << ")，强制总结..." << std::endl;

    messages.push_back({
        {"role", "user"},
        {"content", "已达到最大步数限制。请基于以上所有信息，给出你对用户问题 \""
                  + user_question + "\" 的最终回答。只输出 {\"final\":\"你的回答\"}"}
    });

    try {
        json body;
        body["model"] = model_;
        body["messages"] = messages;
        body["stream"] = false;
        body["options"] = {{"temperature", 0.3}};

        std::string response = http_post(body.dump());
        json resp = json::parse(response);
        std::string content = resp.value("message", json::object())
                                   .value("content", "");

        auto parsed = parse_step(content);
        result.final_answer = parsed.final_answer.empty()
            ? content
            : parsed.final_answer;
        result.success = true;
        result.total_steps = max_steps;

    } catch (const std::exception& e) {
        std::cerr << "   [ReAct] ❌ 强制总结失败: " << e.what() << std::endl;
        result.success = false;
    }

    return result;
}

// ── System Prompt 构建 ────────────────────────────────

std::string ReActEngine::build_react_prompt(
    const std::string& base_prompt,
    const std::vector<FunctionDef>& tools)
{
    std::ostringstream ss;

    // 基础人设
    if (!base_prompt.empty()) {
        ss << base_prompt << "\n\n";
    }

    // ReAct 指令
    ss << "--- 工具使用规则 (ReAct) ---\n\n";
    ss << "你可以使用以下工具来获取信息。你需要通过多步推理来回答复杂问题。\n\n";

    // 工具列表（简化格式，节省 token）
    ss << "可用工具:\n";
    for (auto& t : tools) {
        ss << "- **" << t.name << "**: " << t.description << "\n";
        if (!t.parameters.empty() && t.parameters.contains("properties")) {
            ss << "  参数: " << t.parameters["properties"].dump() << "\n";
        }
    }
    ss << "\n";

    // 输出格式
    ss << "输出格式（必须是纯 JSON，不要加 markdown 代码块标记）:\n";
    ss << "- 需要调用工具时: {\"thought\":\"你的推理过程\",\"action\":\"工具名\",\"args\":{\"参数名\":\"参数值\"}}\n";
    ss << "- 可以回答时: {\"thought\":\"推理总结\",\"final\":\"你的回复\"}\n\n";

    // 规则
    ss << "规则:\n";
    ss << "1. 从用户消息中直接提取参数值（城市名、日期等），绝对不要反问用户已经提供的信息\n";
    ss << "2. 参数确实缺失时才问，每次只问最关键的缺失信息\n";
    ss << "3. 每次只能输出一个 JSON，调用一个工具\n";
    ss << "4. 收到工具结果后，结合原问题综合判断是否还需要更多信息\n";
    ss << "5. 用户询问天气/时间等实时数据时：必须先调用工具获取，禁止自己编造或猜测\n";
    ss << "6. 信息足够时用 final 给出简洁口语化回复，不要画蛇添足（比如不要追问\"还有什么需要帮忙的吗\"）\n";
    ss << "7. 用中文思考和回答，回复要像朋友聊天一样自然\n";

    return ss.str();
}

// ── JSON 解析 ─────────────────────────────────────────

ReActEngine::ParsedStep ReActEngine::parse_step(const std::string& llm_output)
{
    ParsedStep result;

    // 0. 预处理：修复 LLM 常见 JSON 格式错误（在提取前修复）
    // qwen 常见错误: "args:{" → "args":{（键的闭合引号缺失）
    // 这会导致 brace balancing 失效，必须提前修复
    std::string fixed_output = llm_output;
    {
        size_t pos = 0;
        while ((pos = fixed_output.find("\"args:{", pos)) != std::string::npos) {
            fixed_output.replace(pos, 7, "\"args\":{");
            pos += 8;
        }
    }

    // 1. 提取第一个完整的 JSON 对象
    std::string json_str;
    size_t brace_start = fixed_output.find('{');
    if (brace_start == std::string::npos) {
        result.parse_error = true;
        return result;
    }

    // 匹配平衡的大括号
    int depth = 0;
    size_t brace_end = std::string::npos;
    bool in_string = false;
    for (size_t i = brace_start; i < fixed_output.size(); ++i) {
        char c = fixed_output[i];
        if (c == '\\' && in_string) { ++i; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        if (in_string) continue;
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) { brace_end = i; break; }
        }
    }

    if (brace_end == std::string::npos) {
        result.parse_error = true;
        return result;
    }

    json_str = fixed_output.substr(brace_start, brace_end - brace_start + 1);

    // 2. 解析 JSON
    json j;
    try {
        j = json::parse(json_str);
    } catch (...) {
        result.parse_error = true;
        return result;
    }

    // 3. 提取字段
    result.thought = j.value("thought", "");

    // 检查 final (最终答案)
    if (j.contains("final") && j["final"].is_string() && !j["final"].get<std::string>().empty()) {
        result.final_answer = j["final"].get<std::string>();
        return result;
    }

    // 检查 action (工具调用)
    if (j.contains("action") && j["action"].is_string()) {
        result.action = j["action"].get<std::string>();
        result.args = j.value("args", json::object());
        return result;
    }

    // 既没有 final 也没有 action → 解析失败
    result.parse_error = true;
    return result;
}

// ── HTTP POST ─────────────────────────────────────────

std::string ReActEngine::http_post(const std::string& request_json) const
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
