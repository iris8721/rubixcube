#pragma once

#include "cube_solver.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cube {

class KociembaSolver final : public ICubeSolver {
public:
    SolveResult Solve(const CubeState& state) override;
    const char* Name() const override;
    std::string LastDebugInfo() const override;

private:
    struct Tables {
        bool initialized = false;
        std::array<std::array<std::uint16_t, 18>, 2187> twistMove{};
        std::array<std::array<std::uint16_t, 18>, 2048> flipMove{};
        std::array<std::array<std::uint16_t, 18>, 495> sliceMove{};
        std::array<std::array<std::uint16_t, 10>, 40320> cornerPermMove{};
        std::array<std::array<std::uint16_t, 10>, 40320> udEdgePermMove{};
        std::array<std::array<std::uint16_t, 10>, 24> slicePermMove{};
        std::vector<unsigned char> phase1TwistSlicePrune;
        std::vector<unsigned char> phase1FlipSlicePrune;
        std::vector<unsigned char> phase2CornerSlicePrune;
        std::vector<unsigned char> phase2EdgeSlicePrune;
    };

    struct SearchContext {
        std::vector<int> phase1Path;
        std::vector<int> phase2Path;
        std::optional<std::vector<int>> solution;
        std::uint64_t phase1Nodes = 0;
        std::uint64_t phase2Nodes = 0;
        int foundPhase1Depth = -1;
        int foundPhase2Depth = -1;
    };

    static Tables& GetTables();
    static void InitializeTables(Tables& tables);

    static CubeState MoveCubeQuarter(Face face);
    static CubeState Compose(const CubeState& state, const CubeState& move);
    static const std::array<CubeState, 18>& MoveCubes18();
    static const std::array<int, 10>& Phase2Moves18();
    static void ApplyMove(CubeState& state, int move18);
    static int Move18(Face face, int turnQuarter);
    static Face FaceFromMove18(int move18);
    static int TurnFromMove18(int move18);
    static int AxisFromFace(Face face);
    static Move MoveFromMove18(int move18);

    static int GetTwist(const CubeState& state);
    static int GetFlip(const CubeState& state);
    static int GetSlice(const CubeState& state);
    static int SolvedSliceCoordinate();
    static int GetCornerPerm(const CubeState& state);
    static int GetUdEdgePerm(const CubeState& state);
    static int GetSlicePerm(const CubeState& state);

    static void SetTwist(CubeState& state, int twist);
    static void SetFlip(CubeState& state, int flip);
    static void SetSlice(CubeState& state, int slice);
    static void SetCornerPerm(CubeState& state, int cornerPerm);
    static void SetUdEdgePerm(CubeState& state, int udEdgePerm);
    static void SetSlicePerm(CubeState& state, int slicePerm);

    static int PermToIndex(const int* perm, int length);
    static void IndexToPerm(int index, int length, int* outPerm);
    static int Binomial(int n, int k);
    static bool IsSliceEdge(int edgePiece);

    static void BuildMoveTables(Tables& tables);
    static void BuildPruneTables(Tables& tables);
    static bool LoadPruneCache(Tables& tables, std::string& errorMessage);
    static bool SavePruneCache(const Tables& tables, std::string& errorMessage);
    static std::uint32_t HashTables(const Tables& tables);

    static bool SearchPhase2(
        const Tables& tables,
        int cornerPerm,
        int udEdgePerm,
        int slicePerm,
        int depthRemaining,
        int lastFace,
        SearchContext& ctx);

    static bool SearchPhase1(
        const Tables& tables,
        const CubeState& currentState,
        int twist,
        int flip,
        int slice,
        int depthRemaining,
        int lastFace,
        SearchContext& ctx);

    static std::optional<MoveSequence> SolveTwoPhase(
        const CubeState& initial,
        std::string& statusMessage,
        SearchContext& ctx);

    std::string lastStatusMessage_;
    double lastSolveMs_ = 0.0;
    int lastSolutionLength_ = 0;
    int lastPhase1Depth_ = -1;
    int lastPhase2Depth_ = -1;
    std::uint64_t lastPhase1Nodes_ = 0;
    std::uint64_t lastPhase2Nodes_ = 0;
};

} // namespace cube
