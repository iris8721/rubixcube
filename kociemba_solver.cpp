#include "kociemba_solver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::array<int, 13> kFactorial{
    1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880, 3628800, 39916800, 479001600
};

constexpr std::uint32_t kCacheVersion = 3;
constexpr const char* kCacheMagic = "KOCIEMBA_CACHE";
constexpr const char* kCacheFileName = "kociemba_cache.bin";

struct CacheHeader {
    char magic[24];
    std::uint32_t version = 0;
    std::uint32_t twistMoveSize = 0;
    std::uint32_t flipMoveSize = 0;
    std::uint32_t sliceMoveSize = 0;
    std::uint32_t cornerPermMoveSize = 0;
    std::uint32_t udEdgePermMoveSize = 0;
    std::uint32_t slicePermMoveSize = 0;
    std::uint32_t phase1TwistSliceSize = 0;
    std::uint32_t phase1FlipSliceSize = 0;
    std::uint32_t phase2CornerSliceSize = 0;
    std::uint32_t phase2EdgeSliceSize = 0;
};

struct TableInitStats {
    bool initialized = false;
    bool loadedPruneCache = false;
    bool savedPruneCache = false;
    std::string cachePath;
    std::string cacheError;
    double initMs = 0.0;
};

int NormalizeTurns(int turns) {
    int normalized = turns % 4;
    if (normalized < 0) normalized += 4;
    return normalized;
}

TableInitStats& GetTableInitStats() {
    static TableInitStats stats;
    return stats;
}

std::filesystem::path GetCachePath() {
    return std::filesystem::current_path() / kCacheFileName;
}

std::string CompactCount(std::uint64_t value) {
    static const char* suffixes[] = { "", "k", "M", "B" };
    double scaled = static_cast<double>(value);
    int suffix = 0;

    while (scaled >= 1000.0 && suffix < 3) {
        scaled /= 1000.0;
        suffix++;
    }

    std::ostringstream out;
    out.setf(std::ios::fixed);
    if (suffix == 0 || scaled >= 100.0) out.precision(0);
    else if (scaled >= 10.0) out.precision(1);
    else out.precision(2);
    out << scaled << suffixes[suffix];
    return out.str();
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

bool WriteBytes(std::ofstream& out, const void* data, std::size_t size) {
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

bool ReadBytes(std::ifstream& in, void* data, std::size_t size) {
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return in.good();
}

} // namespace

namespace cube {

SolveResult KociembaSolver::Solve(const CubeState& state) {
    auto solveStart = std::chrono::steady_clock::now();
    SolveResult result;
    SearchContext context;

    if (state.IsSolved()) {
        result.message = "Cube is already solved.";
        lastUsedFallback_ = false;
        lastStatusMessage_ = result.message;
        lastSolveMs_ = 0.0;
        lastSolutionLength_ = 0;
        lastPhase1Depth_ = 0;
        lastPhase2Depth_ = 0;
        lastPhase1Nodes_ = 0;
        lastPhase2Nodes_ = 0;
        return result;
    }

    std::string statusMessage;
    std::optional<MoveSequence> twoPhase = SolveTwoPhase(state, statusMessage, context);
    if (twoPhase) {
        result.moves = *twoPhase;
        result.message = statusMessage;
        lastUsedFallback_ = false;
        lastStatusMessage_ = statusMessage;
        lastSolutionLength_ = static_cast<int>(result.moves.size());
        lastPhase1Depth_ = context.foundPhase1Depth;
        lastPhase2Depth_ = context.foundPhase2Depth;
        lastPhase1Nodes_ = context.phase1Nodes;
        lastPhase2Nodes_ = context.phase2Nodes;
        auto solveEnd = std::chrono::steady_clock::now();
        lastSolveMs_ = std::chrono::duration<double, std::milli>(solveEnd - solveStart).count();
        return result;
    }

    result.moves = SolveByInverseHistory(state);
    result.usedFallback = true;
    if (!statusMessage.empty()) {
        result.message = statusMessage + " Falling back to inverse-history solve.";
    }
    else {
        result.message = "Falling back to inverse-history solve.";
    }

    lastUsedFallback_ = true;
    lastStatusMessage_ = result.message;
    lastSolutionLength_ = static_cast<int>(result.moves.size());
    lastPhase1Depth_ = context.foundPhase1Depth;
    lastPhase2Depth_ = context.foundPhase2Depth;
    lastPhase1Nodes_ = context.phase1Nodes;
    lastPhase2Nodes_ = context.phase2Nodes;
    auto solveEnd = std::chrono::steady_clock::now();
    lastSolveMs_ = std::chrono::duration<double, std::milli>(solveEnd - solveStart).count();
    return result;
}

const char* KociembaSolver::Name() const {
    return "Kociemba two-phase";
}

std::string KociembaSolver::LastDebugInfo() const {
    const TableInitStats& init = GetTableInitStats();
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);

    if (!init.initialized) {
        text << "Init: pending (runs on first solve)";
    }
    else {
        text << "Init: " << init.initMs << " ms";
        if (init.loadedPruneCache) {
            text << " | cache: loaded";
        }
        else {
            text << " | cache: built";
        }
        if (init.savedPruneCache) {
            text << " (saved)";
        }
        if (!init.cacheError.empty()) {
            text << " | note: " << init.cacheError;
        }
        if (!init.cachePath.empty()) {
            std::filesystem::path cachePath(init.cachePath);
            text << "\nCache file: " << cachePath.filename().string();
        }
    }
    text << "\n";

    text << "Solve: " << lastSolveMs_ << " ms"
         << " | len: " << lastSolutionLength_
         << " | p1d: " << lastPhase1Depth_
         << " | p2d: " << lastPhase2Depth_;
    text << "\n";

    text << "Nodes: p1 " << CompactCount(lastPhase1Nodes_)
         << " | p2 " << CompactCount(lastPhase2Nodes_);
    if (lastUsedFallback_) {
        text << " | fallback";
    }
    else {
        text << " | fallback:no";
    }
    text << "\n";

    text << "Status: " << (lastStatusMessage_.empty() ? "n/a" : lastStatusMessage_);
    return text.str();
}

KociembaSolver::ValidationResult KociembaSolver::ValidateExplicitState(const ExplicitState& state) const {
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

SolveResult KociembaSolver::SolveExplicitState(const ExplicitState& state) {
    auto solveStart = std::chrono::steady_clock::now();
    SolveResult result;
    SearchContext context;

    ValidationResult validation = ValidateExplicitState(state);
    if (!validation.valid) {
        result.usedFallback = true;
        result.message = validation.message;

        lastUsedFallback_ = true;
        lastStatusMessage_ = validation.message;
        lastSolveMs_ = 0.0;
        lastSolutionLength_ = 0;
        lastPhase1Depth_ = -1;
        lastPhase2Depth_ = -1;
        lastPhase1Nodes_ = 0;
        lastPhase2Nodes_ = 0;
        return result;
    }

    std::string statusMessage;
    CoordinateState initial = BuildCoordinateState(state);
    std::optional<MoveSequence> twoPhase = SolveTwoPhase(initial, statusMessage, context);
    if (twoPhase) {
        result.moves = *twoPhase;
        result.message = statusMessage;

        lastUsedFallback_ = false;
        lastStatusMessage_ = statusMessage;
        lastSolutionLength_ = static_cast<int>(result.moves.size());
        lastPhase1Depth_ = context.foundPhase1Depth;
        lastPhase2Depth_ = context.foundPhase2Depth;
        lastPhase1Nodes_ = context.phase1Nodes;
        lastPhase2Nodes_ = context.phase2Nodes;
        auto solveEnd = std::chrono::steady_clock::now();
        lastSolveMs_ = std::chrono::duration<double, std::milli>(solveEnd - solveStart).count();
        return result;
    }

    result.usedFallback = true;
    result.message = !statusMessage.empty() ? statusMessage : "Unable to solve provided explicit state.";

    lastUsedFallback_ = true;
    lastStatusMessage_ = result.message;
    lastSolutionLength_ = 0;
    lastPhase1Depth_ = context.foundPhase1Depth;
    lastPhase2Depth_ = context.foundPhase2Depth;
    lastPhase1Nodes_ = context.phase1Nodes;
    lastPhase2Nodes_ = context.phase2Nodes;
    auto solveEnd = std::chrono::steady_clock::now();
    lastSolveMs_ = std::chrono::duration<double, std::milli>(solveEnd - solveStart).count();
    return result;
}

KociembaSolver::Tables& KociembaSolver::GetTables() {
    static Tables tables;
    static const bool initialized = [] {
        InitializeTables(tables);
        return true;
    }();
    (void)initialized;
    return tables;
}

void KociembaSolver::InitializeTables(Tables& tables) {
    if (tables.initialized) return;

    TableInitStats& init = GetTableInitStats();
    init = TableInitStats{};
    init.cachePath = GetCachePath().string();

    auto initStart = std::chrono::steady_clock::now();

    std::string cacheError;
    if (LoadPruneCache(tables, cacheError)) {
        init.loadedPruneCache = true;
    }
    else {
        if (!cacheError.empty()) init.cacheError = cacheError;
        BuildMoveTables(tables);
        BuildPruneTables(tables);
        std::string saveError;
        if (SavePruneCache(tables, saveError)) {
            init.savedPruneCache = true;
        }
        else if (!saveError.empty()) {
            if (!init.cacheError.empty()) init.cacheError += "; ";
            init.cacheError += saveError;
        }
    }

    auto initEnd = std::chrono::steady_clock::now();
    init.initMs = std::chrono::duration<double, std::milli>(initEnd - initStart).count();
    init.initialized = true;

    tables.initialized = true;
}

KociembaSolver::CubieCube KociembaSolver::SolvedCubieCube() {
    CubieCube cube;
    for (int i = 0; i < 8; i++) {
        cube.cp[i] = i;
        cube.co[i] = 0;
    }
    for (int i = 0; i < 12; i++) {
        cube.ep[i] = i;
        cube.eo[i] = 0;
    }
    return cube;
}

KociembaSolver::CubieCube KociembaSolver::MoveCubeQuarter(Face face) {
    CubieCube move = SolvedCubieCube();

    switch (face) {
    case Face::U:
        move.cp = { 3, 0, 1, 2, 4, 5, 6, 7 };
        move.ep = { 3, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11 };
        break;
    case Face::D:
        move.cp = { 0, 1, 2, 3, 5, 6, 7, 4 };
        move.ep = { 0, 1, 2, 3, 5, 6, 7, 4, 8, 9, 10, 11 };
        break;
    case Face::F:
        move.cp = { 1, 5, 2, 3, 0, 4, 6, 7 };
        move.co = { 1, 2, 0, 0, 2, 1, 0, 0 };
        move.ep = { 0, 9, 2, 3, 4, 8, 6, 7, 1, 5, 10, 11 };
        move.eo = { 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0 };
        break;
    case Face::B:
        move.cp = { 0, 1, 3, 7, 4, 5, 2, 6 };
        move.co = { 0, 0, 1, 2, 0, 0, 2, 1 };
        move.ep = { 0, 1, 2, 11, 4, 5, 6, 10, 8, 9, 3, 7 };
        move.eo = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1 };
        break;
    case Face::R:
        move.cp = { 4, 1, 2, 0, 7, 5, 6, 3 };
        move.co = { 2, 0, 0, 1, 1, 0, 0, 2 };
        move.ep = { 8, 1, 2, 3, 11, 5, 6, 7, 4, 9, 10, 0 };
        break;
    case Face::L:
        move.cp = { 0, 2, 6, 3, 4, 1, 5, 7 };
        move.co = { 0, 1, 2, 0, 0, 2, 1, 0 };
        move.ep = { 0, 1, 10, 3, 4, 5, 9, 7, 8, 2, 6, 11 };
        break;
    }

    return move;
}

KociembaSolver::CubieCube KociembaSolver::Compose(const CubieCube& state, const CubieCube& move) {
    CubieCube result;

    for (int i = 0; i < 8; i++) {
        result.cp[i] = state.cp[move.cp[i]];
        result.co[i] = (state.co[move.cp[i]] + move.co[i]) % 3;
    }

    for (int i = 0; i < 12; i++) {
        result.ep[i] = state.ep[move.ep[i]];
        result.eo[i] = (state.eo[move.ep[i]] + move.eo[i]) % 2;
    }

    return result;
}

const std::array<KociembaSolver::CubieCube, 18>& KociembaSolver::MoveCubes18() {
    static const std::array<CubieCube, 18> cubes = [] {
        std::array<CubieCube, 18> moves{};
        for (int faceIndex = 0; faceIndex < 6; faceIndex++) {
            Face face = static_cast<Face>(faceIndex);
            CubieCube quarter = MoveCubeQuarter(face);
            CubieCube half = Compose(quarter, quarter);
            CubieCube inverse = Compose(half, quarter);

            moves[Move18(face, 1)] = quarter;
            moves[Move18(face, 2)] = half;
            moves[Move18(face, 3)] = inverse;
        }
        return moves;
    }();

    return cubes;
}

const std::array<int, 10>& KociembaSolver::Phase2Moves18() {
    static const std::array<int, 10> moves{
        Move18(Face::U, 1), Move18(Face::U, 2), Move18(Face::U, 3),
        Move18(Face::D, 1), Move18(Face::D, 2), Move18(Face::D, 3),
        Move18(Face::F, 2), Move18(Face::B, 2), Move18(Face::R, 2), Move18(Face::L, 2)
    };
    return moves;
}

void KociembaSolver::ApplyMove(CubieCube& state, int move18) {
    state = Compose(state, MoveCubes18()[move18]);
}

int KociembaSolver::Move18(Face face, int turnQuarter) {
    return static_cast<int>(face) * 3 + (turnQuarter - 1);
}

Face KociembaSolver::FaceFromMove18(int move18) {
    return static_cast<Face>(move18 / 3);
}

int KociembaSolver::TurnFromMove18(int move18) {
    return (move18 % 3) + 1;
}

int KociembaSolver::AxisFromFace(Face face) {
    switch (face) {
    case Face::U:
    case Face::D:
        return 0;
    case Face::F:
    case Face::B:
        return 1;
    case Face::R:
    case Face::L:
        return 2;
    }
    return -1;
}

Move KociembaSolver::MoveFromMove18(int move18) {
    return { FaceFromMove18(move18), TurnFromMove18(move18) };
}

KociembaSolver::CoordinateState KociembaSolver::BuildCoordinateState(const CubeState& state) {
    CoordinateState coord;
    coord.cubie = SolvedCubieCube();

    for (const Move& move : state.ReducedHistory()) {
        int turns = NormalizeTurns(move.turns);
        if (turns == 0) continue;
        ApplyMove(coord.cubie, Move18(move.face, turns));
    }

    coord.twist = GetTwist(coord.cubie);
    coord.flip = GetFlip(coord.cubie);
    coord.slice = GetSlice(coord.cubie);
    coord.cornerPerm = GetCornerPerm(coord.cubie);
    coord.udEdgePerm = GetUdEdgePerm(coord.cubie);
    coord.slicePerm = GetSlicePerm(coord.cubie);
    return coord;
}

KociembaSolver::CoordinateState KociembaSolver::BuildCoordinateState(const ExplicitState& state) {
    CoordinateState coord;

    for (int i = 0; i < 8; i++) {
        coord.cubie.cp[i] = state.cp[i];
        coord.cubie.co[i] = state.co[i];
    }
    for (int i = 0; i < 12; i++) {
        coord.cubie.ep[i] = state.ep[i];
        coord.cubie.eo[i] = state.eo[i];
    }

    coord.twist = GetTwist(coord.cubie);
    coord.flip = GetFlip(coord.cubie);
    coord.slice = GetSlice(coord.cubie);
    coord.cornerPerm = GetCornerPerm(coord.cubie);
    coord.udEdgePerm = GetUdEdgePerm(coord.cubie);
    coord.slicePerm = GetSlicePerm(coord.cubie);
    return coord;
}

bool KociembaSolver::IsSolvedCubie(const CubieCube& state) {
    for (int i = 0; i < 8; i++) {
        if (state.cp[i] != i || state.co[i] != 0) return false;
    }
    for (int i = 0; i < 12; i++) {
        if (state.ep[i] != i || state.eo[i] != 0) return false;
    }
    return true;
}

int KociembaSolver::GetTwist(const CubieCube& state) {
    int twist = 0;
    for (int i = 0; i < 7; i++) {
        twist = twist * 3 + state.co[i];
    }
    return twist;
}

int KociembaSolver::GetFlip(const CubieCube& state) {
    int flip = 0;
    for (int i = 0; i < 11; i++) {
        flip = flip * 2 + state.eo[i];
    }
    return flip;
}

int KociembaSolver::GetSlice(const CubieCube& state) {
    int slice = 0;
    int r = 4;
    for (int position = 11; position >= 0 && r > 0; position--) {
        if (IsSliceEdge(state.ep[position])) {
            slice += Binomial(position, r);
            r--;
        }
    }
    return slice;
}

int KociembaSolver::SolvedSliceCoordinate() {
    static const int solvedSlice = GetSlice(SolvedCubieCube());
    return solvedSlice;
}

int KociembaSolver::GetCornerPerm(const CubieCube& state) {
    int perm[8];
    for (int i = 0; i < 8; i++) perm[i] = state.cp[i];
    return PermToIndex(perm, 8);
}

int KociembaSolver::GetUdEdgePerm(const CubieCube& state) {
    int perm[8];
    for (int i = 0; i < 8; i++) perm[i] = state.ep[i];
    return PermToIndex(perm, 8);
}

int KociembaSolver::GetSlicePerm(const CubieCube& state) {
    int perm[4];
    for (int i = 0; i < 4; i++) perm[i] = state.ep[8 + i] - 8;
    return PermToIndex(perm, 4);
}

void KociembaSolver::SetTwist(CubieCube& state, int twist) {
    int sum = 0;
    for (int i = 6; i >= 0; i--) {
        state.co[i] = twist % 3;
        sum += state.co[i];
        twist /= 3;
    }
    state.co[7] = (3 - (sum % 3)) % 3;
}

void KociembaSolver::SetFlip(CubieCube& state, int flip) {
    int sum = 0;
    for (int i = 10; i >= 0; i--) {
        state.eo[i] = flip % 2;
        sum += state.eo[i];
        flip /= 2;
    }
    state.eo[11] = (2 - (sum % 2)) % 2;
}

void KociembaSolver::SetSlice(CubieCube& state, int slice) {
    state.ep.fill(-1);
    int remaining = slice;
    int r = 4;

    for (int position = 11; position >= 0 && r > 0; position--) {
        int combination = Binomial(position, r);
        if (combination <= remaining) {
            remaining -= combination;
            state.ep[position] = 8 + (r - 1);
            r--;
        }
    }

    int edge = 0;
    for (int position = 0; position < 12; position++) {
        if (state.ep[position] == -1) {
            state.ep[position] = edge++;
        }
    }
}

void KociembaSolver::SetCornerPerm(CubieCube& state, int cornerPerm) {
    int perm[8];
    IndexToPerm(cornerPerm, 8, perm);
    for (int i = 0; i < 8; i++) state.cp[i] = perm[i];
}

void KociembaSolver::SetUdEdgePerm(CubieCube& state, int udEdgePerm) {
    int perm[8];
    IndexToPerm(udEdgePerm, 8, perm);
    for (int i = 0; i < 8; i++) state.ep[i] = perm[i];
    for (int i = 8; i < 12; i++) state.ep[i] = i;
}

void KociembaSolver::SetSlicePerm(CubieCube& state, int slicePerm) {
    int perm[4];
    IndexToPerm(slicePerm, 4, perm);
    for (int i = 0; i < 8; i++) state.ep[i] = i;
    for (int i = 0; i < 4; i++) state.ep[8 + i] = 8 + perm[i];
}

int KociembaSolver::PermToIndex(const int* perm, int length) {
    int index = 0;
    for (int i = 0; i < length; i++) {
        int smaller = 0;
        for (int j = i + 1; j < length; j++) {
            if (perm[j] < perm[i]) smaller++;
        }
        index += smaller * kFactorial[length - 1 - i];
    }
    return index;
}

void KociembaSolver::IndexToPerm(int index, int length, int* outPerm) {
    if (!outPerm || length <= 0 || length > 12) return;
    if (index < 0) index = 0;

    std::array<int, 12> values{};
    for (int i = 0; i < length; i++) values[i] = i;

    int active = length;
    for (int i = 0; i < length; i++) {
        int factorIndex = length - 1 - i;
        int factor = (factorIndex >= 0 && factorIndex < static_cast<int>(kFactorial.size()))
            ? kFactorial[factorIndex]
            : 1;
        if (factor <= 0) factor = 1;

        int pick = index / factor;
        index %= factor;

        pick = std::clamp(pick, 0, active - 1);
        outPerm[i] = values[pick];

        for (int j = pick + 1; j < active; j++) {
            values[j - 1] = values[j];
        }
        active--;
    }
}

int KociembaSolver::Binomial(int n, int k) {
    if (k < 0 || k > n) return 0;

    static const auto choose = [] {
        std::array<std::array<int, 13>, 13> table{};
        for (int i = 0; i <= 12; i++) {
            table[i][0] = 1;
            table[i][i] = 1;
            for (int j = 1; j < i; j++) {
                table[i][j] = table[i - 1][j - 1] + table[i - 1][j];
            }
        }
        return table;
    }();

    return choose[n][k];
}

bool KociembaSolver::IsSliceEdge(int edgePiece) {
    return edgePiece >= 8 && edgePiece <= 11;
}

void KociembaSolver::BuildMoveTables(Tables& tables) {
    for (int twist = 0; twist < 2187; twist++) {
        CubieCube base = SolvedCubieCube();
        SetTwist(base, twist);
        for (int move = 0; move < 18; move++) {
            CubieCube next = base;
            ApplyMove(next, move);
            tables.twistMove[twist][move] = static_cast<std::uint16_t>(GetTwist(next));
        }
    }

    for (int flip = 0; flip < 2048; flip++) {
        CubieCube base = SolvedCubieCube();
        SetFlip(base, flip);
        for (int move = 0; move < 18; move++) {
            CubieCube next = base;
            ApplyMove(next, move);
            tables.flipMove[flip][move] = static_cast<std::uint16_t>(GetFlip(next));
        }
    }

    for (int slice = 0; slice < 495; slice++) {
        CubieCube base = SolvedCubieCube();
        SetSlice(base, slice);
        for (int move = 0; move < 18; move++) {
            CubieCube next = base;
            ApplyMove(next, move);
            tables.sliceMove[slice][move] = static_cast<std::uint16_t>(GetSlice(next));
        }
    }

    const std::array<int, 10>& phase2Moves = Phase2Moves18();

    for (int cornerPerm = 0; cornerPerm < 40320; cornerPerm++) {
        CubieCube base = SolvedCubieCube();
        SetCornerPerm(base, cornerPerm);
        for (int move = 0; move < 10; move++) {
            CubieCube next = base;
            ApplyMove(next, phase2Moves[move]);
            tables.cornerPermMove[cornerPerm][move] = static_cast<std::uint16_t>(GetCornerPerm(next));
        }
    }

    for (int edgePerm = 0; edgePerm < 40320; edgePerm++) {
        CubieCube base = SolvedCubieCube();
        SetUdEdgePerm(base, edgePerm);
        for (int move = 0; move < 10; move++) {
            CubieCube next = base;
            ApplyMove(next, phase2Moves[move]);
            tables.udEdgePermMove[edgePerm][move] = static_cast<std::uint16_t>(GetUdEdgePerm(next));
        }
    }

    for (int slicePerm = 0; slicePerm < 24; slicePerm++) {
        CubieCube base = SolvedCubieCube();
        SetSlicePerm(base, slicePerm);
        for (int move = 0; move < 10; move++) {
            CubieCube next = base;
            ApplyMove(next, phase2Moves[move]);
            tables.slicePermMove[slicePerm][move] = static_cast<std::uint16_t>(GetSlicePerm(next));
        }
    }
}

void KociembaSolver::BuildPruneTables(Tables& tables) {
    const int solvedTwist = 0;
    const int solvedFlip = 0;
    const int solvedSlice = SolvedSliceCoordinate();
    const int solvedCornerPerm = 0;
    const int solvedUdEdgePerm = 0;
    const int solvedSlicePerm = 0;

    tables.phase1TwistSlicePrune.assign(2187 * 495, 0xFF);
    tables.phase1FlipSlicePrune.assign(2048 * 495, 0xFF);
    tables.phase2CornerSlicePrune.assign(40320 * 24, 0xFF);
    tables.phase2EdgeSlicePrune.assign(40320 * 24, 0xFF);

    {
        std::vector<int> queue;
        queue.reserve(tables.phase1TwistSlicePrune.size());
        size_t head = 0;

        int root = solvedTwist * 495 + solvedSlice;
        tables.phase1TwistSlicePrune[root] = 0;
        queue.push_back(root);

        while (head < queue.size()) {
            int index = queue[head++];
            int depth = tables.phase1TwistSlicePrune[index];
            int twist = index / 495;
            int slice = index % 495;

            for (int move = 0; move < 18; move++) {
                int nextTwist = tables.twistMove[twist][move];
                int nextSlice = tables.sliceMove[slice][move];
                int nextIndex = nextTwist * 495 + nextSlice;
                if (tables.phase1TwistSlicePrune[nextIndex] != 0xFF) continue;
                tables.phase1TwistSlicePrune[nextIndex] = (unsigned char)(depth + 1);
                queue.push_back(nextIndex);
            }
        }
    }

    {
        std::vector<int> queue;
        queue.reserve(tables.phase1FlipSlicePrune.size());
        size_t head = 0;

        int root = solvedFlip * 495 + solvedSlice;
        tables.phase1FlipSlicePrune[root] = 0;
        queue.push_back(root);

        while (head < queue.size()) {
            int index = queue[head++];
            int depth = tables.phase1FlipSlicePrune[index];
            int flip = index / 495;
            int slice = index % 495;

            for (int move = 0; move < 18; move++) {
                int nextFlip = tables.flipMove[flip][move];
                int nextSlice = tables.sliceMove[slice][move];
                int nextIndex = nextFlip * 495 + nextSlice;
                if (tables.phase1FlipSlicePrune[nextIndex] != 0xFF) continue;
                tables.phase1FlipSlicePrune[nextIndex] = (unsigned char)(depth + 1);
                queue.push_back(nextIndex);
            }
        }
    }

    {
        std::vector<int> queue;
        queue.reserve(tables.phase2CornerSlicePrune.size());
        size_t head = 0;

        int root = solvedCornerPerm * 24 + solvedSlicePerm;
        tables.phase2CornerSlicePrune[root] = 0;
        queue.push_back(root);

        while (head < queue.size()) {
            int index = queue[head++];
            int depth = tables.phase2CornerSlicePrune[index];
            int cornerPerm = index / 24;
            int slicePerm = index % 24;

            for (int move = 0; move < 10; move++) {
                int nextCornerPerm = tables.cornerPermMove[cornerPerm][move];
                int nextSlicePerm = tables.slicePermMove[slicePerm][move];
                int nextIndex = nextCornerPerm * 24 + nextSlicePerm;
                if (tables.phase2CornerSlicePrune[nextIndex] != 0xFF) continue;
                tables.phase2CornerSlicePrune[nextIndex] = (unsigned char)(depth + 1);
                queue.push_back(nextIndex);
            }
        }
    }

    {
        std::vector<int> queue;
        queue.reserve(tables.phase2EdgeSlicePrune.size());
        size_t head = 0;

        int root = solvedUdEdgePerm * 24 + solvedSlicePerm;
        tables.phase2EdgeSlicePrune[root] = 0;
        queue.push_back(root);

        while (head < queue.size()) {
            int index = queue[head++];
            int depth = tables.phase2EdgeSlicePrune[index];
            int udEdgePerm = index / 24;
            int slicePerm = index % 24;

            for (int move = 0; move < 10; move++) {
                int nextUdEdgePerm = tables.udEdgePermMove[udEdgePerm][move];
                int nextSlicePerm = tables.slicePermMove[slicePerm][move];
                int nextIndex = nextUdEdgePerm * 24 + nextSlicePerm;
                if (tables.phase2EdgeSlicePrune[nextIndex] != 0xFF) continue;
                tables.phase2EdgeSlicePrune[nextIndex] = (unsigned char)(depth + 1);
                queue.push_back(nextIndex);
            }
        }
    }
}

bool KociembaSolver::LoadPruneCache(Tables& tables, std::string& errorMessage) {
    std::ifstream in(GetCachePath(), std::ios::binary);
    if (!in.is_open()) {
        errorMessage = "cache file missing";
        return false;
    }

    CacheHeader header{};
    if (!ReadBytes(in, &header, sizeof(header))) {
        errorMessage = "cache header read failed";
        return false;
    }

    if (std::strncmp(header.magic, kCacheMagic, std::strlen(kCacheMagic) + 1) != 0) {
        errorMessage = "cache magic mismatch";
        return false;
    }
    if (header.version != kCacheVersion) {
        errorMessage = "cache version mismatch";
        return false;
    }

    if (header.twistMoveSize != 2187u * 18u ||
        header.flipMoveSize != 2048u * 18u ||
        header.sliceMoveSize != 495u * 18u ||
        header.cornerPermMoveSize != 40320u * 10u ||
        header.udEdgePermMoveSize != 40320u * 10u ||
        header.slicePermMoveSize != 24u * 10u ||
        header.phase1TwistSliceSize != 2187u * 495u ||
        header.phase1FlipSliceSize != 2048u * 495u ||
        header.phase2CornerSliceSize != 40320u * 24u ||
        header.phase2EdgeSliceSize != 40320u * 24u) {
        errorMessage = "cache table sizes mismatch";
        return false;
    }

    tables.phase1TwistSlicePrune.resize(header.phase1TwistSliceSize);
    tables.phase1FlipSlicePrune.resize(header.phase1FlipSliceSize);
    tables.phase2CornerSlicePrune.resize(header.phase2CornerSliceSize);
    tables.phase2EdgeSlicePrune.resize(header.phase2EdgeSliceSize);

    if (!ReadBytes(in, tables.twistMove.data(), sizeof(tables.twistMove))) {
        errorMessage = "cache twist move read failed";
        return false;
    }
    if (!ReadBytes(in, tables.flipMove.data(), sizeof(tables.flipMove))) {
        errorMessage = "cache flip move read failed";
        return false;
    }
    if (!ReadBytes(in, tables.sliceMove.data(), sizeof(tables.sliceMove))) {
        errorMessage = "cache slice move read failed";
        return false;
    }
    if (!ReadBytes(in, tables.cornerPermMove.data(), sizeof(tables.cornerPermMove))) {
        errorMessage = "cache corner move read failed";
        return false;
    }
    if (!ReadBytes(in, tables.udEdgePermMove.data(), sizeof(tables.udEdgePermMove))) {
        errorMessage = "cache edge move read failed";
        return false;
    }
    if (!ReadBytes(in, tables.slicePermMove.data(), sizeof(tables.slicePermMove))) {
        errorMessage = "cache slice-perm move read failed";
        return false;
    }

    if (!ReadBytes(in, tables.phase1TwistSlicePrune.data(), tables.phase1TwistSlicePrune.size())) {
        errorMessage = "cache phase1 twist/slice read failed";
        return false;
    }
    if (!ReadBytes(in, tables.phase1FlipSlicePrune.data(), tables.phase1FlipSlicePrune.size())) {
        errorMessage = "cache phase1 flip/slice read failed";
        return false;
    }
    if (!ReadBytes(in, tables.phase2CornerSlicePrune.data(), tables.phase2CornerSlicePrune.size())) {
        errorMessage = "cache phase2 corner/slice read failed";
        return false;
    }
    if (!ReadBytes(in, tables.phase2EdgeSlicePrune.data(), tables.phase2EdgeSlicePrune.size())) {
        errorMessage = "cache phase2 edge/slice read failed";
        return false;
    }

    return true;
}

bool KociembaSolver::SavePruneCache(const Tables& tables, std::string& errorMessage) {
    std::ofstream out(GetCachePath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        errorMessage = "cache file open failed";
        return false;
    }

    CacheHeader header{};
    std::memset(header.magic, 0, sizeof(header.magic));
    std::memcpy(header.magic, kCacheMagic, std::strlen(kCacheMagic));
    header.version = kCacheVersion;
    header.twistMoveSize = static_cast<std::uint32_t>(2187u * 18u);
    header.flipMoveSize = static_cast<std::uint32_t>(2048u * 18u);
    header.sliceMoveSize = static_cast<std::uint32_t>(495u * 18u);
    header.cornerPermMoveSize = static_cast<std::uint32_t>(40320u * 10u);
    header.udEdgePermMoveSize = static_cast<std::uint32_t>(40320u * 10u);
    header.slicePermMoveSize = static_cast<std::uint32_t>(24u * 10u);
    header.phase1TwistSliceSize = static_cast<std::uint32_t>(tables.phase1TwistSlicePrune.size());
    header.phase1FlipSliceSize = static_cast<std::uint32_t>(tables.phase1FlipSlicePrune.size());
    header.phase2CornerSliceSize = static_cast<std::uint32_t>(tables.phase2CornerSlicePrune.size());
    header.phase2EdgeSliceSize = static_cast<std::uint32_t>(tables.phase2EdgeSlicePrune.size());

    if (!WriteBytes(out, &header, sizeof(header))) {
        errorMessage = "cache header write failed";
        return false;
    }
    if (!WriteBytes(out, tables.twistMove.data(), sizeof(tables.twistMove))) {
        errorMessage = "cache twist move write failed";
        return false;
    }
    if (!WriteBytes(out, tables.flipMove.data(), sizeof(tables.flipMove))) {
        errorMessage = "cache flip move write failed";
        return false;
    }
    if (!WriteBytes(out, tables.sliceMove.data(), sizeof(tables.sliceMove))) {
        errorMessage = "cache slice move write failed";
        return false;
    }
    if (!WriteBytes(out, tables.cornerPermMove.data(), sizeof(tables.cornerPermMove))) {
        errorMessage = "cache corner move write failed";
        return false;
    }
    if (!WriteBytes(out, tables.udEdgePermMove.data(), sizeof(tables.udEdgePermMove))) {
        errorMessage = "cache edge move write failed";
        return false;
    }
    if (!WriteBytes(out, tables.slicePermMove.data(), sizeof(tables.slicePermMove))) {
        errorMessage = "cache slice-perm move write failed";
        return false;
    }
    if (!WriteBytes(out, tables.phase1TwistSlicePrune.data(), tables.phase1TwistSlicePrune.size())) {
        errorMessage = "cache phase1 twist/slice write failed";
        return false;
    }
    if (!WriteBytes(out, tables.phase1FlipSlicePrune.data(), tables.phase1FlipSlicePrune.size())) {
        errorMessage = "cache phase1 flip/slice write failed";
        return false;
    }
    if (!WriteBytes(out, tables.phase2CornerSlicePrune.data(), tables.phase2CornerSlicePrune.size())) {
        errorMessage = "cache phase2 corner/slice write failed";
        return false;
    }
    if (!WriteBytes(out, tables.phase2EdgeSlicePrune.data(), tables.phase2EdgeSlicePrune.size())) {
        errorMessage = "cache phase2 edge/slice write failed";
        return false;
    }

    return out.good();
}

bool KociembaSolver::SearchPhase2(
    const Tables& tables,
    int cornerPerm,
    int udEdgePerm,
    int slicePerm,
    int depthRemaining,
    int lastFace,
    SearchContext& ctx) {
    ctx.phase2Nodes++;

    int pruneCorner = tables.phase2CornerSlicePrune[cornerPerm * 24 + slicePerm];
    int pruneEdge = tables.phase2EdgeSlicePrune[udEdgePerm * 24 + slicePerm];
    int heuristic = std::max(pruneCorner, pruneEdge);
    if (heuristic > depthRemaining) return false;

    if (depthRemaining == 0) {
        return cornerPerm == 0 && udEdgePerm == 0 && slicePerm == 0;
    }

    const std::array<int, 10>& phase2Moves = Phase2Moves18();
    for (int moveLocal = 0; moveLocal < 10; moveLocal++) {
        int move18 = phase2Moves[moveLocal];
        Face face = FaceFromMove18(move18);
        int faceIndex = static_cast<int>(face);

        if (lastFace >= 0) {
            if (faceIndex == lastFace) continue;
            int axis = AxisFromFace(face);
            int lastAxis = AxisFromFace(static_cast<Face>(lastFace));
            if (axis == lastAxis && faceIndex < lastFace) continue;
        }

        int nextCornerPerm = tables.cornerPermMove[cornerPerm][moveLocal];
        int nextUdEdgePerm = tables.udEdgePermMove[udEdgePerm][moveLocal];
        int nextSlicePerm = tables.slicePermMove[slicePerm][moveLocal];

        ctx.phase2Path.push_back(move18);
        if (SearchPhase2(
            tables,
            nextCornerPerm,
            nextUdEdgePerm,
            nextSlicePerm,
            depthRemaining - 1,
            faceIndex,
            ctx)) {
            return true;
        }
        ctx.phase2Path.pop_back();
    }

    return false;
}

bool KociembaSolver::SearchPhase1(
    const Tables& tables,
    const CubieCube& currentState,
    int twist,
    int flip,
    int slice,
    int depthRemaining,
    int lastFace,
    SearchContext& ctx) {
    ctx.phase1Nodes++;

    int pruneTwist = tables.phase1TwistSlicePrune[twist * 495 + slice];
    int pruneFlip = tables.phase1FlipSlicePrune[flip * 495 + slice];
    int heuristic = std::max(pruneTwist, pruneFlip);
    if (heuristic > depthRemaining) return false;

    if (depthRemaining == 0) {
        if (twist != 0 || flip != 0 || slice != SolvedSliceCoordinate()) return false;

        int cornerPerm = GetCornerPerm(currentState);
        int udEdgePerm = GetUdEdgePerm(currentState);
        int slicePerm = GetSlicePerm(currentState);

        int phase2LowerBound = std::max(
            tables.phase2CornerSlicePrune[cornerPerm * 24 + slicePerm],
            tables.phase2EdgeSlicePrune[udEdgePerm * 24 + slicePerm]);

        for (int phase2Depth = phase2LowerBound; phase2Depth <= 18; phase2Depth++) {
            ctx.phase2Path.clear();
            if (SearchPhase2(tables, cornerPerm, udEdgePerm, slicePerm, phase2Depth, -1, ctx)) {
                std::vector<int> combined = ctx.phase1Path;
                combined.insert(combined.end(), ctx.phase2Path.begin(), ctx.phase2Path.end());
                ctx.solution = std::move(combined);
                ctx.foundPhase2Depth = phase2Depth;
                return true;
            }
        }

        return false;
    }

    for (int move18 = 0; move18 < 18; move18++) {
        Face face = FaceFromMove18(move18);
        int faceIndex = static_cast<int>(face);

        if (lastFace >= 0) {
            if (faceIndex == lastFace) continue;
            int axis = AxisFromFace(face);
            int lastAxis = AxisFromFace(static_cast<Face>(lastFace));
            if (axis == lastAxis && faceIndex < lastFace) continue;
        }

        int nextTwist = tables.twistMove[twist][move18];
        int nextFlip = tables.flipMove[flip][move18];
        int nextSlice = tables.sliceMove[slice][move18];

        CubieCube nextState = currentState;
        ApplyMove(nextState, move18);

        ctx.phase1Path.push_back(move18);
        if (SearchPhase1(
            tables,
            nextState,
            nextTwist,
            nextFlip,
            nextSlice,
            depthRemaining - 1,
            faceIndex,
            ctx)) {
            return true;
        }
        ctx.phase1Path.pop_back();
    }

    return false;
}

std::optional<MoveSequence> KociembaSolver::SolveTwoPhase(
    const CubeState& state,
    std::string& statusMessage,
    SearchContext& ctx) {
    CoordinateState initial = BuildCoordinateState(state);
    return SolveTwoPhase(initial, statusMessage, ctx);
}

std::optional<MoveSequence> KociembaSolver::SolveTwoPhase(
    const CoordinateState& initial,
    std::string& statusMessage,
    SearchContext& ctx) {
    const Tables& tables = GetTables();

    if (IsSolvedCubie(initial.cubie)) {
        statusMessage = "Cube is already solved.";
        return MoveSequence{};
    }

    int phase1LowerBound = std::max(
        tables.phase1TwistSlicePrune[initial.twist * 495 + initial.slice],
        tables.phase1FlipSlicePrune[initial.flip * 495 + initial.slice]);

    for (int phase1Depth = phase1LowerBound; phase1Depth <= 12; phase1Depth++) {
        ctx.phase1Path.clear();
        ctx.phase2Path.clear();
        ctx.solution.reset();
        ctx.foundPhase1Depth = -1;
        ctx.foundPhase2Depth = -1;

        if (SearchPhase1(
            tables,
            initial.cubie,
            initial.twist,
            initial.flip,
            initial.slice,
            phase1Depth,
            -1,
            ctx)) {
            ctx.foundPhase1Depth = phase1Depth;
            MoveSequence solution;
            solution.reserve(ctx.solution->size());
            for (int move18 : *ctx.solution) {
                solution.push_back(MoveFromMove18(move18));
            }
            statusMessage = "Solved using Kociemba two-phase search.";
            return solution;
        }
    }

    statusMessage = "Kociemba search exceeded configured depth limits.";
    return std::nullopt;
}

MoveSequence KociembaSolver::SolveByInverseHistory(const CubeState& state) {
    const MoveSequence& history = state.ReducedHistory();

    MoveSequence inverse;
    inverse.reserve(history.size());

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        inverse.push_back(InverseMove(*it));
    }

    return inverse;
}

} // namespace cube
