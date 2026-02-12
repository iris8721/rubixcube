#include "cube_solver.h"

#include <sstream>

namespace {

int NormalizeTurns(int turns) {
    int normalized = turns % 4;
    if (normalized < 0) normalized += 4;
    return normalized;
}

char FaceToChar(cube::Face face) {
    switch (face) {
    case cube::Face::U: return 'U';
    case cube::Face::D: return 'D';
    case cube::Face::F: return 'F';
    case cube::Face::B: return 'B';
    case cube::Face::R: return 'R';
    case cube::Face::L: return 'L';
    }
    return '?';
}

} // namespace

namespace cube {

Move QuarterTurn(Face face, bool clockwise) {
    return { face, clockwise ? 1 : 3 };
}

bool IsClockwiseQuarter(const Move& move) {
    return NormalizeTurns(move.turns) == 1;
}

Move InverseMove(const Move& move) {
    int turns = NormalizeTurns(move.turns);
    if (turns == 0) return { move.face, 0 };
    return { move.face, 4 - turns };
}

std::string MoveToNotation(const Move& move) {
    int turns = NormalizeTurns(move.turns);
    if (turns == 0) return "";

    std::string token(1, FaceToChar(move.face));
    if (turns == 2) token += "2";
    if (turns == 3) token += "'";
    return token;
}

std::string MoveSequenceToString(const MoveSequence& moves) {
    std::ostringstream output;
    bool first = true;

    for (const Move& move : moves) {
        std::string token = MoveToNotation(move);
        if (token.empty()) continue;

        if (!first) output << ' ';
        output << token;
        first = false;
    }

    return output.str();
}

void CubeState::Reset() {
    reducedHistory_.clear();
}

void CubeState::ApplyMove(const Move& move) {
    int turns = NormalizeTurns(move.turns);
    if (turns == 0) return;

    Move normalized = { move.face, turns };

    if (!reducedHistory_.empty() && reducedHistory_.back().face == normalized.face) {
        int combined = NormalizeTurns(reducedHistory_.back().turns + normalized.turns);
        if (combined == 0) {
            reducedHistory_.pop_back();
        }
        else {
            reducedHistory_.back().turns = combined;
        }
        return;
    }

    reducedHistory_.push_back(normalized);
}

bool CubeState::IsSolved() const {
    return reducedHistory_.empty();
}

const MoveSequence& CubeState::ReducedHistory() const {
    return reducedHistory_;
}

} // namespace cube
