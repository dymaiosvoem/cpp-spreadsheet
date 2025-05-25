#include "cell.h"

#include <cassert>
#include <iostream>
#include <string>
#include <optional>

using namespace std;

class Cell::Impl {
public:
    virtual Value GetValue() const = 0;
    virtual string GetText() const = 0;
    virtual vector<Position> GetReferencedCells() const;
    virtual bool HasCache() const;   
    virtual void InvalidateCache();
    virtual ~Impl() = default;
};

class Cell::EmptyImpl : public Cell::Impl {
public:
    Value GetValue() const override {
        return "";
    }

    string GetText() const override {
        return "";
    }
};

class Cell::TextImpl : public Cell::Impl {
public:
    explicit TextImpl(string text) : text_(text) {}

    Value GetValue() const override {
        if (!text_.empty() && text_[0] == '\'') {
            return text_.substr(1);
        }
        return text_;
    }

    string GetText() const override {
        return text_;
    }
private:
    string text_;
};

class Cell::FormulaImpl : public Cell::Impl {
public:
    explicit FormulaImpl(string formula, SheetInterface& sheet) : formula_(ParseFormula(formula)), formula_sheet_(sheet) {}

    Value GetValue() const override {
        if (!cache_.has_value()) {
            cache_ = formula_->Evaluate(formula_sheet_);
        }
        
        if (holds_alternative<double>(cache_.value())) {
            return get<double>(cache_.value());
        } else {
            return get<FormulaError>(cache_.value());
        }
    }

    string GetText() const override {
        return FORMULA_SIGN + formula_->GetExpression();
    }

    vector<Position> GetReferencedCells() const override {
        return formula_->GetReferencedCells();
    }
        
    bool HasCache() const override {
        return cache_.has_value();
    }  

    void InvalidateCache() override {
        cache_.reset();
    }
private:
    unique_ptr<FormulaInterface> formula_;
    const SheetInterface& formula_sheet_;
    mutable optional<FormulaInterface::Value> cache_;
};

vector<Position> Cell::Impl::GetReferencedCells() const {
    return {};
}

bool Cell::Impl::HasCache() const {
    return true;
}

void Cell::Impl::InvalidateCache() {}

Cell::Cell(SheetInterface& sheet) : impl_(make_unique<EmptyImpl>()), sheet_(sheet) {}

Cell::~Cell() = default;

void Cell::Set(string text) {
    if (text.empty()) {
        impl_ = make_unique<EmptyImpl>();
    } else if (text[0] == ESCAPE_SIGN) {
        impl_ = make_unique<TextImpl>(text);
    } else if (text[0] == FORMULA_SIGN && text.size() > 1) {
        const Impl& formula = FormulaImpl(text.substr(1), sheet_);
        auto referensed_cells_pos = formula.GetReferencedCells();

        if (!referensed_cells_pos.empty()) {
            if (CheckCircularDependencies(referensed_cells_pos)) {
                throw CircularDependencyException("Circular dependency detected");
            }
        }

        InvalidateCache();
        UpdateDependencies(referensed_cells_pos);

        impl_ = make_unique<FormulaImpl>(text.substr(1), sheet_);
    } else {
        impl_ = make_unique<TextImpl>(text);
    }
}

void Cell::Clear() {
    Set("");
}

Cell::Value Cell::GetValue() const {
    return impl_->GetValue();
}

string Cell::GetText() const {
    return impl_->GetText();
}

vector<Position> Cell::GetReferencedCells() const {
    return impl_->GetReferencedCells();
}

bool Cell::IsReferenced() const {
    return !dependent_cells_.empty();
}

bool Cell::HasCache() const {
    return impl_->HasCache();
}

void Cell::InvalidateCache() {
    if (impl_->HasCache()) {
        impl_->InvalidateCache();

        for (auto cell : dependent_cells_) {
            cell->InvalidateCache();
        }
    } 
}

bool Cell::CheckCircularDependencies(const vector<Position>& refs) {
    for (auto& pos : refs) {
        const Cell* cell_ptr = dynamic_cast<const Cell*>(sheet_.GetCell(pos));

        if (!cell_ptr) {
            continue;
        }

        vector<const Cell*> stack;
        stack.push_back(cell_ptr);

        unordered_set<const Cell*> visited;

        while (!stack.empty()) {
            const Cell* current = stack.back();
            stack.pop_back();

            if (current == this) {
                return true;
            }

            visited.insert(current);

            for (auto& ref_cell : current->referenced_cells_) {
                if (!ref_cell) {
                    continue;
                }

                stack.push_back(ref_cell);
            }
        }
    }
    return false;
}


void Cell::UpdateDependencies(vector<Position>& referenced_cells_pos) {
    for (auto& ref_cell : referenced_cells_) {
        ref_cell->dependent_cells_.erase(this);
    }

    referenced_cells_.clear();

    for (auto& pos : referenced_cells_pos) {
        Cell* cell_ptr = dynamic_cast<Cell*>(sheet_.GetCell(pos));

        if (!cell_ptr) {
            sheet_.SetCell(pos, "");
            cell_ptr = dynamic_cast<Cell*>(sheet_.GetCell(pos));
        }

        referenced_cells_.insert(cell_ptr);
        cell_ptr->dependent_cells_.insert(this);
    }
}
