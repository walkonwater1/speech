/**
 * 技能管理器 & 技能单元测试
 *
 * 测试重点:
 *   1. 关键字匹配：各技能的 match() 是否正确触发
 *   2. 技能执行：execute() 返回非空有效结果
 *   3. 直接交付：is_direct_response() 标记正确
 *   4. 无匹配：随机文本不触发技能
 *
 * 编译：cmake --build build --target test_skills
 * 运行：./build/test_skills
 */

#include "skills/skill_time.h"
#include "skills/skill_calculator.h"
#include "skills/skill_poetry.h"
#include "skills/skill_fortune.h"
#include "skills/skill_entertainment.h"
#include "skills/skill_riddle.h"
#include "skills/skill_games.h"

#include <iostream>
#include <cassert>
#include <string>

static int passed = 0, failed = 0;

#define TEST(name) do { std::cout << "  [TEST] " << name << " ... " << std::flush; } while(0)
#define PASS() do { std::cout << "✅" << std::endl; passed++; } while(0)
#define FAIL(msg) do { std::cout << "❌ " << msg << std::endl; failed++; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// ── 时间技能 ───────────────────────────────────────────

static void test_time_skill()
{
    TimeSkill t;

    TEST("时间技能 match('现在几点')");
    CHECK(t.match("现在几点"), "应匹配时间查询");
    PASS();

    TEST("时间技能 match('今天几号')");
    CHECK(t.match("今天几号"), "应匹配日期查询");
    PASS();

    TEST("时间技能 match('几点了')");
    CHECK(t.match("几点了"), "应匹配简单时间查询");
    PASS();

    TEST("时间技能不匹配随机文本");
    CHECK(!t.match("你好"), "不应匹配无时间关键词文本");
    PASS();

    TEST("时间技能 execute 返回值非空");
    std::string r = t.execute("现在几点");
    CHECK(!r.empty(), "时间结果不应为空");
    PASS();

    TEST("时间技能不是 direct 响应");
    CHECK(!t.is_direct_response(), "时间结果需要LLM格式化");
    PASS();
}

// ── 计算器技能 ─────────────────────────────────────────

static void test_calculator_skill()
{
    CalculatorSkill c;

    TEST("计算器 match('1加1等于几')");
    CHECK(c.match("1加1等于几"), "应匹配计算请求（需要数字+运算符）");
    PASS();

    TEST("计算器 match('12乘以3是多少')");
    CHECK(c.match("12乘以3是多少"), "应匹配乘法");
    PASS();

    TEST("计算器 match('100除以5')");
    CHECK(c.match("100除以5"), "应匹配除法");
    PASS();

    TEST("计算器 execute 返回值非空");
    std::string r = c.execute("1加1等于几");
    CHECK(!r.empty(), "计算结果不应为空");
    PASS();

    TEST("计算器 纯中文数字不匹配（无阿拉伯数字）");
    CHECK(!c.match("一加一等于几"), "纯中文数字不应匹配（需要阿拉伯数字）");
    PASS();
}

// ── 诗词技能 ───────────────────────────────────────────

static void test_poetry_skill()
{
    PoetrySkill p;

    TEST("诗词 match('背一首李白的诗')");
    CHECK(p.match("背一首李白的诗"), "应匹配背诗请求");
    PASS();

    TEST("诗词 match('来首唐诗')");
    CHECK(p.match("来首唐诗"), "应匹配唐诗请求");
    PASS();

    TEST("诗词 match('推荐一首杜甫的诗')");
    CHECK(p.match("推荐一首杜甫的诗"), "应匹配推荐诗人");
    PASS();

    TEST("诗词 execute 返回值包含诗名");
    std::string r = p.execute("背一首李白的诗");
    CHECK(r.find("静夜思") != std::string::npos ||
          r.find("望庐山") != std::string::npos ||
          r.find("早发") != std::string::npos ||
          r.find("赠汪伦") != std::string::npos,
          "应返回李白的某首诗");
    PASS();

    TEST("诗词是 direct 响应（绕过LLM）");
    CHECK(p.is_direct_response(), "诗词应直接交付");
    PASS();
}

// ── 娱乐技能 ───────────────────────────────────────────

static void test_entertainment_skill()
{
    EntertainmentSkill e;

    TEST("娱乐 match('讲个笑话')");
    CHECK(e.match("讲个笑话"), "应匹配笑话请求");
    PASS();

    TEST("娱乐 match('说个段子')");
    CHECK(e.match("说个段子"), "应匹配段子请求");
    PASS();

    TEST("娱乐 match('讲个故事')");
    CHECK(e.match("讲个故事"), "应匹配故事请求");
    PASS();

    TEST("娱乐 match('说个有趣的事')");
    CHECK(e.match("说个有趣的事"), "应匹配趣闻请求");
    PASS();

    TEST("娱乐 match('来点毒鸡汤')");
    CHECK(e.match("来点毒鸡汤"), "应匹配毒鸡汤请求");
    PASS();

    TEST("娱乐 execute 返回值非空");
    std::string r = e.execute("讲个笑话");
    CHECK(!r.empty(), "笑话结果不应为空");
    PASS();

    TEST("娱乐是 direct 响应");
    CHECK(e.is_direct_response(), "娱乐内容应直接交付");
    PASS();
}

// ── 占卜技能 ───────────────────────────────────────────

static void test_fortune_skill()
{
    FortuneSkill f;

    TEST("占卜 match('帮我算一卦')");
    CHECK(f.match("帮我算一卦"), "应匹配算卦");
    PASS();

    TEST("占卜 match('天秤座今日运势')");
    CHECK(f.match("天秤座今日运势"), "应匹配星座运势");
    PASS();

    TEST("占卜 match('抽一张塔罗牌')");
    CHECK(f.match("抽一张塔罗牌"), "应匹配塔罗");
    PASS();

    TEST("占卜 match('今日黄历')");
    CHECK(f.match("今日黄历"), "应匹配黄历");
    PASS();

    TEST("占卜 execute 返回值非空");{
    std::string r = f.execute("帮我算一卦");
    CHECK(!r.empty(), "占卜结果不应为空");
    PASS();
}}
// ── 谜语技能 ───────────────────────────────────────────

static void test_riddle_skill()
{
    RiddleSkill r;

    TEST("谜语 match('出个谜语')");
    CHECK(r.match("出个谜语"), "应匹配谜语请求");
    PASS();

    TEST("谜语 match('猜谜语')");
    CHECK(r.match("猜谜语"), "应匹配猜谜");
    PASS();

    TEST("谜语 execute 返回值非空");
    std::string result = r.execute("出个谜语");
    CHECK(!result.empty(), "谜语结果不应为空");
    PASS();

    TEST("谜语是 direct 响应");
    CHECK(r.is_direct_response(), "谜语应直接交付");
    PASS();
}

// ── 游戏技能 ───────────────────────────────────────────

static void test_games_skill()
{
    GamesSkill g;

    TEST("游戏 match('猜数字')");
    CHECK(g.match("猜数字"), "应匹配猜数字游戏");
    PASS();

    TEST("游戏 match('成语接龙')");
    CHECK(g.match("成语接龙"), "应匹配成语接龙");
    PASS();

    TEST("游戏 execute 返回值非空");
    std::string r = g.execute("猜数字");
    CHECK(!r.empty(), "游戏结果不应为空");
    PASS();

    TEST("游戏是 direct 响应");
    CHECK(g.is_direct_response(), "游戏应直接交付");
    PASS();
}

// ── Main ───────────────────────────────────────────────

int main()
{
    std::cout << "\n========== 技能系统测试 ==========\n" << std::endl;

    std::cout << "--- 时间技能 ---" << std::endl;
    test_time_skill();

    std::cout << "\n--- 计算器技能 ---" << std::endl;
    test_calculator_skill();

    std::cout << "\n--- 诗词技能 ---" << std::endl;
    test_poetry_skill();

    std::cout << "\n--- 娱乐技能 ---" << std::endl;
    test_entertainment_skill();

    std::cout << "\n--- 占卜技能 ---" << std::endl;
    test_fortune_skill();

    std::cout << "\n--- 谜语技能 ---" << std::endl;
    test_riddle_skill();

    std::cout << "\n--- 游戏技能 ---" << std::endl;
    test_games_skill();

    std::cout << "\n=====================================" << std::endl;
    std::cout << "通过: " << passed << " / 失败: " << failed << std::endl;

    if (failed > 0) {
        std::cout << "\n❌ 测试未全部通过！" << std::endl;
        return 1;
    }
    std::cout << "✅ 全部测试通过！" << std::endl;
    return 0;
}
