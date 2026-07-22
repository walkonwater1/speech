/**
 * 网页搜索技能 — 实现
 *
 * DuckDuckGo Lite HTML 版（无需 JS，抓取友好）。
 */

#include "skill_search.h"
#include "skill_utils.h"
#include "http_client.h"
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cctype>

bool WebSearchSkill::match(const std::string& text)
{
    static const std::vector<std::string> keywords = {
        "搜索", "搜一下", "帮我查", "帮我搜", "上网查",
        "百度", "谷歌", "查一下", "帮我找", "查找"
    };
    return contains_any(text, keywords);
}

std::string WebSearchSkill::execute(const std::string& text)
{
    // 提取搜索关键词（去除指令词）
    std::string query = text;
    static const std::vector<std::string> prefixes = {
        "搜索", "搜一下", "帮我查", "帮我搜", "上网查", "查一下", "帮我找", "查找"
    };
    for (auto& p : prefixes) {
        size_t pos = query.find(p);
        if (pos != std::string::npos) {
            query = query.substr(pos + p.length());
            break;
        }
    }
    // 去除首尾空白
    auto start = query.find_first_not_of(" \t\r\n，,。.");
    auto end   = query.find_last_not_of(" \t\r\n，,。.");
    if (start != std::string::npos)
        query = query.substr(start, end - start + 1);
    else
        query = text;

    std::cout << "   [Skill:搜索] " << query << " ..." << std::endl;

    // URL 编码
    std::string encoded;
    for (unsigned char c : query) {
        if (std::isalnum(c) || c == ' ') {
            if (c == ' ') encoded += '+';
            else encoded += c;
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }

    std::string url = "https://lite.duckduckgo.com/lite/?q=" + encoded;
    std::string html;
    try {
        html = HttpClient::get(url);
    } catch (const std::exception& e) {
        std::cerr << "   [Skill] HTTP 请求失败: " << e.what() << std::endl;
        return "搜索失败，请稍后再试。";
    }

    if (html.empty()) {
        return "搜索失败，请稍后再试。";
    }

    // 简单解析 DuckDuckGo Lite 的搜索结果
    std::stringstream result;
    int count = 0;

    size_t pos = 0;
    while (count < 3) {
        // 找标题
        size_t link_start = html.find("result-link", pos);
        if (link_start == std::string::npos) break;
        size_t link_text = html.find('>', link_start);
        if (link_text == std::string::npos) break;
        size_t link_end = html.find("</a>", link_text);
        if (link_end == std::string::npos) break;

        std::string title = html.substr(link_text + 1, link_end - link_text - 1);

        // 找摘要
        size_t snip_start = html.find("result-snippet", link_end);
        std::string snippet;
        if (snip_start != std::string::npos && snip_start - link_end < 500) {
            size_t snip_text = html.find('>', snip_start);
            size_t snip_end = html.find("</span>", snip_text);
            if (snip_text != std::string::npos && snip_end != std::string::npos) {
                snippet = html.substr(snip_text + 1, snip_end - snip_text - 1);
            }
        }

        if (count > 0) result << "; ";
        result << title;
        if (!snippet.empty()) {
            // 清理 HTML 标签
            std::string clean_snip;
            bool in_tag = false;
            for (size_t i = 0; i < snippet.size(); ++i) {
                if (snippet[i] == '<') in_tag = true;
                else if (snippet[i] == '>') in_tag = false;
                else if (!in_tag) clean_snip += snippet[i];
            }
            if (clean_snip.size() > 100)
                clean_snip = clean_snip.substr(0, 100) + "...";
            result << " - " << clean_snip;
        }

        pos = snip_start > 0 ? snip_start : link_end;
        ++count;
    }

    if (count == 0) {
        return "没有搜索到 \"" + query + "\" 的相关结果。";
    }

    std::cout << "   [Skill:搜索] 找到 " << count << " 条结果" << std::endl;
    return "关于 \"" + query + "\" 的搜索结果: " + result.str();
}
