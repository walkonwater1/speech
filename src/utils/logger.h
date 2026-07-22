/**
 * 结构化日志系统 — spdlog 封装
 *
 * 双输出: 控制台(彩色) + 文件(轮转, max 5MB x 3)
 *
 * 用法:
 *   #include "logger.h"
 *   LOG_INFO("模块初始化完成");
 *   LOG_WARN("配置项缺失，使用默认值");
 *   LOG_ERROR("连接失败: {}", error_msg);
 *   LOG_DEBUG("VAD 能量值: {:.4f}", rms);
 */

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>
#include <iostream>

// ── 便捷宏（必须在函数使用之前定义）──────────────────

#define LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...)  spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)  spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)

namespace voice {

/// 初始化日志系统（main 函数开始时调用一次）
inline void init_logger(const std::string& log_file = "voice_pipeline.log",
                        spdlog::level::level_enum level = spdlog::level::info)
{
    try {
        // 控制台 sink（带颜色）
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(level);
        console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

        // 文件 sink（轮转: 5MB x 3 文件）
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 5 * 1024 * 1024, 3);
        file_sink->set_level(spdlog::level::trace);  // 文件记录所有级别
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

        // 组合 logger
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("voice", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(logger);
        spdlog::set_level(level);

        spdlog::info("Logger initialized (file: {})", log_file);
    } catch (const spdlog::spdlog_ex& ex) {
        // 降级: spdlog 初始化失败时回退到 std::cerr
        std::cerr << "spdlog init failed: " << ex.what() << std::endl;
    }
}

} // namespace voice
