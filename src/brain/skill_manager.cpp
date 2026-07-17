/**
 * 技能管理器 — 实现
 *
 * 天气:  wttr.in API (免费，无需注册)
 * 时间:  localtime(3)
 * 搜索:  DuckDuckGo Lite (HTML 抓取)
 */

#include "skill_manager.h"

#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>
#include <iostream>
#include <regex>

// ── 字符串工具 ────────────────────────────────────────

static std::string to_lower(const std::string& s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

static bool contains_any(const std::string& text,
                         const std::vector<std::string>& keywords)
{
    std::string lower = to_lower(text);
    for (auto& kw : keywords) {
        if (lower.find(to_lower(kw)) != std::string::npos)
            return true;
    }
    return false;
}

// ── libcurl 回调 ──────────────────────────────────────

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* str = static_cast<std::string*>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string curl_get(const std::string& url)
{
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "   [Skill] HTTP 请求失败: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    return response;
}

// ── 天气技能 ──────────────────────────────────────────

bool WeatherSkill::match(const std::string& text)
{
    // 天气相关关键词
    static const std::vector<std::string> keywords = {
        "天气", "气温", "温度", "下雨", "下雪", "刮风",
        "多少度", "冷不冷", "热不热", "晴", "阴天",
        "多云", "暴雨", "台风", "雾霾", "空气质量",
        "湿度", "风力", "紫外线", "天气预报"
    };
    return contains_any(text, keywords);
}

std::string WeatherSkill::execute(const std::string& text)
{
    std::string city = extract_city(text);
    std::string url = "https://wttr.in/" + city + "?format=%C+%t+%h+%w&lang=zh";

    std::cout << "   [Skill:天气] 查询 " << city << " ..." << std::endl;

    std::string result = http_get(url);
    if (result.empty()) {
        return "天气查询失败，请稍后再试。";
    }

    // 清洗结果（去除换行、多余空格）
    std::string clean;
    for (char c : result) {
        if (c == '\n' || c == '\r') clean += ' ';
        else clean += c;
    }

    return city + " 天气: " + clean;
}

std::string WeatherSkill::extract_city(const std::string& text)
{
    // 常见城市名匹配（可扩展）
    static const std::vector<std::pair<std::string, std::string>> cities = {
        {"北京", "Beijing"}, {"上海", "Shanghai"}, {"广州", "Guangzhou"},
        {"深圳", "Shenzhen"}, {"杭州", "Hangzhou"}, {"成都", "Chengdu"},
        {"武汉", "Wuhan"},   {"南京", "Nanjing"},  {"西安", "Xi'an"},
        {"重庆", "Chongqing"}, {"天津", "Tianjin"}, {"苏州", "Suzhou"},
        {"长沙", "Changsha"}, {"郑州", "Zhengzhou"}, {"东莞", "Dongguan"},
        {"青岛", "Qingdao"}, {"厦门", "Xiamen"}, {"大连", "Dalian"},
        {"济南", "Jinan"}, {"合肥", "Hefei"}, {"福州", "Fuzhou"},
        {"昆明", "Kunming"}, {"哈尔滨", "Harbin"}, {"沈阳", "Shenyang"},
        {"台北", "Taipei"}, {"香港", "HongKong"},
    };

    for (auto& [cn, en] : cities) {
        if (text.find(cn) != std::string::npos)
            return en;
    }
    // 默认北京
    return "Beijing";
}

std::string WeatherSkill::http_get(const std::string& url)
{
    return curl_get(url);
}

// ── 时间技能 ──────────────────────────────────────────

bool TimeSkill::match(const std::string& text)
{
    static const std::vector<std::string> keywords = {
        "几点", "时间", "日期", "几号", "星期几",
        "今天几", "现在", "当前时间", "日历", "年月日"
    };
    return contains_any(text, keywords);
}

std::string TimeSkill::execute(const std::string& /*text*/)
{
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);

    static const char* weekdays[] = {
        "星期日", "星期一", "星期二", "星期三",
        "星期四", "星期五", "星期六"
    };

    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "现在是 %d年%d月%d日 %s %02d:%02d:%02d",
        local->tm_year + 1900,
        local->tm_mon + 1,
        local->tm_mday,
        weekdays[local->tm_wday],
        local->tm_hour,
        local->tm_min,
        local->tm_sec);

    std::cout << "   [Skill:时间] " << buf << std::endl;
    return std::string(buf);
}

// ── 网页搜索技能 ──────────────────────────────────────

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

    // DuckDuckGo Lite HTML 版（无需 JS，抓取友好）
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
    std::string html = http_get(url);

    if (html.empty()) {
        return "搜索失败，请稍后再试。";
    }

    // 简单解析 DuckDuckGo Lite 的搜索结果
    // 格式: <a rel="nofollow" href="..." class="result-link">标题</a>
    //       <span class="result-snippet">摘要</span>
    std::stringstream result;
    int count = 0;

    // 用简陋但够用的方式提取摘要
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
            // 清理 HTML 实体和一些标签
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

std::string WebSearchSkill::http_get(const std::string& url)
{
    return curl_get(url);
}

// ── 技能管理器 ────────────────────────────────────────

SkillManager::SkillManager()
{
    // 默认注册所有技能
    add_skill(std::make_unique<WeatherSkill>());
    add_skill(std::make_unique<TimeSkill>());
    add_skill(std::make_unique<WebSearchSkill>());
}

void SkillManager::add_skill(std::unique_ptr<Skill> skill)
{
    skills_.push_back(std::move(skill));
}

void SkillManager::set_enabled(const std::string& name, bool enabled)
{
    for (auto& s : skills_) {
        if (s->name() == name) {
            s->set_enabled(enabled);
            return;
        }
    }
}

SkillResult SkillManager::detect_and_execute(const std::string& user_text)
{
    for (auto& s : skills_) {
        if (!s->enabled()) continue;

        if (s->match(user_text)) {
            std::string result = s->execute(user_text);
            if (!result.empty()) {
                return {true, s->name(), result};
            }
        }
    }
    return {false, "", ""};
}

std::string SkillManager::get_skills_context() const
{
    std::stringstream ss;
    for (auto& s : skills_) {
        if (!s->enabled()) continue;
        std::string desc = s->describe();
        if (!desc.empty()) {
            ss << "- " << desc << "\n";
        }
    }
    return ss.str();
}

std::string SkillManager::get_system_context()
{
    // 总是可用的系统信息（时间等）
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);

    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "当前时间: %d-%02d-%02d %02d:%02d",
        local->tm_year + 1900,
        local->tm_mon + 1,
        local->tm_mday,
        local->tm_hour,
        local->tm_min);

    return std::string(buf);
}
