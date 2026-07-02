"""
对话记忆管理

C++ 对应: std::vector<std::pair<std::string, std::string>>
    #include <vector>
    #include <string>
    #include <deque>

    class ChatMemory {
    public:
        void add(const std::string& user_msg, const std::string& assistant_msg);
        std::string get_context() const;
        void clear();
    private:
        // 按轮次存储，(user_msg, assistant_msg)
        std::deque<std::pair<std::string, std::string>> history_;
        int max_rounds_;    // 默认 10
        int max_tokens_;    // 默认 512 (粗略估算)
    };

实现要点:
    - add() 后自动裁剪: 超 rounds 删最早，超 tokens 也删最早
    - get_context() 输出格式: "User: ...\nAssistant: ...\n..."
    - clear() 清空全部历史
"""


class ChatMemory:
    """对话历史管理"""

    def __init__(self, max_rounds: int = 10, max_tokens: int = 512):
        """
        参数:
            max_rounds: 最多保留 N 轮对话
            max_tokens: 总 token 上限（粗略按字符数 * 4 估算）
        """
        self.history: list[tuple[str, str]] = []   # [(user_msg, assistant_msg), ...]
        self.max_rounds = max_rounds
        self.max_tokens = max_tokens

    # ── 核心操作 ──────────────────────────────────────

    def add(self, user_msg: str, assistant_msg: str):
        """记录一轮对话"""
        self.history.append((user_msg, assistant_msg))
        self._trim()

    def get_context(self) -> str:
        """获取对话上下文文本（最近 max_rounds 轮）

        返回格式:
            User: 你好
            Assistant: 你好呀！今天想聊点什么？
            User: 今天天气怎么样
            Assistant: ...
        """
        if not self.history:
            return ""

        recent = self.history[-self.max_rounds:]
        lines: list[str] = []
        for user, assistant in recent:
            lines.append(f"User: {user}")
            lines.append(f"Assistant: {assistant}")
        return "\n".join(lines)

    def clear(self):
        """清空全部记忆"""
        self.history.clear()

    # ── 内部 ──────────────────────────────────────────

    def _trim(self):
        """裁剪历史，保持轮数和 token 数不超限"""
        # 1) 限制轮数
        while len(self.history) > self.max_rounds:
            self.history.pop(0)

        # 2) 限制 token 数（粗略：1 token ≈ 4 字符）
        while self.history:
            total_chars = sum(len(u) + len(a) for u, a in self.history)
            if total_chars * 4 <= self.max_tokens:
                break
            self.history.pop(0)
