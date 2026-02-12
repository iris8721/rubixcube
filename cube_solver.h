#pragma once

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

struct SolveResult {
    MoveSequence moves;
    bool usedFallback = false;
    std::string message;
};

Move QuarterTurn(Face face, bool clockwise);
bool IsClockwiseQuarter(const Move& move);
Move InverseMove(const Move& move);
std::string MoveToNotation(const Move& move);
std::string MoveSequenceToString(const MoveSequence& moves);

class CubeState {
public:
    void Reset();
    void ApplyMove(const Move& move);
    bool IsSolved() const;
    const MoveSequence& ReducedHistory() const;

private:
    MoveSequence reducedHistory_;
};

class ICubeSolver {
public:
    virtual ~ICubeSolver() = default;
    virtual SolveResult Solve(const CubeState& state) = 0;
    virtual const char* Name() const = 0;
    virtual std::string LastDebugInfo() const { return ""; }
};

} // namespace cube
