#pragma once
/**
 * 文档切分器 — 将长文档切成可检索的片段 (chunks)
 *
 * 学习要点:
 *   1. LLM 上下文窗口有限 → 不能把整本书塞进去
 *   2. Chunk 策略影响检索质量:
 *      - 太小: 缺少上下文，搜出来的片段看不懂
 *      - 太大: 包含太多无关信息，稀释相关性
 *      - 重叠: 避免关键信息刚好被切在两段之间
 *   3. 生产环境通常用 LangChain RecursiveCharacterTextSplitter
 *
 * 使用方式:
 *   auto chunks = DocumentChunker::split("长文档内容...", 256, 32);
 *   // 每个 chunk 约 256 汉字，相邻 chunk 重叠 32 汉字
 */

#include <string>
#include <vector>

class DocumentChunker {
public:
    /// 将文本切分为重叠的 chunk
    /// @param text            原始文本
    /// @param chunk_size      每段目标字符数
    /// @param overlap         相邻段重叠字符数
    /// @param separators      优先在这些分隔符处断句（按优先级排列）
    static std::vector<std::string> split(
        const std::string& text,
        int chunk_size = 256,
        int overlap = 32,
        const std::vector<std::string>& separators = {
            "\n\n", "\n", "。", "！", "？", "；", "，", " "
        });

private:
    /// 在给定位置附近找最佳断句点
    static size_t find_split_point(const std::string& text,
                                   size_t target,
                                   const std::vector<std::string>& separators);
};
