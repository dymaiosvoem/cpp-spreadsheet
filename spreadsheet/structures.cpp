#include "common.h"

#include <cctype>
#include <sstream>
#include <algorithm>

using namespace std;

const int LETTERS = 26;
const int MAX_POSITION_LENGTH = 17;
const int MAX_POS_LETTER_COUNT = 3;

const Position Position::NONE = {-1, -1};

bool Position::operator==(const Position rhs) const {
    return tie(row, col) == tie(rhs.row, rhs.col);
}

bool Position::operator<(const Position rhs) const {
    return tie(row, col) < tie(rhs.row, rhs.col);
}

bool Position::IsValid() const {
    return row >= 0 && col >= 0 && row < MAX_ROWS && col < MAX_COLS;
}

string Position::ToString() const {
    if (!IsValid()) {
        return "";
    }

    string result;
    result.reserve(MAX_POSITION_LENGTH);
    int c = col;
    while (c >= 0) {
        result.insert(result.begin(), 'A' + c % LETTERS);
        c = c / LETTERS - 1;
    }

    result += to_string(row + 1);

    return result;
}

Position Position::FromString(string_view str) {
    auto it = find_if(str.begin(), str.end(), [](const char c) {
        return !(isalpha(c) && isupper(c));
    });
    auto letters = str.substr(0, it - str.begin());
    auto digits = str.substr(it - str.begin());

    if (letters.empty() || digits.empty()) {
        return Position::NONE;
    }
    if (letters.size() > MAX_POS_LETTER_COUNT) {
        return Position::NONE;
    }

    if (!isdigit(digits[0])) {
        return Position::NONE;
    }

    int row;
    istringstream row_in{string{digits}};
    if (!(row_in >> row) || !row_in.eof()) {
        return Position::NONE;
    }

    int col = 0;
    for (char ch : letters) {
        col *= LETTERS;
        col += ch - 'A' + 1;
    }

    return { row - 1, col - 1 };
}

bool Size::operator==(Size rhs) const {
    return tie(rows, cols) == tie(rhs.rows, rhs.cols);
}