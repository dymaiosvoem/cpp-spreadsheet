#pragma once

#include "common.h"
#include "formula.h"

#include <unordered_set>

class Cell : public CellInterface {
public:
    Cell(SheetInterface& sheet);
    ~Cell();

    void Set(std::string text);
    void Clear();

    Value GetValue() const override;
    std::string GetText() const override;
    std::vector<Position> GetReferencedCells() const override;

    bool IsReferenced() const;
    bool HasCache() const;
    
    void InvalidateCache();
private:
    class Impl;
    class EmptyImpl;
    class TextImpl;
    class FormulaImpl;

    std::unique_ptr<Impl> impl_;
    SheetInterface& sheet_;

    // на кого ссылается ячейка / кто ссылается на ячейку, 
    // 1) при добавлении ячейки смотрю циклические зависимости,
    // 2) при изменении одной из ячеек, нужно смотреть кто использует
    // измененную ячейку, чтобы инвалидировать кэш
    std::unordered_set<Cell*> referenced_cells_, dependent_cells_;

    bool CheckCircularDependencies(const std::vector<Position>& referenced_cells_pos);
    void UpdateDependencies(std::vector<Position>& referenced_cells_pos);
};