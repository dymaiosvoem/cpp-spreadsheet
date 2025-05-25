#include "FormulaAST.h"

#include "FormulaBaseListener.h"
#include "FormulaLexer.h"
#include "FormulaParser.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>

using namespace std;

namespace ASTImpl {

enum ExprPrecedence {
    EP_ADD,
    EP_SUB,
    EP_MUL,
    EP_DIV,
    EP_UNARY,
    EP_ATOM,
    EP_END,
};

// a bit is set when the parentheses are needed
enum PrecedenceRule {
    PR_NONE = 0b00,                // never needed
    PR_LEFT = 0b01,                // needed for a left child
    PR_RIGHT = 0b10,               // needed for a right child
    PR_BOTH = PR_LEFT | PR_RIGHT,  // needed for both children
};

// PRECEDENCE_RULES[parent][child] determines if parentheses need
// to be inserted between a parent and a child of specific precedences;
// for some nodes rules are different for left and right children:
// (X c Y) p Z  vs  X p (Y c Z)
//
// The interesting cases are the ones where removing the parens would change the AST.
// It may happen when our precedence rules for parentheses are different from
// the grammatic precedence of operations.
//
// Case analysis:
// A + (B + C) - always okay (nothing of lower grammatic precedence could have been written to the
// right)
//    (e.g. if we had A + (B + C) / D, it wouldn't parse in a way
//    that woudld have given us A + (B + C) as a subexpression to deal with)
// A + (B - C) - always okay (nothing of lower grammatic precedence could have been written to the
// right) A - (B + C) - never okay A - (B - C) - never okay A * (B * C) - always okay (the parent
// has the highest grammatic precedence) A * (B / C) - always okay (the parent has the highest
// grammatic precedence) A / (B * C) - never okay A / (B / C) - never okay
// -(A + B) - never okay
// -(A - B) - never okay
// -(A * B) - always okay (the resulting binary op has the highest grammatic precedence)
// -(A / B) - always okay (the resulting binary op has the highest grammatic precedence)
// +(A + B) - **sometimes okay** (e.g. parens in +(A + B) / C are **not** optional)
//     (currently in the table we're always putting in the parentheses)
// +(A - B) - **sometimes okay** (same)
//     (currently in the table we're always putting in the parentheses)
// +(A * B) - always okay (the resulting binary op has the highest grammatic precedence)
// +(A / B) - always okay (the resulting binary op has the highest grammatic precedence)
constexpr PrecedenceRule PRECEDENCE_RULES[EP_END][EP_END] = {
    /* EP_ADD */ {PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_SUB */ {PR_RIGHT, PR_RIGHT, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_MUL */ {PR_BOTH, PR_BOTH, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_DIV */ {PR_BOTH, PR_BOTH, PR_RIGHT, PR_RIGHT, PR_NONE, PR_NONE},
    /* EP_UNARY */ {PR_BOTH, PR_BOTH, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_ATOM */ {PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
};

class Expr {
public:
    virtual ~Expr() = default;
    virtual void Print(ostream& out) const = 0;
    virtual void DoPrintFormula(ostream& out, ExprPrecedence precedence) const = 0;
    virtual double Evaluate(const SheetInterface& sheet) const = 0;

    // higher is tighter
    virtual ExprPrecedence GetPrecedence() const = 0;

    void PrintFormula(ostream& out, ExprPrecedence parent_precedence,
                      bool right_child = false) const {
        auto precedence = GetPrecedence();
        auto mask = right_child ? PR_RIGHT : PR_LEFT;
        bool parens_needed = PRECEDENCE_RULES[parent_precedence][precedence] & mask;
        if (parens_needed) {
            out << '(';
        }

        DoPrintFormula(out, precedence);

        if (parens_needed) {
            out << ')';
        }
    }
};

namespace {
class BinaryOpExpr final : public Expr {
public:
    enum Type : char {
        Add = '+',
        Subtract = '-',
        Multiply = '*',
        Divide = '/',
    };

public:
    explicit BinaryOpExpr(Type type, unique_ptr<Expr> lhs, unique_ptr<Expr> rhs)
        : type_(type)
        , lhs_(move(lhs))
        , rhs_(move(rhs)) {
    }

    void Print(ostream& out) const override {
        out << '(' << static_cast<char>(type_) << ' ';
        lhs_->Print(out);
        out << ' ';
        rhs_->Print(out);
        out << ')';
    }

    void DoPrintFormula(ostream& out, ExprPrecedence precedence) const override {
        lhs_->PrintFormula(out, precedence);
        out << static_cast<char>(type_);
        rhs_->PrintFormula(out, precedence, /* right_child = */ true);
    }

    ExprPrecedence GetPrecedence() const override {
        switch (type_) {
            case Add:
                return EP_ADD;
            case Subtract:
                return EP_SUB;
            case Multiply:
                return EP_MUL;
            case Divide:
                return EP_DIV;
            default:
                // have to do this because VC++ has a buggy warning
                assert(false);
                return static_cast<ExprPrecedence>(INT_MAX);
        }
    }

    double Evaluate(const SheetInterface& sheet) const override {
        double left = lhs_->Evaluate(sheet);
        double right = rhs_->Evaluate(sheet);
        double result = 0;
        
        switch (type_) {
            case Type::Add:
                result = left + right;
                break;
            case Type::Subtract:
                result = left - right;
                break;
            case Type::Multiply:
                result = left * right;
                break;
            case Type::Divide:
                result = left / right;  
                break;
            default:
                assert(false && "Unknown Bionary Op");
        }

        if (!isfinite(result)) {
            throw FormulaError(FormulaError::Category::Arithmetic);
        } 

        return result;
    }

private:
    Type type_;
    unique_ptr<Expr> lhs_;
    unique_ptr<Expr> rhs_;
};

class UnaryOpExpr final : public Expr {
public:
    enum Type : char {
        UnaryPlus = '+',
        UnaryMinus = '-',
    };

public:
    explicit UnaryOpExpr(Type type, unique_ptr<Expr> operand)
        : type_(type)
        , operand_(move(operand)) {
    }

    void Print(ostream& out) const override {
        out << '(' << static_cast<char>(type_) << ' ';
        operand_->Print(out);
        out << ')';
    }

    void DoPrintFormula(ostream& out, ExprPrecedence precedence) const override {
        out << static_cast<char>(type_);
        operand_->PrintFormula(out, precedence);
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_UNARY;
    }

    double Evaluate(const SheetInterface& sheet) const override {
        double result = operand_->Evaluate(sheet);

        switch (type_) {
            case Type::UnaryPlus:
                result = +result;
                break;
            case Type::UnaryMinus:
                result = -result;
                break;
            default:
                assert(false && "Unknown Unary Op");
        }
        return result;
    }

private:
    Type type_;
    unique_ptr<Expr> operand_;
};

class CellExpr final : public Expr {
public:
    explicit CellExpr(const Position* cell)
        : cell_(cell) {
    }

    void Print(ostream& out) const override {
        if (!cell_->IsValid()) {
            out << FormulaError::Category::Ref;
        } else {
            out << cell_->ToString();
        }
    }

    void DoPrintFormula(ostream& out, ExprPrecedence /* precedence */) const override {
        Print(out);
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_ATOM;
    }

    double Evaluate(const SheetInterface& sheet) const override {
        if (!cell_->IsValid()) {
            throw FormulaError(FormulaError::Category::Ref);
        }

        const CellInterface* cell_ptr = sheet.GetCell(*cell_);

        if (!cell_ptr) {
            return 0.0;
        } else {
            auto result = cell_ptr->GetValue();
            if (holds_alternative<string>(result)) {
                string text = get<string>(result);
                if (text.empty()) {
                    return 0.0;
                } else {
                    try {
                        size_t pos = 0;
                        double answer = stod(text, &pos);
                        if (pos == text.size()) {
                            return answer;
                        } else {
                            throw FormulaError(FormulaError::Category::Value);
                        }
                    } catch (...) {}
                }
            } else if (holds_alternative<double>(result)) {
                return get<double>(result);
            } else {
                throw get<FormulaError>(result);
            }
        }

        throw FormulaError(FormulaError::Category::Value);
    }

private:
    const Position* cell_;
};

class NumberExpr final : public Expr {
public:
    explicit NumberExpr(double value)
        : value_(value) {
    }

    void Print(ostream& out) const override {
        out << value_;
    }

    void DoPrintFormula(ostream& out, ExprPrecedence /* precedence */) const override {
        out << value_;
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_ATOM;
    }

    double Evaluate(const SheetInterface& sheet) const override {
        return value_;
    }

private:
    double value_;
};

class ParseASTListener final : public FormulaBaseListener {
public:
    unique_ptr<Expr> MoveRoot() {
        assert(args_.size() == 1);
        auto root = move(args_.front());
        args_.clear();

        return root;
    }

    forward_list<Position> MoveCells() {
        return move(cells_);
    }

public:
    void exitUnaryOp(FormulaParser::UnaryOpContext* ctx) override {
        assert(args_.size() >= 1);

        auto operand = move(args_.back());

        UnaryOpExpr::Type type;
        if (ctx->SUB()) {
            type = UnaryOpExpr::UnaryMinus;
        } else {
            assert(ctx->ADD() != nullptr);
            type = UnaryOpExpr::UnaryPlus;
        }

        auto node = make_unique<UnaryOpExpr>(type, move(operand));
        args_.back() = move(node);
    }

    void exitLiteral(FormulaParser::LiteralContext* ctx) override {
        double value = 0;
        auto valueStr = ctx->NUMBER()->getSymbol()->getText();
        istringstream in(valueStr);
        in >> value;
        if (!in) {
            throw ParsingError("Invalid number: " + valueStr);
        }

        auto node = make_unique<NumberExpr>(value);
        args_.push_back(move(node));
    }

    void exitCell(FormulaParser::CellContext* ctx) override {
        auto value_str = ctx->CELL()->getSymbol()->getText();
        auto value = Position::FromString(value_str);
        if (!value.IsValid()) {
            throw FormulaException("Invalid position: " + value_str);
        }

        cells_.push_front(value);
        auto node = make_unique<CellExpr>(&cells_.front());
        args_.push_back(move(node));
    }

    void exitBinaryOp(FormulaParser::BinaryOpContext* ctx) override {
        assert(args_.size() >= 2);

        auto rhs = move(args_.back());
        args_.pop_back();

        auto lhs = move(args_.back());

        BinaryOpExpr::Type type;
        if (ctx->ADD()) {
            type = BinaryOpExpr::Add;
        } else if (ctx->SUB()) {
            type = BinaryOpExpr::Subtract;
        } else if (ctx->MUL()) {
            type = BinaryOpExpr::Multiply;
        } else {
            assert(ctx->DIV() != nullptr);
            type = BinaryOpExpr::Divide;
        }

        auto node = make_unique<BinaryOpExpr>(type, move(lhs), move(rhs));
        args_.back() = move(node);
    }

    void visitErrorNode(antlr4::tree::ErrorNode* node) override {
        throw ParsingError("Error when parsing: " + node->getSymbol()->getText());
    }

private:
    vector<unique_ptr<Expr>> args_;
    forward_list<Position> cells_;
};

class BailErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(antlr4::Recognizer* /* recognizer */, antlr4::Token* /* offendingSymbol */,
                     size_t /* line */, size_t /* charPositionInLine */, const string& msg,
                     exception_ptr /* e */
                     ) override {
        throw ParsingError("Error when lexing: " + msg);
    }
};

}  // namespace
}  // namespace ASTImpl

FormulaAST ParseFormulaAST(istream& in) {
    using namespace antlr4;

    ANTLRInputStream input(in);

    FormulaLexer lexer(&input);
    ASTImpl::BailErrorListener error_listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&error_listener);

    CommonTokenStream tokens(&lexer);

    FormulaParser parser(&tokens);
    auto error_handler = make_shared<BailErrorStrategy>();
    parser.setErrorHandler(error_handler);
    parser.removeErrorListeners();

    tree::ParseTree* tree = parser.main();
    ASTImpl::ParseASTListener listener;
    tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);

    return FormulaAST(listener.MoveRoot(), listener.MoveCells());
}

FormulaAST ParseFormulaAST(const string& in_str) {
    istringstream in(in_str);
    return ParseFormulaAST(in);
}

void FormulaAST::PrintCells(ostream& out) const {
    for (auto cell : cells_) {
        out << cell.ToString() << ' ';
    }
}

void FormulaAST::Print(ostream& out) const {
    root_expr_->Print(out);
}

void FormulaAST::PrintFormula(ostream& out) const {
    root_expr_->PrintFormula(out, ASTImpl::EP_ATOM);
}

double FormulaAST::Execute(const SheetInterface& sheet) const {
    return root_expr_->Evaluate(sheet);
}

FormulaAST::FormulaAST(unique_ptr<ASTImpl::Expr> root_expr, forward_list<Position> cells)
    : root_expr_(move(root_expr))
    , cells_(move(cells)) {
    cells_.sort();  // to avoid sorting in GetReferencedCells
}

FormulaAST::~FormulaAST() = default;
