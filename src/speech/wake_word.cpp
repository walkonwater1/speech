/**
 * 唤醒词检测 — 拼音匹配
 *
 * Python 对应: src/kws.py → WakeWordDetector
 * C++ 实现:   纯 C++，静态汉字→拼音查找表 → 子串匹配
 *
 * 不需要 ML 模型。
 * 算法:
 *   1. 提取 ASR 文本中的汉字
 *   2. 查表转为拼音
 *   3. 检查唤醒词拼音是否为子串
 */

#include "wake_word.h"

#include <iostream>
#include "logger.h"

// ════════════════════════════════════════════════════════════════
// 汉字 → 拼音查找表（覆盖 ~400 个常用汉字）
// ════════════════════════════════════════════════════════════════

const std::unordered_map<std::string, std::string>& WakeWordDetector::pinyin_table()
{
    static const std::unordered_map<std::string, std::string> table = {
        // 常用打招呼/命令
        {"你","ni"},{"好","hao"},{"小","xiao"},{"千","qian"},
        {"站","zhan"},{"起","qi"},{"来","lai"},{"说","shuo"},{"话","hua"},
        {"打","da"},{"开","kai"},{"关","guan"},{"停","ting"},{"走","zou"},
        {"跑","pao"},{"前","qian"},{"后","hou"},{"左","zuo"},{"右","you"},
        {"上","shang"},{"下","xia"},{"回","hui"},{"去","qu"},{"转","zhuan"},

        // 常用动词
        {"是","shi"},{"有","you"},{"不","bu"},{"在","zai"},{"了","le"},
        {"会","hui"},{"能","neng"},{"要","yao"},{"想","xiang"},{"看","kan"},
        {"听","ting"},{"吃","chi"},{"喝","he"},{"做","zuo"},{"让","rang"},
        {"给","gei"},{"拿","na"},{"放","fang"},{"等","deng"},{"带","dai"},
        {"问","wen"},{"答","da"},{"告","gao"},{"诉","su"},{"知","zhi"},
        {"道","dao"},{"明","ming"},{"白","bai"},{"懂","dong"},{"记","ji"},

        // 常用名词
        {"我","wo"},{"他","ta"},{"她","ta"},{"它","ta"},{"们","men"},
        {"这","zhe"},{"那","na"},{"哪","na"},{"什","shen"},{"么","me"},
        {"谁","shui"},{"人","ren"},{"家","jia"},{"朋","peng"},{"友","you"},
        {"机","ji"},{"器","qi"},{"天","tian"},{"地","di"},{"水","shui"},
        {"火","huo"},{"风","feng"},{"雨","yu"},{"云","yun"},{"花","hua"},
        {"书","shu"},{"电","dian"},{"门","men"},{"窗","chuang"},{"车","che"},
        {"房","fang"},{"桌","zhuo"},{"椅","yi"},{"饭","fan"},{"菜","cai"},
        {"茶","cha"},{"酒","jiu"},{"手","shou"},{"脚","jiao"},{"头","tou"},
        {"眼","yan"},{"耳","er"},{"口","kou"},{"鼻","bi"},{"心","xin"},

        // 常用形容词/副词
        {"大","da"},{"小","xiao"},{"多","duo"},{"少","shao"},{"高","gao"},
        {"低","di"},{"长","chang"},{"短","duan"},{"快","kuai"},{"慢","man"},
        {"新","xin"},{"旧","jiu"},{"好","hao"},{"坏","huai"},{"对","dui"},
        {"错","cuo"},{"真","zhen"},{"假","jia"},{"热","re"},{"冷","leng"},
        {"干","gan"},{"湿","shi"},{"亮","liang"},{"暗","an"},{"轻","qing"},
        {"重","zhong"},{"远","yuan"},{"近","jin"},{"深","shen"},{"浅","qian"},
        {"很","hen"},{"太","tai"},{"最","zui"},{"更","geng"},{"非","fei"},
        {"常","chang"},{"都","dou"},{"也","ye"},{"还","hai"},{"就","jiu"},

        // 数字
        {"一","yi"},{"二","er"},{"三","san"},{"四","si"},{"五","wu"},
        {"六","liu"},{"七","qi"},{"八","ba"},{"九","jiu"},{"十","shi"},
        {"百","bai"},{"千","qian"},{"万","wan"},{"亿","yi"},{"零","ling"},

        // 颜色
        {"红","hong"},{"绿","lv"},{"蓝","lan"},{"黄","huang"},{"黑","hei"},
        {"白","bai"},{"紫","zi"},{"灰","hui"},{"金","jin"},{"银","yin"},

        // 时间
        {"今","jin"},{"明","ming"},{"昨","zuo"},{"年","nian"},{"月","yue"},
        {"日","ri"},{"时","shi"},{"分","fen"},{"秒","miao"},{"早","zao"},
        {"晚","wan"},{"午","wu"},{"现","xian"},{"已","yi"},{"正","zheng"},
        {"刚","gang"},{"才","cai"},{"之","zhi"},{"后","hou"},{"中","zhong"},

        // 方向/位置
        {"里","li"},{"外","wai"},{"旁","pang"},{"边","bian"},{"间","jian"},
        {"面","mian"},{"东","dong"},{"西","xi"},{"南","nan"},{"北","bei"},

        // 动物（常见）
        {"猫","mao"},{"狗","gou"},{"鸟","niao"},{"鱼","yu"},{"马","ma"},

        // 语气词/连词
        {"吧","ba"},{"吗","ma"},{"呢","ne"},{"啊","a"},{"哦","o"},
        {"嗯","en"},{"和","he"},{"或","huo"},{"但","dan"},{"因","yin"},
        {"为","wei"},{"所","suo"},{"以","yi"},{"可","ke"},{"如","ru"},
        {"果","guo"},{"虽","sui"},{"然","ran"},{"比","bi"},{"与","yu"},

        // 特征/状态
        {"生","sheng"},{"死","si"},{"病","bing"},{"老","lao"},{"爱","ai"},
        {"恨","hen"},{"喜","xi"},{"怒","nu"},{"哀","ai"},{"乐","le"},
        {"笑","xiao"},{"哭","ku"},{"忙","mang"},{"闲","xian"},{"累","lei"},
        {"困","kun"},{"饿","e"},{"渴","ke"},{"痛","tong"},{"痒","yang"},

        // 机器人常见
        {"欢","huan"},{"迎","ying"},{"谢","xie"},{"请","qing"},{"帮","bang"},
        {"助","zhu"},{"需","xu"},{"求","qiu"},{"找","zhao"},{"查","cha"},
        {"搜","sou"},{"送","song"},{"取","qu"},{"接","jie"},{"发","fa"},
        {"收","shou"},{"读","du"},{"写","xie"},{"算","suan"},{"数","shu"},
        {"聊","liao"},{"讲","jiang"},{"介","jie"},{"绍","shao"},{"安","an"},
        {"教","jiao"},{"学","xue"},{"练","lian"},{"试","shi"},{"测","ce"},
        {"复","fu"},{"播","bo"},{"唱","chang"},{"跳","tiao"},{"舞","wu"},
        {"按","an"},{"照","zhao"},{"反","fan"},{"正","zheng"},{"随","sui"},
        {"便","bian"},{"挺","ting"},{"棒","bang"},{"错","cuo"},{"误","wu"},
        {"肯","ken"},{"定","ding"},{"否","fou"},{"认","ren"},{"该","gai"},

        // 补充
        {"每","mei"},{"些","xie"},{"种","zhong"},{"样","yang"},{"件","jian"},
        {"个","ge"},{"只","zhi"},{"双","shuang"},{"本","ben"},{"条","tiao"},
        {"张","zhang"},{"次","ci"},{"遍","bian"},{"回","hui"},{"点","dian"},
        {"声","sheng"},{"音","yin"},{"色","se"},{"味","wei"},{"气","qi"},
        {"感","gan"},{"觉","jue"},{"思","si"},{"意","yi"},{"情","qing"},
        {"理","li"},{"法","fa"},{"名","ming"},{"字","zi"},{"姓","xing"},
        {"号","hao"},{"码","ma"},{"网","wang"},{"址","zhi"},{"图","tu"},
        {"片","pian"},{"视","shi"},{"频","pin"},{"文","wen"},{"语","yu"},
        {"言","yan"},{"英","ying"},{"美","mei"},{"国","guo"},{"京","jing"},
        {"海","hai"},{"广","guang"},{"深","shen"},{"杭","hang"},{"苏","su"},
        {"州","zhou"},{"川","chuan"},{"湖","hu"},{"山","shan"},{"河","he"},
    };
    return table;
}


// ════════════════════════════════════════════════════════════════
// WakeWordDetector
// ════════════════════════════════════════════════════════════════

WakeWordDetector::WakeWordDetector(const std::string& wake_word)
    : wake_word_(wake_word)
{}

bool WakeWordDetector::detect(const std::string& asr_text)
{
    if (wake_word_.empty()) {
        return true;  // 未启用唤醒词，直接通过
    }

    std::string pinyin_text = text_to_pinyin(asr_text);
    bool matched = pinyin_text.find(wake_word_) != std::string::npos;

    if (!matched) {
        std::cout << "   [KWS] 未检测到唤醒词 \"" << wake_word_
                  << "\" (识别: " << pinyin_text << ")" << std::endl;
    } else {
        LOG_INFO("   [KWS] ✅ 唤醒词检测成功");
    }

    return matched;
}

std::string WakeWordDetector::text_to_pinyin(const std::string& text)
{
    std::string result;

    const auto& table = pinyin_table();
    for (size_t i = 0; i < text.size(); ) {
        // 尝试匹配 UTF-8 编码的汉字 (3字节)
        if (i + 3 <= text.size()) {
            std::string ch = text.substr(i, 3);
            auto it = table.find(ch);
            if (it != table.end()) {
                if (!result.empty()) result += " ";
                result += it->second;
                i += 3;
                continue;
            }
        }
        i++;
    }

    return result;
}
