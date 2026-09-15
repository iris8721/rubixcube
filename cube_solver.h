#pragma once

#include <array>
#include <string>
#include <vector>

namespace cube {

enum class Face : int {
    U = 0,
    D = 1,
    F = 2,
    B = 3,
    R = 4,
    L = 5
};

struct Move {
    Face face = Face::U;
    // Number of clockwise quarter turns: 1 (90), 2 (180), 3 (-90).
    int turns = 1;
};

using MoveSequence = std::vector<Move>;

// Cubie-level state: corner/edge permutation and orientation, corners
// ordered URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB and edges UR, UF, UL, UB,
// DR, DF, DL, DB, FR, FL, BL, BR.
struct CubeState {
    std::array<int, 8> cp{};
    std::array<int, 8> co{};
    std::array<int, 12> ep{};
    std::array<int, 12> eo{};
};

struct ValidationResult {
    bool valid = false;
    std::string message;
};

struct SolveResult {
    MoveSequence moves;
    std::string message;
};

int NormalizeTurns(int turns);
Move QuarterTurn(Face face, bool clockwise);
bool IsClockwiseQuarter(const Move& move);
std::string MoveToNotation(const Move& move);
std::string MoveSequenceToString(const MoveSequence& moves);

CubeState SolvedCubeState();
bool IsSolved(const CubeState& state);
ValidationResult ValidateCubeState(const CubeState& state);

class ICubeSolver {
public:
    virtual ~ICubeSolver() = default;
    // The state must pass ValidateCubeState.
    virtual SolveResult Solve(const CubeState& state) = 0;
    virtual const char* Name() const = 0;
    virtual std::string LastDebugInfo() const { return ""; }
};

} // namespace cube
