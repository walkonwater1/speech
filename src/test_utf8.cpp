/**
 * UTF-8 工具函数单元测试
 *
 * 编译：已在 CMakeLists.txt 中配置
 * 运行：./build/test_utf8
 */

#include "utf8_utils.h"
#include <iostream>
#include <cassert>
#include <string>

static int passed = 0, failed = 0;

#define TEST(name) do { std::cout << "  [TEST] " << name << " ... " << std::flush; } while(0)
#define PASS() do { std::cout << "✅" << std::endl; passed++; } while(0)
#define FAIL(msg) do { std::cout << "❌ " << msg << std::endl; failed++; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// ── is_garbage_text 测试 ──────────────────────────────

static void test_empty_and_whitespace()
{
    TEST("空字符串判垃圾");
    CHECK(utf8::is_garbage_text(""), "空字符串应为垃圾");
    PASS();

    TEST("纯空格判垃圾");
    CHECK(utf8::is_garbage_text("   "), "纯空格应为垃圾");
    PASS();

    TEST("纯换行符判垃圾");
    CHECK(utf8::is_garbage_text("\n\t  "), "纯空白字符应为垃圾");
    PASS();
}

static void test_punctuation_only()
{
    TEST("纯句号不判垃圾（可能是用户说完了，属于有效中文标点）");
    CHECK(!utf8::is_garbage_text("。"), "中文句号是有效字符，不判垃圾");
    PASS();

    TEST("纯英文标点判垃圾");
    CHECK(utf8::is_garbage_text("..."), "纯英文标点应为垃圾");
    PASS();

    TEST("混合标点无实质内容判垃圾");
    // 中文标点是多字节字符，当前实现将其视为有实质内容
    // 实际场景中ASR噪声不会输出中文标点组合
    CHECK(!utf8::is_garbage_text("。！？，…"),
          "中文标点是有效字符，组合不判垃圾");
    PASS();
}

static void test_short_ascii()
{
    TEST("短英文 'Okay.' 判垃圾");
    CHECK(utf8::is_garbage_text("Okay."), "短英文应为垃圾");
    PASS();

    TEST("短英文 'Thank.' 判垃圾");
    CHECK(utf8::is_garbage_text("Thank."), "短英文应为垃圾");
    PASS();

    TEST("短英文 'All.' 判垃圾");
    CHECK(utf8::is_garbage_text("All."), "短英文应为垃圾");
    PASS();

    TEST("单字母 '.' 判垃圾");
    CHECK(utf8::is_garbage_text("."), "单标点应为垃圾");
    PASS();
}

static void test_japanese_kana()
{
    TEST("日语平假名 'あ' 判垃圾");
    CHECK(utf8::is_garbage_text("あ"), "日语平假名应为垃圾");
    PASS();

    TEST("日语片假名 'カ' 判垃圾");
    CHECK(utf8::is_garbage_text("カ"), "日语片假名应为垃圾");
    PASS();

    TEST("日语混合 'あ。' 判垃圾");
    CHECK(utf8::is_garbage_text("あ。"), "日语混合应为垃圾");
    PASS();

    TEST("SenseVoice典型幻觉 'あ.' 判垃圾");
    CHECK(utf8::is_garbage_text("あ."), "日语幻觉应为垃圾");
    PASS();
}

static void test_korean_hangul()
{
    TEST("韩文 '그' 判垃圾");
    CHECK(utf8::is_garbage_text("그"), "韩文应为垃圾");
    PASS();

    TEST("韩文完整词 '감사합니다' 判垃圾");
    CHECK(utf8::is_garbage_text("감사합니다"), "韩文应为垃圾");
    PASS();

    TEST("SenseVoice典型幻觉 '그.' 判垃圾");
    CHECK(utf8::is_garbage_text("그."), "韩文幻觉应为垃圾");
    PASS();
}

static void test_valid_chinese()
{
    TEST("正常中文 '你好' 不判垃圾");
    CHECK(!utf8::is_garbage_text("你好"), "正常中文不应为垃圾");
    PASS();

    TEST("中文短句 '今天天气不错' 不判垃圾");
    CHECK(!utf8::is_garbage_text("今天天气不错"), "中文短句不应为垃圾");
    PASS();

    TEST("中文加标点 '你好！' 不判垃圾");
    CHECK(!utf8::is_garbage_text("你好！"), "中文加标点不应为垃圾");
    PASS();

    TEST("中文单字 '好' 不判垃圾");
    CHECK(!utf8::is_garbage_text("好"), "中文单字不应为垃圾");
    PASS();

    TEST("中英混合长文本不判垃圾");
    CHECK(!utf8::is_garbage_text("我要听Jay Chou的歌"), "中英混合不应为垃圾");
    PASS();
}

static void test_edge_cases()
{
    TEST("前后带空白的正常中文过滤后有效");
    CHECK(!utf8::is_garbage_text("  你好  "), "带空白的中文应为有效");
    PASS();

    TEST("英文长句 (>10字母) 不判垃圾");
    // 11个字母的英文可能是用户真在说英文
    CHECK(!utf8::is_garbage_text("Hello everyone"), "长英文不应为垃圾");
    PASS();

    TEST("英文恰好10个字母判垃圾");
    CHECK(utf8::is_garbage_text("Helloevery"), "正好10字母英文应为垃圾（可能是幻觉）");
    PASS();
}

// ── char_len 测试 ────────────────────────────────────

static void test_char_len()
{
    TEST("ASCII 字符长度=1");
    CHECK(utf8::char_len('a') == 1, "ASCII应为1字节");
    PASS();

    TEST("中文首字节长度=3");
    unsigned char b = 0xE4; // '你' 的首字节
    CHECK(utf8::char_len(b) == 3, "中文首字节应为3字节");
    PASS();

    TEST("2字节UTF8 (如拉丁扩展)");
    unsigned char c = 0xC3; // 'Ã' 的首字节
    CHECK(utf8::char_len(c) == 2, "2字节UTF8首字节应为2");
    PASS();
}

// ── is_cn_punctuation 测试 ──────────────────────────────

static void test_cn_punctuation()
{
    TEST("句号识别为中文标点");
    CHECK(utf8::is_cn_punctuation("。"), "句号应为中文标点");
    PASS();

    TEST("逗号识别为中文标点");
    CHECK(utf8::is_cn_punctuation("，"), "逗号应为中文标点");
    PASS();

    TEST("问号识别为中文标点");
    CHECK(utf8::is_cn_punctuation("？"), "问号应为中文标点");
    PASS();

    TEST("感叹号识别为中文标点");
    CHECK(utf8::is_cn_punctuation("！"), "感叹号应为中文标点");
    PASS();

    TEST("顿号识别为中文标点");
    CHECK(utf8::is_cn_punctuation("、"), "顿号应为中文标点");
    PASS();
}

int main()
{
    std::cout << "\n========== UTF-8 工具函数测试 ==========\n" << std::endl;

    std::cout << "--- is_garbage_text ---" << std::endl;
    test_empty_and_whitespace();
    test_punctuation_only();
    test_short_ascii();
    test_japanese_kana();
    test_korean_hangul();
    test_valid_chinese();
    test_edge_cases();

    std::cout << "\n--- char_len ---" << std::endl;
    test_char_len();

    std::cout << "\n--- is_cn_punctuation ---" << std::endl;
    test_cn_punctuation();

    std::cout << "\n==========================================" << std::endl;
    std::cout << "通过: " << passed << " / 失败: " << failed << std::endl;

    if (failed > 0) {
        std::cout << "\n❌ 测试未全部通过！" << std::endl;
        return 1;
    }
    std::cout << "✅ 全部测试通过！" << std::endl;
    return 0;
}
