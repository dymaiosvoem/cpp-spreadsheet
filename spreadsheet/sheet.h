#pragma once

#include "cell.h"
#include "common.h"

#include <functional>

struct SheetHash {
    size_t operator()(Position pos) const;

    std::hash<int> hasher_;
};

class Sheet : public SheetInterface {
public:
    ~Sheet();

    void SetCell(Position pos, std::string text) override;

    const CellInterface* GetCell(Position pos) const override;
    CellInterface* GetCell(Position pos) override;

    void ClearCell(Position pos) override;

    Size GetPrintableSize() const override;

    void PrintValues(std::ostream& output) const override;
    void PrintTexts(std::ostream& output) const override;
private:
    std::unordered_map<Position, std::unique_ptr<Cell>, SheetHash> sheet_;
    Size printable_size_;

    void UpdatePrintableSize(Position pos);
    const CellInterface* FindCellInterfacePtr(Position pos) const;
    void PrintContext(std::ostream& output, std::string context) const;
};