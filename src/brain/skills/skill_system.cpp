#include "logger.h"
/**
 * 系统状态查询技能 — 实现
 */

#include "skill_system.h"
#include "skill_utils.h"

#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>

// ── 关键词匹配 ──────────────────────────────────────────

bool SystemSkill::match(const std::string& text)
{
    static const std::vector<std::string> keywords = {
        "cpu", "CPU", "温度", "内存", "磁盘", "硬盘",
        "空间", "还剩多少", "系统状态", "负载", "运行时间",
        "开机多久", "电量", "进程", "散热", "风扇"
    };
    return contains_any(text, keywords);
}

// ── 命令执行工具 ────────────────────────────────────────

std::string SystemSkill::exec_cmd(const char* cmd)
{
    std::string result;
    char buf[256];
    FILE* fp = popen(cmd, "r");
    if (!fp) return "";
    while (fgets(buf, sizeof(buf), fp)) {
        result += buf;
    }
    pclose(fp);
    // 去掉尾部换行
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

// ── CPU 温度 ────────────────────────────────────────────

std::string SystemSkill::get_cpu_temp()
{
    // 尝试多种路径（不同硬件/内核接口）
    std::ifstream f;
    std::string line;

    // 路径1: /sys/class/thermal/thermal_zone0/temp (常见)
    for (int i = 0; i < 10; ++i) {
        std::string path = "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp";
        f.open(path);
        if (f.is_open()) {
            std::getline(f, line);
            f.close();
            if (!line.empty()) {
                long temp = std::stol(line) / 1000;
                return std::to_string(temp) + "°C";
            }
        }
    }

    // 路径2: /sys/class/hwmon
    std::string hwmon_temp = exec_cmd(
        "cat /sys/class/hwmon/hwmon*/temp*_input 2>/dev/null | head -1");
    if (!hwmon_temp.empty()) {
        try {
            long t = std::stol(hwmon_temp) / 1000;
            return std::to_string(t) + "°C";
        } catch (...) {}
    }

    // 路径3: sensors 命令
    std::string sensors_out = exec_cmd("sensors 2>/dev/null | grep -E 'Package|Core 0|temp1' | head -3");
    if (!sensors_out.empty()) return sensors_out;

    return "无法读取CPU温度";
}

// ── 内存 ────────────────────────────────────────────────

std::string SystemSkill::get_memory()
{
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return "无法读取内存信息";

    long total = 0, avail = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            sscanf(line.c_str(), "MemTotal: %ld kB", &total);
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            sscanf(line.c_str(), "MemAvailable: %ld kB", &avail);
        }
    }
    f.close();

    long used = total - avail;
    int used_gb  = (int)(used / (1024 * 1024.0 * 100) + 0.5) / 100;
    int total_gb = (int)(total / (1024 * 1024.0 * 100) + 0.5) / 100;
    int pct = (int)(used * 100.0 / total + 0.5);

    std::ostringstream oss;
    oss << "内存: " << used_gb << "GB / " << total_gb
        << "GB (" << pct << "%)";
    if (pct < 30) oss << "，还很充裕";
    else if (pct < 60) oss << "，正常使用中";
    else if (pct < 85) oss << "，有点紧张了";
    else oss << "，快满了！";
    return oss.str();
}

// ── 磁盘 ────────────────────────────────────────────────

std::string SystemSkill::get_disk()
{
    std::string df = exec_cmd("df -h / 2>/dev/null | tail -1");
    if (df.empty()) return "无法读取磁盘信息";

    // 解析 "df -h" 输出: Filesystem Size Used Avail Use% Mounted on
    std::istringstream iss(df);
    std::string fs, size, used, avail, pct, mount;
    iss >> fs >> size >> used >> avail >> pct >> mount;

    std::ostringstream oss;
    oss << "磁盘(根分区): " << used << " / " << size
        << " 已用 (" << pct << "), " << avail << " 可用";

    // 数值化使用率
    std::string pct_num = pct;
    pct_num.pop_back();  // 去掉 '%'
    try {
        int p = std::stoi(pct_num);
        if (p < 50) oss << "，空间充足";
        else if (p < 80) oss << "，正常";
        else oss << "，空间不多了！";
    } catch (...) {}

    return oss.str();
}

// ── 系统负载 ────────────────────────────────────────────

std::string SystemSkill::get_load()
{
    std::ifstream f("/proc/loadavg");
    if (!f.is_open()) return "无法读取负载";
    std::string line;
    std::getline(f, line);

    float l1, l5, l15;
    sscanf(line.c_str(), "%f %f %f", &l1, &l5, &l15);

    std::ostringstream oss;
    oss << "系统负载: " << l1 << " (1分钟) / " << l5
        << " (5分钟) / " << l15 << " (15分钟)。";
    if (l1 < 1.0) oss << "几乎没有压力～";
    else if (l1 < 3.0) oss << "正常运行中";
    else oss << "有点忙了";
    return oss.str();
}

// ── 运行时间 ────────────────────────────────────────────

std::string SystemSkill::get_uptime()
{
    std::ifstream f("/proc/uptime");
    if (!f.is_open()) return "无法读取运行时间";
    double uptime_sec;
    f >> uptime_sec;
    f.close();

    int days  = (int)uptime_sec / 86400;
    int hours = ((int)uptime_sec % 86400) / 3600;
    int mins  = ((int)uptime_sec % 3600) / 60;

    std::ostringstream oss;
    oss << "已开机 ";
    if (days > 0) oss << days << "天";
    if (hours > 0) oss << hours << "小时";
    oss << mins << "分钟";
    return oss.str();
}

// ── 执行入口 ────────────────────────────────────────────

std::string SystemSkill::execute(const std::string& text)
{
    return execute(text, nlohmann::json::object());
}

std::string SystemSkill::execute(const std::string& text,
                                  const nlohmann::json& args)
{
    std::string type = "all";

    if (args.contains("query_type") && args["query_type"].is_string()) {
        type = args["query_type"].get<std::string>();
    } else {
        // 从文本推断查询类型
        if (text.find("温度") != std::string::npos ||
            text.find("cpu") != std::string::npos ||
            text.find("CPU") != std::string::npos) {
            type = "cpu_temp";
        } else if (text.find("内存") != std::string::npos) {
            type = "memory";
        } else if (text.find("磁盘") != std::string::npos ||
                   text.find("硬盘") != std::string::npos ||
                   text.find("空间") != std::string::npos) {
            type = "disk";
        } else if (text.find("负载") != std::string::npos) {
            type = "load";
        } else if (text.find("运行") != std::string::npos ||
                   text.find("开机") != std::string::npos) {
            type = "uptime";
        }
    }

    std::cout << "   [Skill:系统] 查询类型=" << type << std::endl;

    std::ostringstream oss;
    if (type == "cpu_temp") {
        oss << "CPU温度: " << get_cpu_temp();
    } else if (type == "memory") {
        oss << get_memory();
    } else if (type == "disk") {
        oss << get_disk();
    } else if (type == "load") {
        oss << get_load();
    } else if (type == "uptime") {
        oss << get_uptime();
    } else {
        // "all" — 整体概览
        oss << "📊 系统状态:\n"
            << "  CPU温度: " << get_cpu_temp() << "\n"
            << "  " << get_memory() << "\n"
            << "  " << get_disk() << "\n"
            << "  " << get_load() << "\n"
            << "  " << get_uptime();
    }
    return oss.str();
}
