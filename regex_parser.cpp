#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>

class RegexParserError : public std::runtime_error {
public:
    explicit RegexParserError(const std::string& msg)
        : std::runtime_error(msg) {}
};

// Типы токенов
enum class TokenType {
    CAP_OPEN,       // '('
    NONCAP_OPEN,    // (?:
    LOOKAHEAD_OPEN, // (?=
    EXPR_REF_OPEN,  // (?N)
    CLOSE,          // ')'
    ALT,            // '|'
    STAR,           // '*'
    CHAR            // [a-z]
};

class Token {
public:
    TokenType ttype;
    char value;   // актуально, если ttype == CHAR
    int refValue; // актуально, если ttype == EXPR_REF_OPEN

    Token(TokenType type_, char v = '\0', int rv = -1)
        : ttype(type_), value(v), refValue(rv) {}

    friend std::ostream& operator<<(std::ostream& os, const Token& tk) {
        os << "Token(";
        switch (tk.ttype) {
            case TokenType::CAP_OPEN:       os << "CAP_OPEN"; break;
            case TokenType::NONCAP_OPEN:    os << "NONCAP_OPEN"; break;
            case TokenType::LOOKAHEAD_OPEN: os << "LOOKAHEAD_OPEN"; break;
            case TokenType::EXPR_REF_OPEN:  os << "EXPR_REF_OPEN, " << tk.refValue; break;
            case TokenType::CLOSE:          os << "CLOSE"; break;
            case TokenType::ALT:            os << "ALT"; break;
            case TokenType::STAR:           os << "STAR"; break;
            case TokenType::CHAR:           os << "CHAR, '" << tk.value << "'"; break;
        }
        return os << ")";
    }
};

class Lexer {
public:
    explicit Lexer(const std::string& text) : text_(text), pos_(0) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos_ < text_.size()) {
            char ch = peek();
            switch (ch) {
                case '(':
                {
                    advance();
                    if (peek() == '?') {
                        advance();
                        char nxt = peek();
                        if (nxt == ':') {
                            advance();
                            tokens.emplace_back(TokenType::NONCAP_OPEN);
                        } else if (nxt == '=') {
                            advance();
                            tokens.emplace_back(TokenType::LOOKAHEAD_OPEN);
                        } else if (std::isdigit(nxt)) {
                            int val = nxt - '0';
                            advance();
                            tokens.emplace_back(TokenType::EXPR_REF_OPEN, '\0', val);
                        } else {
                            throw RegexParserError("Invalid syntax after '(?'");
                        }
                    } else {
                        tokens.emplace_back(TokenType::CAP_OPEN);
                    }
                    break;
                }
                case ')':
                    tokens.emplace_back(TokenType::CLOSE);
                    advance();
                    break;
                case '|':
                    tokens.emplace_back(TokenType::ALT);
                    advance();
                    break;
                case '*':
                    tokens.emplace_back(TokenType::STAR);
                    advance();
                    break;
                default:
                    if (ch >= 'a' && ch <= 'z') {
                        tokens.emplace_back(TokenType::CHAR, ch);
                        advance();
                    } else {
                        throw RegexParserError("Unknown character: " + std::string(1, ch));
                    }
                    break;
            }
        }
        return tokens;
    }

private:
    std::string text_;
    size_t pos_;

    char peek() const {
        return (pos_ < text_.size()) ? text_[pos_] : '\0';
    }
    void advance() { if (pos_ < text_.size()) ++pos_; }
};

// Базовый класс узла AST
class Node {
public:
    virtual ~Node() = default;
};

class GroupNode : public Node {
public:
    int group_id;
    std::unique_ptr<Node> node;
    GroupNode(int gid, std::unique_ptr<Node> n) : group_id(gid), node(std::move(n)) {}
};

class NonCapGroupNode : public Node {
public:
    std::unique_ptr<Node> node;
    explicit NonCapGroupNode(std::unique_ptr<Node> n) : node(std::move(n)) {}
};

class LookaheadNode : public Node {
public:
    std::unique_ptr<Node> node;
    explicit LookaheadNode(std::unique_ptr<Node> n) : node(std::move(n)) {}
};

class ConcatNode : public Node {
public:
    std::vector<std::unique_ptr<Node>> nodes;
    explicit ConcatNode(std::vector<std::unique_ptr<Node>> ns) : nodes(std::move(ns)) {}
};

class AltNode : public Node {
public:
    std::vector<std::unique_ptr<Node>> branches;
    explicit AltNode(std::vector<std::unique_ptr<Node>> bs) : branches(std::move(bs)) {}
};

class StarNode : public Node {
public:
    std::unique_ptr<Node> node;
    explicit StarNode(std::unique_ptr<Node> n) : node(std::move(n)) {}
};

class CharNode : public Node {
public:
    char ch;
    explicit CharNode(char c) : ch(c) {}
};

class ExprRefNode : public Node {
public:
    int ref_id;
    explicit ExprRefNode(int id) : ref_id(id) {}
};

// Парсер
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens)
        : tokens_(tokens), pos_(0), group_count_(0), max_groups_(9), in_lookahead_(false) {}

    // group_id -> AST
    std::map<int, Node*> groups_ast;

    std::unique_ptr<Node> parse() {
        auto root = parse_alternation();
        if (current_token()) {
            throw RegexParserError("Extra characters after a valid expression");
        }
        // Проверяем ссылки
        std::set<int> defined;
        check_references(root.get(), defined);
        return root;
    }

private:
    const std::vector<Token>& tokens_;
    size_t pos_;
    int group_count_;
    const int max_groups_;
    bool in_lookahead_;

    const Token* current_token() const {
        return (pos_ < tokens_.size()) ? &tokens_[pos_] : nullptr;
    }
    const Token* eat(TokenType ttype) {
        auto tok = current_token();
        if (!tok) throw RegexParserError("Unexpected end of expression");
        if (tok->ttype != ttype) {
            throw RegexParserError("Syntax error: expected a different token");
        }
        ++pos_;
        return tok;
    }

    // parse_alternation -> parse_concatenation ('|' parse_concatenation)*
    std::unique_ptr<Node> parse_alternation() {
        std::vector<std::unique_ptr<Node>> branches;
        branches.push_back(parse_concatenation());

        while (current_token() && current_token()->ttype == TokenType::ALT) {
            eat(TokenType::ALT);
            if (!current_token() ||
                current_token()->ttype == TokenType::CLOSE ||
                current_token()->ttype == TokenType::ALT)
            {
                throw RegexParserError("Empty alternation is not allowed");
            }
            branches.push_back(parse_concatenation());
        }
        if (branches.size() == 1) return std::move(branches[0]);
        return std::make_unique<AltNode>(std::move(branches));
    }

    // parse_concatenation -> parse_repetition+
    std::unique_ptr<Node> parse_concatenation() {
        std::vector<std::unique_ptr<Node>> parts;
        while (current_token() &&
               current_token()->ttype != TokenType::CLOSE &&
               current_token()->ttype != TokenType::ALT)
        {
            parts.push_back(parse_repetition());
        }
        if (parts.size() == 1) return std::move(parts[0]);
        return std::make_unique<ConcatNode>(std::move(parts));
    }

    // parse_repetition -> parse_base ('*')*
    std::unique_ptr<Node> parse_repetition() {
        auto node = parse_base();
        while (current_token() && current_token()->ttype == TokenType::STAR) {
            eat(TokenType::STAR);
            node = std::make_unique<StarNode>(std::move(node));
        }
        return node;
    }

    // parse_base -> '(' parse_alternation ')' | (?:...) | (?=...) | (?N) | CHAR
    std::unique_ptr<Node> parse_base() {
        auto tok = current_token();
        if (!tok) throw RegexParserError("Unexpected end when expecting a base expression");

        switch (tok->ttype) {
        case TokenType::CAP_OPEN:
        {
            eat(TokenType::CAP_OPEN);
            if (++group_count_ > max_groups_) {
                throw RegexParserError("Exceeded the number of capture groups (>9)");
            }
            int gid = group_count_;
            auto sub = parse_alternation();
            eat(TokenType::CLOSE);

            groups_ast[gid] = sub.get(); // регистрируем узел для проверки ссылок
            return std::make_unique<GroupNode>(gid, std::move(sub));
        }
        case TokenType::NONCAP_OPEN:
        {
            eat(TokenType::NONCAP_OPEN);
            auto sub = parse_alternation();
            eat(TokenType::CLOSE);
            return std::make_unique<NonCapGroupNode>(std::move(sub));
        }
        case TokenType::LOOKAHEAD_OPEN:
        {
            if (in_lookahead_) {
                throw RegexParserError("Nested lookaheads are not allowed");
            }
            eat(TokenType::LOOKAHEAD_OPEN);
            bool old = in_lookahead_;
            in_lookahead_ = true;
            auto sub = parse_alternation();
            in_lookahead_ = old;
            eat(TokenType::CLOSE);
            return std::make_unique<LookaheadNode>(std::move(sub));
        }
        case TokenType::EXPR_REF_OPEN:
        {
            int ref_id = tok->refValue;
            eat(TokenType::EXPR_REF_OPEN);
            eat(TokenType::CLOSE);
            return std::make_unique<ExprRefNode>(ref_id);
        }
        case TokenType::CHAR:
        {
            char c = tok->value;
            eat(TokenType::CHAR);
            return std::make_unique<CharNode>(c);
        }
        default:
            throw RegexParserError("Invalid token in parse_base()");
        }
    }

    // Проверка корректности ссылок
    std::set<int> check_references(const Node* node, const std::set<int>& defined) {
        if (!node) return defined;

        // Определяем тип узла через dynamic_cast
        if (auto cn = dynamic_cast<const CharNode*>(node)) {
            (void)cn; 
            return defined;
        }
        if (auto en = dynamic_cast<const ExprRefNode*>(node)) {
            // Допустим forward-ref, но ref_id не может выходить за границы [1..9].
            // Можно проверять также <= group_count_, но в условии разрешаются рекурсивные forward-ссылки.
            if (en->ref_id <= 0 || en->ref_id > max_groups_) {
                throw RegexParserError("Reference to non-existent group: " + std::to_string(en->ref_id));
            }
            return defined;
        }
        if (auto gn = dynamic_cast<const GroupNode*>(node)) {
            auto new_def = check_references(gn->node.get(), defined);
            // Завершение группы: она становится определённой
            std::set<int> res = new_def;
            res.insert(gn->group_id);
            return res;
        }
        if (auto ng = dynamic_cast<const NonCapGroupNode*>(node)) {
            return check_references(ng->node.get(), defined);
        }
        if (auto ln = dynamic_cast<const LookaheadNode*>(node)) {
            check_no_cap_and_lookahead(ln->node.get(), true);
            return check_references(ln->node.get(), defined);
        }
        if (auto sn = dynamic_cast<const StarNode*>(node)) {
            return check_references(sn->node.get(), defined);
        }
        if (auto cc = dynamic_cast<const ConcatNode*>(node)) {
            auto cur = defined;
            for (auto& child : cc->nodes) {
                cur = check_references(child.get(), cur);
            }
            return cur;
        }
        if (auto alt = dynamic_cast<const AltNode*>(node)) {
            // берём union по всем ветвям
            auto uni = defined;
            for (auto& br : alt->branches) {
                auto br_def = check_references(br.get(), defined);
                uni.insert(br_def.begin(), br_def.end());
            }
            return uni;
        }
        throw RegexParserError("Unknown node type during reference checking.");
    }

    // Проверка отсутствия групп захвата и lookahead внутри lookahead
    void check_no_cap_and_lookahead(const Node* node, bool insideLook) {
        if (!node) return;
        if (auto gn = dynamic_cast<const GroupNode*>(node)) {
            if (insideLook) {
                throw RegexParserError("Capture groups cannot be used inside lookaheads");
            }
        }
        if (auto ln = dynamic_cast<const LookaheadNode*>(node)) {
            if (insideLook) {
                throw RegexParserError("Nested lookaheads are not allowed");
            }
        }
        // Рекурсивный обход
        if (auto ng = dynamic_cast<const NonCapGroupNode*>(node)) {
            check_no_cap_and_lookahead(ng->node.get(), insideLook);
        } else if (auto l = dynamic_cast<const LookaheadNode*>(node)) {
            check_no_cap_and_lookahead(l->node.get(), insideLook);
        } else if (auto s = dynamic_cast<const StarNode*>(node)) {
            check_no_cap_and_lookahead(s->node.get(), insideLook);
        } else if (auto c = dynamic_cast<const ConcatNode*>(node)) {
            for (auto& ch : c->nodes) {
                check_no_cap_and_lookahead(ch.get(), insideLook);
            }
        } else if (auto a = dynamic_cast<const AltNode*>(node)) {
            for (auto& br : a->branches) {
                check_no_cap_and_lookahead(br.get(), insideLook);
            }
        }
        // CharNode и ExprRefNode не содержат дочерних
    }
};

// Построение "каркасной" КС-грамматики
class CFGBuilder {
public:
    explicit CFGBuilder(const std::map<int, Node*>& groups_ast)
        : groups_ast_(groups_ast), noncap_idx_(1), star_idx_(1) {}

    // Возвращает (start_symbol, rules)
    //  rules[NonTerminal] = { {rhs1}, {rhs2}, ... }
    std::pair<std::string, std::map<std::string, std::vector<std::vector<std::string>>>>
    build(const Node* node) {
        std::string start = "S";
        std::map<std::string, std::vector<std::vector<std::string>>> rules;

        std::string main_nt = node_to_cfg(node, rules, "");
        rules[start].push_back({main_nt});

        // Регистрируем группы, если были forward-ссылки
        for (auto& [gid, nd] : groups_ast_) {
            if (group_nonterm_.find(gid) == group_nonterm_.end()) {
                group_nonterm_[gid] = "G" + std::to_string(gid);
            }
            node_to_cfg(nd, rules, group_nonterm_[gid]);
        }
        return {start, rules};
    }

private:
    const std::map<int, Node*>& groups_ast_;
    std::map<int, std::string> group_nonterm_;
    
    // Кэширование для оптимизации
    std::unordered_map<const Node*, std::string> node_cache_;
    std::set<std::string> built_nt_;
    
    int noncap_idx_, star_idx_;

    std::string node_to_cfg(const Node* node,
        std::map<std::string, std::vector<std::vector<std::string>>>& rules,
        const std::string& start_symbol)
    {
        if (!node) throw RegexParserError("node_to_cfg: empty node");

        // Проверяем кэш
        auto it = node_cache_.find(node);
        if (it != node_cache_.end()) {
            return it->second;
        }

        // Определяем имя нетерминала
        std::string nt = !start_symbol.empty() ? start_symbol : 
            (dynamic_cast<const GroupNode*>(node) ? 
                (group_nonterm_[dynamic_cast<const GroupNode*>(node)->group_id] = 
                    "G" + std::to_string(dynamic_cast<const GroupNode*>(node)->group_id)) : 
                fresh_nt(node));

        // Кэшируем результат
        node_cache_[node] = nt;
        
        // Если правила уже построены, возвращаем нетерминал
        if (built_nt_.count(nt)) {
            return nt;
        }
        built_nt_.insert(nt);

        if (auto chn = dynamic_cast<const CharNode*>(node)) {
            rules[nt].push_back({std::string(1, chn->ch)});
        }
        else if (auto gn = dynamic_cast<const GroupNode*>(node)) {
            std::string sub_nt = node_to_cfg(gn->node.get(), rules, "");
            rules[nt].push_back({sub_nt});
        }
        else if (auto ncg = dynamic_cast<const NonCapGroupNode*>(node)) {
            std::string sub_nt = node_to_cfg(ncg->node.get(), rules, "");
            rules[nt].push_back({sub_nt});
        }
        else if (auto ln = dynamic_cast<const LookaheadNode*>(node)) {
            rules[nt].push_back({});
        }
        else if (auto cn = dynamic_cast<const ConcatNode*>(node)) {
            std::vector<std::string> seq;
            for (auto& ch : cn->nodes) {
                seq.push_back(node_to_cfg(ch.get(), rules, ""));
            }
            rules[nt].push_back(seq);
        }
        else if (auto an = dynamic_cast<const AltNode*>(node)) {
            for (auto& br : an->branches) {
                std::string br_nt = node_to_cfg(br.get(), rules, "");
                rules[nt].push_back({br_nt});
            }
        }
        else if (auto sn = dynamic_cast<const StarNode*>(node)) {
            std::string sub_nt = node_to_cfg(sn->node.get(), rules, "");
            rules[nt].push_back({});
            rules[nt].push_back({nt, sub_nt});
        }
        else if (auto erf = dynamic_cast<const ExprRefNode*>(node)) {
            int rid = erf->ref_id;
            if (group_nonterm_.find(rid) == group_nonterm_.end()) {
                group_nonterm_[rid] = "G" + std::to_string(rid);
            }
            if (groups_ast_.find(rid) == groups_ast_.end()) {
                throw RegexParserError("Reference to non-existent group " + std::to_string(rid));
            }
            node_to_cfg(groups_ast_.at(rid), rules, group_nonterm_[rid]);
            return group_nonterm_[rid];
        }
        else {
            throw RegexParserError("node_to_cfg: unknown AST node type");
        }
        return nt;
    }

    std::string fresh_nt(const Node* node) {
        static int la_count = 1, c_count = 1, a_count = 1, char_count = 1;
        
        // Для lookahead
        if (dynamic_cast<const LookaheadNode*>(node)) {
            return "LA" + std::to_string(la_count++);
        }
        // Для конкатенации
        else if (dynamic_cast<const ConcatNode*>(node)) {
            return "C" + std::to_string(c_count++);
        }
        // Для альтернатив
        else if (dynamic_cast<const AltNode*>(node)) {
            return "A" + std::to_string(a_count++);
        }
        // Для одиночного символа
        else if (auto chn = dynamic_cast<const CharNode*>(node)) {
            // Используем кэш имён для каждого символа
            static std::map<char, std::string> char_map;
            char c = chn->ch;
            // Если уже есть нетерминал для данного символа, возвращаем его
            auto it = char_map.find(c);
            if (it != char_map.end()) {
                return it->second;
            } else {
                // Иначе генерируем новый нетерминал, запоминаем и возвращаем
                std::string newNt = "CHAR" + std::to_string(char_count++);
                char_map[c] = newNt;
                return newNt;
            }
        }
        // Для незахватывающих групп
        else if (dynamic_cast<const NonCapGroupNode*>(node)) {
            static int noncap_idx_ = 1; 
            return "N" + std::to_string(noncap_idx_++);
        }
        // Для звёздочки
        else if (dynamic_cast<const StarNode*>(node)) {
            static int star_idx_ = 1; 
            return "R" + std::to_string(star_idx_++);
        }
        // На всякий случай универсальный вариант
        static int generic_idx = 1;
        return "X" + std::to_string(generic_idx++);
    }
};

int main() {
    try {
        std::string text;
        std::getline(std::cin, text);

        if (text.empty()) {
            throw RegexParserError("Input string is empty");
        }
        // Лексер
        Lexer lexer(text);
        auto tokens = lexer.tokenize();

        // Парсер
        Parser parser(tokens);
        auto ast = parser.parse();

        // Построение КС-грамматики
        CFGBuilder builder(parser.groups_ast);
        auto [start_nt, rules] = builder.build(ast.get());

        std::cout << "The expression is valid and meets the constraints.\n";
        std::cout << "Constructed CFG (skeleton):\n";
        std::cout << "Start non-terminal: " << start_nt << "\n";

        for (auto& [nt, rhss] : rules) {
            for (auto& rhs : rhss) {
                if (rhs.empty()) {
                    std::cout << nt << " -> ε\n";
                } else {
                    std::cout << nt << " -> ";
                    for (size_t i = 0; i < rhs.size(); ++i) {
                        std::cout << rhs[i];
                        if (i + 1 < rhs.size()) std::cout << " ";
                    }
                    std::cout << "\n";
                }
            }
        }
        std::cout << "============================================================\n";

        /*
        for (auto& tk : tokens) {
            std::cout << tk << "\n";
        }
        */

        std::cout << "Done.\n";

    } catch (const RegexParserError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1; 
    } catch (const std::exception& e) {
        std::cerr << "Standard exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
