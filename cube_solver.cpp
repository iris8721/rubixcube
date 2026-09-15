#include "cube_solver.h"

#include <sstream>

namespace {

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

template <size_t N>
bool AllValuesInRangeUnique(const std::array<int, N>& values, int minValue, int maxValue) {
    std::vector<bool> seen(static_cast<size_t>(maxValue - minValue + 1), false);
    for (int value : values) {
        if (value < minValue || value > maxValue) return false;
        size_t index = static_cast<size_t>(value - minValue);
        if (seen[index]) return false;
        seen[index] = true;
    }
    return true;
}

template <size_t N>
int PermutationParity(const std::array<int, N>& values) {
    int parity = 0;
    for (size_t i = 0; i < N; i++) {
        for (size_t j = i + 1; j < N; j++) {
            if (values[i] > values[j]) parity ^= 1;
        }
    }
    return parity;
}

} // namespace

namespace cube {

int NormalizeTurns(int turns) {
    int normalized = turns % 4;
    if (normalized < 0) normalized += 4;
    return normalized;
}

Move QuarterTurn(Face face, bool clockwise) {
    return { face, clockwise ? 1 : 3 };
}

bool IsClockwiseQuarter(const Move& move) {
    return NormalizeTurns(move.turns) == 1;
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

CubeState SolvedCubeState() {
    CubeState state;
    for (int i = 0; i < 8; i++) {
        state.cp[i] = i;
        state.co[i] = 0;
    }
    for (int i = 0; i < 12; i++) {
        state.ep[i] = i;
        state.eo[i] = 0;
    }
    return state;
}

bool IsSolved(const CubeState& state) {
    for (int i = 0; i < 8; i++) {
        if (state.cp[i] != i || state.co[i] != 0) return false;
    }
    for (int i = 0; i < 12; i++) {
        if (state.ep[i] != i || state.eo[i] != 0) return false;
    }
    return true;
}

ValidationResult ValidateCubeState(const CubeState& state) {
    ValidationResult result;

    if (!AllValuesInRangeUnique(state.cp, 0, 7)) {
        result.message = "Invalid corner permutation.";
        return result;
    }
    if (!AllValuesInRangeUnique(state.ep, 0, 11)) {
        result.message = "Invalid edge permutation.";
        return result;
    }

    int cornerOrientationSum = 0;
    for (int value : state.co) {
        if (value < 0 || value > 2) {
            result.message = "Invalid corner orientation.";
            return result;
        }
        cornerOrientationSum += value;
    }
    if (cornerOrientationSum % 3 != 0) {
        result.message = "Corner orientation sum is invalid.";
        return result;
    }

    int edgeOrientationSum = 0;
    for (int value : state.eo) {
        if (value < 0 || value > 1) {
            result.message = "Invalid edge orientation.";
            return result;
        }
        edgeOrientationSum += value;
    }
    if (edgeOrientationSum % 2 != 0) {
        result.message = "Edge orientation sum is invalid.";
        return result;
    }

    if (PermutationParity(state.cp) != PermutationParity(state.ep)) {
        result.message = "Corner/edge permutation parity mismatch.";
        return result;
    }

    result.valid = true;
    result.message = "Cube state is valid.";
    return result;
}

} // namespace cube
