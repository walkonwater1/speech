/**
 * Reflection 反思引擎 — 实现
 *
 * 学习要点:
 *   1. Reflection 是提升 LLM 输出质量的有效手段
 *   2. 给 LLM 一个"元认知"机会 — 让它检查自己是否有错
 *   3. 两层检查: 事实准确性（hard） + 表达质量（soft）
 *   4. 成本: 额外一次 LLM 调用（可用更小的模型降低成本）
 *
 * 为什么有效:
 *   - LLM 生成时是"单向前向"的，没有机会回头看
 *   - 反思让 LLM 切换角色: 从"作者"变成"编辑"
 *   - 类似人类的"写一遍、读一遍、改一遍"
 */

#include "reflection.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>
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

ReflectionEngine::ReflectionEngine(const std::string& ollama_host,
                                   const std::string& model)
    : host_(ollama_host)
    , model_(model)
{}

// ── 核心：反思循环 ────────────────────────────────────

ReflectionResult ReflectionEngine::reflect(
    const std::string& user_question,
    const std::string& original_reply,
    const std::string& tool_context,
    const std::string& personality)
{
    ReflectionResult result;
    result.original = original_reply;

    // 空回复不反思
    if (original_reply.empty()) {
        result.improved = original_reply;
        return result;
    }

    // 1. 构建反思 prompt
    std::string sys_prompt = build_reflection_prompt(
        user_question, original_reply, tool_context, personality);

    // 2. 发送给 LLM 审查
    json body;
    body["model"] = model_;
    body["stream"] = false;
    body["options"] = {{"temperature", 0.0}};  // 批评需要确定性
    body["messages"] = json::array({
        {{"role", "system"}, {"content", sys_prompt}},
        {{"role", "user"},   {"content", "请审查以上回复。只输出审查结果的 JSON。"}}
    });

    std::string response;
    try {
        response = http_post(body.dump());
    } catch (const std::exception& e) {
        std::cerr << "   [Reflect] ❌ HTTP 错误: " << e.what() << std::endl;
        result.improved = original_reply;  // 反思失败，保持原样
        return result;
    }

    // 3. 解析审查结果
    try {
        json resp = json::parse(response);
        std::string content = resp.value("message", json::object())
                                   .value("content", "");

        // 提取 JSON
        size_t brace_start = content.find('{');
        size_t brace_end   = content.rfind('}');
        std::string json_str;
        if (brace_start != std::string::npos && brace_end != std::string::npos) {
            json_str = content.substr(brace_start, brace_end - brace_start + 1);
        } else {
            json_str = content;
        }

        json review = json::parse(json_str);

        result.critique = review.value("critique", "");
        result.had_issues = review.value("has_issues", false);

        if (result.had_issues) {
            result.improved = review.value("improved", original_reply);
            std::cout << "   [Reflect] 🔍 发现问题: " << result.critique.substr(0, 80)
                      << (result.critique.size() > 80 ? "..." : "") << std::endl;
            std::cout << "   [Reflect] ✏️  已修正: \""
                      << result.improved.substr(0, 60)
                      << (result.improved.size() > 60 ? "..." : "")
                      << "\"" << std::endl;
        } else {
            result.improved = original_reply;
            LOG_INFO("   [Reflect] ✅ 审查通过，无需修正");
        }

    } catch (const std::exception& e) {
        std::cerr << "   [Reflect] ⚠️ JSON 解析失败: " << e.what()
                  << "，保持原始回复" << std::endl;
        result.improved = original_reply;
    }

    return result;
}

// ── Prompt 构建 ───────────────────────────────────────

std::string ReflectionEngine::build_reflection_prompt(
    const std::string& user_question,
    const std::string& original_reply,
    const std::string& tool_context,
    const std::string& personality)
{
    std::ostringstream ss;

    ss << "你是一个严格的质量审查员。审查以下 AI 助手的回复，找出问题并修正。\n\n";

    ss << "## 用户原始问题\n";
    ss << user_question << "\n\n";

    if (!tool_context.empty()) {
        ss << "## 工具查询结果（事实依据）\n";
        ss << tool_context << "\n\n";
    }

    if (!personality.empty()) {
        ss << "## 助手人设\n";
        ss << personality << "\n\n";
    }

    ss << "## 待审查的回复\n";
    ss << original_reply << "\n\n";

    ss << "## 审查标准\n";
    ss << "1. **事实准确性**: 回复是否与工具查询结果一致？有没有编造数字或事实？\n";
    ss << "2. **完整性**: 是否完全回答了用户的问题？有没有遗漏关键信息？\n";
    ss << "3. **语气自然度**: 是否符合人设？对话语气是否自然、不生硬？\n";
    ss << "4. **简洁度**: 是否冗长啰嗦？能否更精炼？\n\n";

    ss << "## 输出格式\n";
    ss << "如果回复良好，无需修改:\n";
    ss << R"({"has_issues": false, "critique": "无问题"})" << "\n\n";
    ss << "如果发现问题，需要修正:\n";
    ss << R"({"has_issues": true, "critique": "具体问题描述", "improved": "修正后的完整回复"})" << "\n\n";
    ss << "注意:\n";
    ss << "- improved 必须是完整的回复文本，不是片段\n";
    ss << "- critique 要具体，指出哪里有问题\n";
    ss << "- 只输出 JSON，不要加 markdown 代码块标记\n";

    return ss.str();
}

// ── HTTP POST ─────────────────────────────────────────

std::string ReflectionEngine::http_post(const std::string& request_json) const
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
