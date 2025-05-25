#include "formula.h"

#include "FormulaAST.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <sstream>

using namespace std;
using namespace literals;

ostream& operator<<(ostream& output, FormulaError fe) {
    return output << "#ARITHM!";
}

namespace {
class Formula : public FormulaInterface {
public:
    explicit Formula(string expression) try 
        : ast_(ParseFormulaAST(expression)) {
    } catch (const exception& e) {
        throw FormulaException(e.what());
    }
    
    // new const SheetInterface& sheet
    Value Evaluate(const SheetInterface& sheet) const override {
        try {
            double result = ast_.Execute(sheet);
            return result;
        } catch (const FormulaError& err) {
            return err;
        }
    }

    string GetExpression() const override {
        ostringstream oss;
        ast_.PrintFormula(oss);
        return oss.str();
    } 

    vector<Position> GetReferencedCells() const {
        vector<Position> cells;

        for (auto& cell : ast_.GetCells()) {
            if (!cell.IsValid()) {
                continue;
            } else {
                cells.push_back(cell);
            }
        }

        sort(cells.begin(), cells.end());
        cells.erase(unique(cells.begin(), cells.end()), cells.end());
        
        return cells;
    }
private:
    FormulaAST ast_;
};
}  // namespace

unique_ptr<FormulaInterface> ParseFormula(string expression) {
    return make_unique<Formula>(move(expression));
}