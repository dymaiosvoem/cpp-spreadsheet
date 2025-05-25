#include "sheet.h"

#include "cell.h"
#include "common.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>

using namespace std;
using namespace literals;

size_t SheetHash::operator()(Position pos) const {
    size_t h1 = hasher_(pos.row);
    size_t h2 = hasher_(pos.col);
    return h1 * 16'387 + h2;
}

Sheet::~Sheet() {}

void Sheet::SetCell(Position pos, string text) {
    if (!pos.IsValid()) {
        throw InvalidPositionException("Position is out of range");
    }
    
    auto it = sheet_.find(pos);

    if (it != sheet_.end()) {
        it->second.get()->Set(text);
    } else {
        auto [key, value] = sheet_.emplace(pos, make_unique<Cell>(*this));
        key->second.get()->Set(text);
    }

    UpdatePrintableSize(pos);
}

const CellInterface* Sheet::GetCell(Position pos) const {
    return FindCellInterfacePtr(pos);
}

CellInterface* Sheet::GetCell(Position pos) {
    return const_cast<CellInterface*>(FindCellInterfacePtr(pos));
}

void Sheet::ClearCell(Position pos) {
    if (!pos.IsValid()) {
        throw InvalidPositionException("Position is out of range");
    }

    auto it = sheet_.find(pos);

    if (it != sheet_.end()) {
        if (pos.row + 1 == printable_size_.rows || pos.col + 1 == printable_size_.cols) {
            sheet_.erase(pos);
            printable_size_ = { 0, 0 };

            for (auto& cell : sheet_) {
                UpdatePrintableSize(cell.first);
            }
            return;
        }
        sheet_.erase(pos);
    }
}

Size Sheet::GetPrintableSize() const {
    return printable_size_;
}

void Sheet::PrintValues(ostream& output) const {
    return PrintContext(output, "Values"s);
}

void Sheet::PrintTexts(ostream& output) const {
    return PrintContext(output, "Texts"s);
}

void Sheet::UpdatePrintableSize(Position pos) {
    printable_size_.rows = max(pos.row + 1, printable_size_.rows);
    printable_size_.cols = max(pos.col + 1, printable_size_.cols);
}

const CellInterface* Sheet::FindCellInterfacePtr(Position pos) const {
    if (!pos.IsValid()) {
        throw InvalidPositionException("out_of_range");
    }

    auto it = sheet_.find(pos);
    return it != sheet_.end() ? it->second.get() : nullptr;
}

void Sheet::PrintContext(ostream& output, string context) const {
    Size size = GetPrintableSize();

    for (int i = 0; i < size.rows; ++ i) {
        for(int j = 0; j < size.cols; ++j) {
            auto* cell = GetCell({ i, j });
            
            if (cell) {
                if (context == "Values"s) {
                    auto value = cell->GetValue();
                    visit([&](auto&& v) {
                        output << v;
                    }, value);
                } else {
                    output << cell->GetText();
                }
            }

            if (j + 1 < size.cols) {
                output << "\t";
            }
        }
        output << "\n";
    }
}

unique_ptr<SheetInterface> CreateSheet() {
    return make_unique<Sheet>();
}