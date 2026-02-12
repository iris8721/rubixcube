#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "kociemba_solver.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <deque>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

constexpr Color COL_WHITE  { 255, 255, 255, 255 };
constexpr Color COL_YELLOW { 255, 213,   0, 255 };
constexpr Color COL_GREEN  {   0, 155,  72, 255 };
constexpr Color COL_BLUE   {   0,  69, 173, 255 };
constexpr Color COL_RED    { 185,   0,   0, 255 };
constexpr Color COL_ORANGE { 255,  89,   0, 255 };
constexpr Color COL_BLACK  {  20,  20,  20, 255 };

using Face = cube::Face;
constexpr Face UP = Face::U;
constexpr Face DOWN = Face::D;
constexpr Face FRONT = Face::F;
constexpr Face BACK = Face::B;
constexpr Face RIGHT = Face::R;
constexpr Face LEFT = Face::L;

constexpr std::array<Color, 6> PAINT_PALETTE{
    COL_WHITE, COL_YELLOW, COL_GREEN, COL_BLUE, COL_RED, COL_ORANGE
};

const float ANIM_SPEED = 5.0f;

struct Sticker {
    Vector3 cubiePos;
    Vector3 offset;
    Vector3 up;
    Vector3 right;
    Color color;
};

struct CubieData {
    Vector3 pos;
};

struct CubeModel {
    std::vector<Sticker> stickers;
    std::vector<CubieData> cubiePositions;
};

struct AppState {
    Font font = { 0 };
    bool customFontLoaded = false;
    bool isAnimating = false;
    bool showUI = true;
    bool showSolverDebug = true;
    bool paintMode = false;
    bool stepMode = false;
    bool showCubies = true;
    bool showStickers = true;
    float animAngle = 0.0f;
    Face animFace = UP;
    bool animCW = true;
    int animDir = 1;
    cube::Move animMove = cube::QuarterTurn(UP, true);
    CubeModel cube;
    cube::CubeState logicalState;
    std::unique_ptr<cube::ICubeSolver> solver;
    std::deque<cube::Move> moveQueue;
    std::vector<cube::Move> activeSolutionMoves;
    int completedSolutionSteps = 0;
    int currentStepQuarterTurnsRemaining = 0;
    bool solutionInProgress = false;
    std::string solverStatus = "Ready";
    std::string lastSolution;
    std::string validationStatus;
};

int NormalizeTurns(int turns) {
    int normalized = turns % 4;
    if (normalized < 0) normalized += 4;
    return normalized;
}

Face FaceFromIndex(int faceIndex) {
    switch (faceIndex) {
    case 0: return UP;
    case 1: return DOWN;
    case 2: return FRONT;
    case 3: return BACK;
    case 4: return RIGHT;
    case 5: return LEFT;
    }
    return UP;
}

struct CornerDefinition {
    int x;
    int y;
    int z;
    std::array<Face, 3> faces;
    const char* name;
};

struct EdgeDefinition {
    int x;
    int y;
    int z;
    std::array<Face, 2> faces;
    const char* name;
};

constexpr std::array<CornerDefinition, 8> CORNER_POSITIONS{ {
    { 1, 1, 1, { UP, RIGHT, FRONT }, "URF" },
    { -1, 1, 1, { UP, FRONT, LEFT }, "UFL" },
    { -1, 1, -1, { UP, LEFT, BACK }, "ULB" },
    { 1, 1, -1, { UP, BACK, RIGHT }, "UBR" },
    { 1, -1, 1, { DOWN, FRONT, RIGHT }, "DFR" },
    { -1, -1, 1, { DOWN, LEFT, FRONT }, "DLF" },
    { -1, -1, -1, { DOWN, BACK, LEFT }, "DBL" },
    { 1, -1, -1, { DOWN, RIGHT, BACK }, "DRB" }
} };

constexpr std::array<EdgeDefinition, 12> EDGE_POSITIONS{ {
    { 1, 1, 0, { UP, RIGHT }, "UR" },
    { 0, 1, 1, { UP, FRONT }, "UF" },
    { -1, 1, 0, { UP, LEFT }, "UL" },
    { 0, 1, -1, { UP, BACK }, "UB" },
    { 1, -1, 0, { DOWN, RIGHT }, "DR" },
    { 0, -1, 1, { DOWN, FRONT }, "DF" },
    { -1, -1, 0, { DOWN, LEFT }, "DL" },
    { 0, -1, -1, { DOWN, BACK }, "DB" },
    { 1, 0, 1, { FRONT, RIGHT }, "FR" },
    { -1, 0, 1, { FRONT, LEFT }, "FL" },
    { -1, 0, -1, { BACK, LEFT }, "BL" },
    { 1, 0, -1, { BACK, RIGHT }, "BR" }
} };

constexpr std::array<std::array<int, 3>, 8> CORNER_COLORS{ {
    { 0, 4, 2 }, // URF
    { 0, 2, 5 }, // UFL
    { 0, 5, 3 }, // ULB
    { 0, 3, 4 }, // UBR
    { 1, 2, 4 }, // DFR
    { 1, 5, 2 }, // DLF
    { 1, 3, 5 }, // DBL
    { 1, 4, 3 }  // DRB
} };

constexpr std::array<std::array<int, 2>, 12> EDGE_COLORS{ {
    { 0, 4 }, // UR
    { 0, 2 }, // UF
    { 0, 5 }, // UL
    { 0, 3 }, // UB
    { 1, 4 }, // DR
    { 1, 2 }, // DF
    { 1, 5 }, // DL
    { 1, 3 }, // DB
    { 2, 4 }, // FR
    { 2, 5 }, // FL
    { 3, 5 }, // BL
    { 3, 4 }  // BR
} };

bool SameColor(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int ColorToPaletteIndex(const Color& color) {
    for (int i = 0; i < static_cast<int>(PAINT_PALETTE.size()); i++) {
        if (SameColor(color, PAINT_PALETTE[i])) return i;
    }
    return -1;
}

Face FaceFromOffset(const Vector3& offset) {
    if (fabsf(offset.x) > fabsf(offset.y) && fabsf(offset.x) > fabsf(offset.z)) {
        return offset.x > 0.0f ? RIGHT : LEFT;
    }
    if (fabsf(offset.y) > fabsf(offset.x) && fabsf(offset.y) > fabsf(offset.z)) {
        return offset.y > 0.0f ? UP : DOWN;
    }
    return offset.z > 0.0f ? FRONT : BACK;
}

int GetFaceAxis(Face face) {
    if (face == UP || face == DOWN) return 1;
    if (face == FRONT || face == BACK) return 2;
    return 0;
}

Matrix GetRotationMatrix(int axis, float angle) {
    switch (axis) {
    case 0: return MatrixRotateX(angle);
    case 1: return MatrixRotateY(angle);
    case 2: return MatrixRotateZ(angle);
    }
    return MatrixIdentity();
}

float SnapToInt(float value) {
    return roundf(value);
}

float SnapToHalf(float value) {
    return roundf(value * 2.0f) * 0.5f;
}

void SnapCubiePosition(Vector3& pos) {
    pos.x = SnapToInt(pos.x);
    pos.y = SnapToInt(pos.y);
    pos.z = SnapToInt(pos.z);
}

void SnapStickerOrientation(Sticker& sticker) {
    sticker.offset.x = SnapToHalf(sticker.offset.x);
    sticker.offset.y = SnapToHalf(sticker.offset.y);
    sticker.offset.z = SnapToHalf(sticker.offset.z);

    sticker.up.x = SnapToInt(sticker.up.x);
    sticker.up.y = SnapToInt(sticker.up.y);
    sticker.up.z = SnapToInt(sticker.up.z);

    sticker.right.x = SnapToInt(sticker.right.x);
    sticker.right.y = SnapToInt(sticker.right.y);
    sticker.right.z = SnapToInt(sticker.right.z);
}

void AddSticker(CubeModel& cubeModel, Vector3 cubiePos, Vector3 offset, Vector3 up, Vector3 right, Color color) {
    Sticker sticker;
    sticker.cubiePos = cubiePos;
    sticker.offset = offset;
    sticker.up = up;
    sticker.right = right;
    sticker.color = color;
    cubeModel.stickers.push_back(sticker);
}

void InitCube(CubeModel& cubeModel) {
    cubeModel.stickers.clear();
    cubeModel.cubiePositions.clear();
    cubeModel.stickers.reserve(54);
    cubeModel.cubiePositions.reserve(27);

    float offset = 0.5f;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                CubieData cubie;
                cubie.pos = { (float)x, (float)y, (float)z };
                cubeModel.cubiePositions.push_back(cubie);
            }
        }
    }

    for (int x = -1; x <= 1; x++) {
        for (int z = -1; z <= 1; z++) {
            AddSticker(cubeModel, { (float)x, 1, (float)z }, { 0, offset, 0 }, { 0,0,-1 }, { 1,0,0 }, COL_WHITE);
            AddSticker(cubeModel, { (float)x, -1, (float)z }, { 0, -offset, 0 }, { 0,0,1 }, { 1,0,0 }, COL_YELLOW);
        }
    }
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            AddSticker(cubeModel, { (float)x, (float)y, 1 }, { 0, 0, offset }, { 0,1,0 }, { 1,0,0 }, COL_GREEN);
            AddSticker(cubeModel, { (float)x, (float)y, -1 }, { 0, 0, -offset }, { 0,1,0 }, { -1,0,0 }, COL_BLUE);
        }
    }
    for (int y = -1; y <= 1; y++) {
        for (int z = -1; z <= 1; z++) {
            AddSticker(cubeModel, { 1, (float)y, (float)z }, { offset, 0, 0 }, { 0,1,0 }, { 0,0,-1 }, COL_RED);
            AddSticker(cubeModel, { -1, (float)y, (float)z }, { -offset, 0, 0 }, { 0,1,0 }, { 0,0,1 }, COL_ORANGE);
        }
    }
}

void ResetCube(AppState& state) {
    InitCube(state.cube);
    state.logicalState.Reset();
    state.moveQueue.clear();
    state.activeSolutionMoves.clear();
    state.completedSolutionSteps = 0;
    state.currentStepQuarterTurnsRemaining = 0;
    state.solutionInProgress = false;
    state.isAnimating = false;
    state.animAngle = 0.0f;
    state.lastSolution.clear();
    state.validationStatus.clear();
    state.solverStatus = "Cube reset.";
}

bool CubieOnFace(Vector3 pos, Face face) {
    switch (face) {
    case Face::U: return pos.y > 0.5f;
    case Face::D: return pos.y < -0.5f;
    case Face::F: return pos.z > 0.5f;
    case Face::B: return pos.z < -0.5f;
    case Face::R: return pos.x > 0.5f;
    case Face::L: return pos.x < -0.5f;
    }
    return false;
}

float GetFaceDir(Face face, bool clockwise) {
    float direction = clockwise ? -1.0f : 1.0f;
    if (face == DOWN || face == BACK || face == LEFT) direction = -direction;
    return direction * PI / 2.0f;
}

void ApplyRotation(CubeModel& cubeModel, Face face, bool clockwise) {
    int axis = GetFaceAxis(face);
    float angle = GetFaceDir(face, clockwise);
    Matrix rotation = GetRotationMatrix(axis, angle);

    for (auto& sticker : cubeModel.stickers) {
        if (CubieOnFace(sticker.cubiePos, face)) {
            sticker.cubiePos = Vector3Transform(sticker.cubiePos, rotation);
            sticker.offset = Vector3Transform(sticker.offset, rotation);
            sticker.up = Vector3Transform(sticker.up, rotation);
            sticker.right = Vector3Transform(sticker.right, rotation);
            SnapCubiePosition(sticker.cubiePos);
            SnapStickerOrientation(sticker);
        }
    }

    for (auto& cubie : cubeModel.cubiePositions) {
        if (CubieOnFace(cubie.pos, face)) {
            cubie.pos = Vector3Transform(cubie.pos, rotation);
            SnapCubiePosition(cubie.pos);
        }
    }
}

void ApplyMoveInstant(CubeModel& cubeModel, const cube::Move& move) {
    int turns = NormalizeTurns(move.turns);
    if (turns == 0) return;

    if (turns == 3) {
        ApplyRotation(cubeModel, move.face, false);
        return;
    }

    for (int i = 0; i < turns; i++) {
        ApplyRotation(cubeModel, move.face, true);
    }
}

void StartRotation(AppState& state, Face face, bool clockwise) {
    if (state.isAnimating) return;
    state.isAnimating = true;
    state.animFace = face;
    state.animCW = clockwise;
    state.animMove = cube::QuarterTurn(face, clockwise);
    state.animDir = (GetFaceDir(face, clockwise) < 0.0f) ? -1 : 1;
    state.animAngle = 0.0f;
}

int AnimationQuarterTurnsForMove(const cube::Move& move) {
    int turns = NormalizeTurns(move.turns);
    if (turns == 0) return 0;
    return (turns == 2) ? 2 : 1;
}

void EnqueueAnimatedMove(AppState& state, const cube::Move& move) {
    int turns = NormalizeTurns(move.turns);
    if (turns == 0) return;

    if (turns == 3) {
        state.moveQueue.push_back(cube::QuarterTurn(move.face, false));
        return;
    }

    for (int i = 0; i < turns; i++) {
        state.moveQueue.push_back(cube::QuarterTurn(move.face, true));
    }
}

int TotalAnimationQuarterTurns(const cube::MoveSequence& moves) {
    int total = 0;
    for (const cube::Move& move : moves) {
        total += AnimationQuarterTurnsForMove(move);
    }
    return total;
}

void QueueNextSolutionStep(AppState& state) {
    if (!state.solutionInProgress) return;
    if (state.currentStepQuarterTurnsRemaining > 0) return;
    if (state.completedSolutionSteps >= static_cast<int>(state.activeSolutionMoves.size())) return;

    const cube::Move& step = state.activeSolutionMoves[state.completedSolutionSteps];
    int quarterTurns = AnimationQuarterTurnsForMove(step);
    if (quarterTurns <= 0) {
        state.completedSolutionSteps++;
        return;
    }

    state.currentStepQuarterTurnsRemaining = quarterTurns;
    EnqueueAnimatedMove(state, step);
}

void ScrambleCube(AppState& state) {
    state.moveQueue.clear();
    state.activeSolutionMoves.clear();
    state.completedSolutionSteps = 0;
    state.currentStepQuarterTurnsRemaining = 0;
    state.solutionInProgress = false;
    state.lastSolution.clear();
    state.validationStatus.clear();

    for (int i = 0; i < 25; i++) {
        Face face = FaceFromIndex(GetRandomValue(0, 5));
        bool clockwise = GetRandomValue(0, 1) == 1;
        cube::Move move = cube::QuarterTurn(face, clockwise);
        ApplyMoveInstant(state.cube, move);
        state.logicalState.ApplyMove(move);
    }

    state.solverStatus = "Cube scrambled.";
}

bool IsCenterSticker(const Sticker& sticker) {
    int x = static_cast<int>(roundf(sticker.cubiePos.x));
    int y = static_cast<int>(roundf(sticker.cubiePos.y));
    int z = static_cast<int>(roundf(sticker.cubiePos.z));
    int nonZero = (x != 0) + (y != 0) + (z != 0);
    return nonZero == 1;
}

int FindStickerColorAt(const CubeModel& cubeModel, int x, int y, int z, Face face) {
    for (const Sticker& sticker : cubeModel.stickers) {
        int sx = static_cast<int>(roundf(sticker.cubiePos.x));
        int sy = static_cast<int>(roundf(sticker.cubiePos.y));
        int sz = static_cast<int>(roundf(sticker.cubiePos.z));
        if (sx != x || sy != y || sz != z) continue;
        if (FaceFromOffset(sticker.offset) != face) continue;
        return ColorToPaletteIndex(sticker.color);
    }
    return -1;
}

bool BuildExplicitStateFromPaintedCube(
    const CubeModel& cubeModel,
    cube::KociembaSolver::ExplicitState& explicitState,
    std::string& message) {
    std::array<int, 6> colorCounts{};

    for (const Sticker& sticker : cubeModel.stickers) {
        int color = ColorToPaletteIndex(sticker.color);
        if (color < 0 || color >= 6) {
            message = "Unknown sticker color encountered.";
            return false;
        }
        colorCounts[color]++;
    }

    for (int i = 0; i < 6; i++) {
        if (colorCounts[i] != 9) {
            std::ostringstream text;
            text << "Color counts invalid (each color must appear 9 times).";
            message = text.str();
            return false;
        }
    }

    for (size_t cornerIndex = 0; cornerIndex < CORNER_POSITIONS.size(); cornerIndex++) {
        const CornerDefinition& def = CORNER_POSITIONS[cornerIndex];

        std::array<int, 3> observedColors{
            FindStickerColorAt(cubeModel, def.x, def.y, def.z, def.faces[0]),
            FindStickerColorAt(cubeModel, def.x, def.y, def.z, def.faces[1]),
            FindStickerColorAt(cubeModel, def.x, def.y, def.z, def.faces[2])
        };
        if (observedColors[0] < 0 || observedColors[1] < 0 || observedColors[2] < 0) {
            message = std::string("Missing corner stickers at ") + def.name + ".";
            return false;
        }

        int orientation = -1;
        for (int i = 0; i < 3; i++) {
            if (observedColors[i] == 0 || observedColors[i] == 1) {
                orientation = i;
                break;
            }
        }
        if (orientation < 0) {
            message = std::string("Corner at ") + def.name + " is missing U/D color.";
            return false;
        }

        int col1 = observedColors[(orientation + 1) % 3];
        int col2 = observedColors[(orientation + 2) % 3];
        int piece = -1;
        for (size_t pieceIndex = 0; pieceIndex < CORNER_COLORS.size(); pieceIndex++) {
            if (CORNER_COLORS[pieceIndex][1] == col1 && CORNER_COLORS[pieceIndex][2] == col2) {
                piece = static_cast<int>(pieceIndex);
                break;
            }
        }
        if (piece < 0) {
            message = std::string("Invalid corner orientation/color order at ") + def.name + ".";
            return false;
        }

        explicitState.cp[cornerIndex] = piece;
        explicitState.co[cornerIndex] = orientation % 3;
    }

    for (size_t edgeIndex = 0; edgeIndex < EDGE_POSITIONS.size(); edgeIndex++) {
        const EdgeDefinition& def = EDGE_POSITIONS[edgeIndex];

        std::array<int, 2> observedColors{
            FindStickerColorAt(cubeModel, def.x, def.y, def.z, def.faces[0]),
            FindStickerColorAt(cubeModel, def.x, def.y, def.z, def.faces[1])
        };
        if (observedColors[0] < 0 || observedColors[1] < 0) {
            message = std::string("Missing edge stickers at ") + def.name + ".";
            return false;
        }

        int piece = -1;
        int orientation = -1;
        for (size_t pieceIndex = 0; pieceIndex < EDGE_COLORS.size(); pieceIndex++) {
            if (observedColors[0] == EDGE_COLORS[pieceIndex][0] &&
                observedColors[1] == EDGE_COLORS[pieceIndex][1]) {
                piece = static_cast<int>(pieceIndex);
                orientation = 0;
                break;
            }
            if (observedColors[0] == EDGE_COLORS[pieceIndex][1] &&
                observedColors[1] == EDGE_COLORS[pieceIndex][0]) {
                piece = static_cast<int>(pieceIndex);
                orientation = 1;
                break;
            }
        }
        if (piece < 0 || orientation < 0) {
            message = std::string("Invalid edge orientation/color order at ") + def.name + ".";
            return false;
        }

        explicitState.ep[edgeIndex] = piece;
        explicitState.eo[edgeIndex] = orientation;
    }

    message = "Painted state captured.";
    return true;
}

bool ValidatePaintedCubeState(AppState& state, cube::KociembaSolver::ExplicitState& explicitState, std::string& message) {
    auto* kociemba = dynamic_cast<cube::KociembaSolver*>(state.solver.get());
    if (!kociemba) {
        message = "Solver does not support painted-state validation.";
        return false;
    }

    if (!BuildExplicitStateFromPaintedCube(state.cube, explicitState, message)) {
        return false;
    }

    cube::KociembaSolver::ValidationResult validation = kociemba->ValidateExplicitState(explicitState);
    if (!validation.valid) {
        message = validation.message;
        return false;
    }

    message = validation.message;
    return true;
}

void RequestSolve(AppState& state) {
    if (!state.solver) {
        state.solverStatus = "No solver available.";
        return;
    }

    cube::SolveResult result;
    if (state.paintMode) {
        auto* kociemba = dynamic_cast<cube::KociembaSolver*>(state.solver.get());
        if (!kociemba) {
            state.solverStatus = "Solver does not support paint-state solving.";
            return;
        }

        cube::KociembaSolver::ExplicitState explicitState;
        std::string validationMessage;
        if (!ValidatePaintedCubeState(state, explicitState, validationMessage)) {
            state.validationStatus = validationMessage;
            state.solverStatus = "Painted state is invalid.";
            state.moveQueue.clear();
            state.activeSolutionMoves.clear();
            state.completedSolutionSteps = 0;
            state.currentStepQuarterTurnsRemaining = 0;
            state.solutionInProgress = false;
            return;
        }

        state.validationStatus = validationMessage;
        result = kociemba->SolveExplicitState(explicitState);
    }
    else {
        // In normal play, trust the logical move-history state.
        result = state.solver->Solve(state.logicalState);
    }

    if (state.paintMode) state.paintMode = false;

    state.moveQueue.clear();
    state.activeSolutionMoves = result.moves;
    state.completedSolutionSteps = 0;
    state.currentStepQuarterTurnsRemaining = 0;
    state.solutionInProgress = !result.moves.empty();
    state.lastSolution = cube::MoveSequenceToString(result.moves);

    if (!result.message.empty()) {
        state.solverStatus = result.message;
    }
    else if (result.moves.empty()) {
        state.solverStatus = "No moves required.";
    }
    else {
        state.solverStatus = "Solution queued.";
    }

    if (state.stepMode && !result.moves.empty()) {
        state.solverStatus += " Step mode active: press N for next move.";
    }
    else if (!result.moves.empty()) {
        state.solverStatus += " (" + std::to_string(TotalAnimationQuarterTurns(result.moves)) + " turns)";
    }
}

struct StickerQuad {
    Vector3 v1;
    Vector3 v2;
    Vector3 v3;
    Vector3 v4;
};

StickerQuad BuildStickerQuad(const Sticker& sticker, Matrix transform) {
    Vector3 cubieWorld = Vector3Transform(sticker.cubiePos, transform);
    Vector3 offsetWorld = Vector3Transform(sticker.offset, transform);
    Vector3 pos = Vector3Add(cubieWorld, offsetWorld);

    Vector3 up = Vector3Transform(sticker.up, transform);
    Vector3 right = Vector3Transform(sticker.right, transform);

    float halfSize = 0.38f;
    StickerQuad quad;
    quad.v1 = Vector3Add(pos, Vector3Add(Vector3Scale(right, -halfSize), Vector3Scale(up, -halfSize)));
    quad.v2 = Vector3Add(pos, Vector3Add(Vector3Scale(right, halfSize), Vector3Scale(up, -halfSize)));
    quad.v3 = Vector3Add(pos, Vector3Add(Vector3Scale(right, halfSize), Vector3Scale(up, halfSize)));
    quad.v4 = Vector3Add(pos, Vector3Add(Vector3Scale(right, -halfSize), Vector3Scale(up, halfSize)));
    return quad;
}

void DrawSticker(const Sticker& sticker, Matrix transform) {
    StickerQuad quad = BuildStickerQuad(sticker, transform);
    DrawTriangle3D(quad.v1, quad.v2, quad.v3, sticker.color);
    DrawTriangle3D(quad.v1, quad.v3, quad.v4, sticker.color);
}

void DrawBlackCubie(Vector3 center, Matrix transform) {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(transform));
    DrawCube(center, 0.88f, 0.88f, 0.88f, COL_BLACK);
    rlPopMatrix();
}

std::optional<int> PickStickerIndex(const CubeModel& cubeModel, const Camera3D& camera, Vector2 mousePosition) {
    Ray ray = GetMouseRay(mousePosition, camera);
    float bestDistance = FLT_MAX;
    int bestIndex = -1;

    for (int i = 0; i < static_cast<int>(cubeModel.stickers.size()); i++) {
        const Sticker& sticker = cubeModel.stickers[i];
        StickerQuad quad = BuildStickerQuad(sticker, MatrixIdentity());

        RayCollision hitA = GetRayCollisionTriangle(ray, quad.v1, quad.v2, quad.v3);
        RayCollision hitB = GetRayCollisionTriangle(ray, quad.v1, quad.v3, quad.v4);

        if (hitA.hit && hitA.distance < bestDistance) {
            bestDistance = hitA.distance;
            bestIndex = i;
        }
        if (hitB.hit && hitB.distance < bestDistance) {
            bestDistance = hitB.distance;
            bestIndex = i;
        }
    }

    if (bestIndex < 0) return std::nullopt;
    return bestIndex;
}

void CycleStickerColor(Sticker& sticker, int delta) {
    int current = ColorToPaletteIndex(sticker.color);
    if (current < 0) current = 0;
    int next = (current + delta) % static_cast<int>(PAINT_PALETTE.size());
    if (next < 0) next += static_cast<int>(PAINT_PALETTE.size());
    sticker.color = PAINT_PALETTE[next];
}

void DrawMultilineText(const Font& font, const std::string& text, Vector2 position, float fontSize, float spacing, Color color, float lineSpacing) {
    size_t start = 0;
    int line = 0;

    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string lineText = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        DrawTextEx(font, lineText.c_str(), { position.x, position.y + line * lineSpacing }, fontSize, spacing, color);
        if (end == std::string::npos) break;
        start = end + 1;
        line++;
    }
}

struct ControlPanelLayout {
    Rectangle panel{};
    Rectangle solve{};
    Rectangle scramble{};
    Rectangle reset{};
    Rectangle paint{};
    Rectangle validate{};
    Rectangle step{};
    Rectangle next{};
    Rectangle cubies{};
    Rectangle stickers{};
    Rectangle debug{};
    Rectangle ui{};
    Rectangle u{};
    Rectangle up{};
    Rectangle d{};
    Rectangle dp{};
    Rectangle f{};
    Rectangle fp{};
    Rectangle b{};
    Rectangle bp{};
    Rectangle r{};
    Rectangle rp{};
    Rectangle l{};
    Rectangle lp{};
};

Rectangle GetDebugPanelRect() {
    const float width = 560.0f;
    return { static_cast<float>(GetScreenWidth()) - width - 20.0f, 20.0f, width, 132.0f };
}

Rectangle GetStepsPanelRect() {
    const float width = 420.0f;
    const float height = 590.0f;
    return { static_cast<float>(GetScreenWidth()) - width - 20.0f, 170.0f, width, height };
}

Rectangle GetShowUiButtonRect() {
    return { 20.0f, 20.0f, 120.0f, 32.0f };
}

ControlPanelLayout BuildControlPanelLayout() {
    ControlPanelLayout layout;
    layout.panel = { 20.0f, 20.0f, 380.0f, 370.0f };

    const float pad = 12.0f;
    const float gap = 8.0f;
    const float h = 30.0f;
    const float x = layout.panel.x + pad;
    const float innerW = layout.panel.width - pad * 2.0f;

    const float w3 = (innerW - gap * 2.0f) / 3.0f;
    const float w4 = (innerW - gap * 3.0f) / 4.0f;

    float y = layout.panel.y + 38.0f;
    layout.solve = { x + 0.0f * (w3 + gap), y, w3, h };
    layout.scramble = { x + 1.0f * (w3 + gap), y, w3, h };
    layout.reset = { x + 2.0f * (w3 + gap), y, w3, h };

    y += h + gap;
    layout.paint = { x + 0.0f * (w4 + gap), y, w4, h };
    layout.validate = { x + 1.0f * (w4 + gap), y, w4, h };
    layout.step = { x + 2.0f * (w4 + gap), y, w4, h };
    layout.next = { x + 3.0f * (w4 + gap), y, w4, h };

    y += h + gap;
    layout.cubies = { x + 0.0f * (w4 + gap), y, w4, h };
    layout.stickers = { x + 1.0f * (w4 + gap), y, w4, h };
    layout.debug = { x + 2.0f * (w4 + gap), y, w4, h };
    layout.ui = { x + 3.0f * (w4 + gap), y, w4, h };

    y += h + gap + 20.0f;
    layout.u = { x + 0.0f * (w4 + gap), y, w4, h };
    layout.up = { x + 1.0f * (w4 + gap), y, w4, h };
    layout.d = { x + 2.0f * (w4 + gap), y, w4, h };
    layout.dp = { x + 3.0f * (w4 + gap), y, w4, h };

    y += h + gap;
    layout.f = { x + 0.0f * (w4 + gap), y, w4, h };
    layout.fp = { x + 1.0f * (w4 + gap), y, w4, h };
    layout.b = { x + 2.0f * (w4 + gap), y, w4, h };
    layout.bp = { x + 3.0f * (w4 + gap), y, w4, h };

    y += h + gap;
    layout.r = { x + 0.0f * (w4 + gap), y, w4, h };
    layout.rp = { x + 1.0f * (w4 + gap), y, w4, h };
    layout.l = { x + 2.0f * (w4 + gap), y, w4, h };
    layout.lp = { x + 3.0f * (w4 + gap), y, w4, h };

    return layout;
}

void DrawActionButton(const Font& font, const Rectangle& button, const char* label, bool enabled, bool active) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), button);
    Color fill = enabled ? Color{ 32, 34, 40, 230 } : Color{ 28, 28, 30, 180 };
    Color border = enabled ? Color{ 90, 96, 108, 255 } : Color{ 60, 60, 68, 180 };
    Color text = enabled ? LIGHTGRAY : Color{ 130, 130, 135, 255 };

    if (active) {
        fill = Color{ 62, 92, 120, 240 };
        border = Color{ 120, 165, 205, 255 };
        text = WHITE;
    }
    if (enabled && hover) {
        fill = active ? Color{ 80, 112, 145, 245 } : Color{ 45, 48, 58, 235 };
        border = active ? Color{ 150, 196, 235, 255 } : Color{ 120, 130, 148, 255 };
    }

    DrawRectangleRec(button, fill);
    DrawRectangleLinesEx(button, 1.0f, border);

    Vector2 size = MeasureTextEx(font, label, 14, 1);
    DrawTextEx(
        font,
        label,
        { button.x + (button.width - size.x) * 0.5f, button.y + (button.height - size.y) * 0.5f },
        14,
        1,
        text);
}

void DrawControlPanel(
    const AppState& state,
    const ControlPanelLayout& controls,
    bool idle,
    bool manualInputAllowed,
    bool nextStepAllowed) {
    DrawRectangleRec(controls.panel, Color{ 10, 10, 10, 190 });
    DrawRectangleLinesEx(controls.panel, 1.0f, Color{ 70, 70, 70, 220 });

    DrawTextEx(state.font, "Controls", { controls.panel.x + 12, controls.panel.y + 10 }, 16, 1, WHITE);

    DrawActionButton(state.font, controls.solve, "Solve", idle, false);
    DrawActionButton(state.font, controls.scramble, "Scramble", manualInputAllowed, false);
    DrawActionButton(state.font, controls.reset, "Reset", manualInputAllowed, false);

    DrawActionButton(state.font, controls.paint, "Paint", idle, state.paintMode);
    DrawActionButton(state.font, controls.validate, "Validate", idle && state.paintMode, false);
    DrawActionButton(state.font, controls.step, "Step", true, state.stepMode);
    DrawActionButton(state.font, controls.next, "Next", nextStepAllowed, false);

    DrawActionButton(state.font, controls.cubies, "Cubies", true, state.showCubies);
    DrawActionButton(state.font, controls.stickers, "Stickers", true, state.showStickers);
    DrawActionButton(state.font, controls.debug, "Debug", true, state.showSolverDebug);
    DrawActionButton(state.font, controls.ui, "UI", true, state.showUI);

    DrawActionButton(state.font, controls.u, "U", manualInputAllowed, false);
    DrawActionButton(state.font, controls.up, "U'", manualInputAllowed, false);
    DrawActionButton(state.font, controls.d, "D", manualInputAllowed, false);
    DrawActionButton(state.font, controls.dp, "D'", manualInputAllowed, false);
    DrawActionButton(state.font, controls.f, "F", manualInputAllowed, false);
    DrawActionButton(state.font, controls.fp, "F'", manualInputAllowed, false);
    DrawActionButton(state.font, controls.b, "B", manualInputAllowed, false);
    DrawActionButton(state.font, controls.bp, "B'", manualInputAllowed, false);
    DrawActionButton(state.font, controls.r, "R", manualInputAllowed, false);
    DrawActionButton(state.font, controls.rp, "R'", manualInputAllowed, false);
    DrawActionButton(state.font, controls.l, "L", manualInputAllowed, false);
    DrawActionButton(state.font, controls.lp, "L'", manualInputAllowed, false);

    DrawTextEx(state.font, "View: drag mouse, scroll zoom", { controls.panel.x + 12, controls.panel.y + 340 }, 13, 1, GRAY);
}

void DrawSolutionStepsPanel(const AppState& state) {
    if (state.activeSolutionMoves.empty()) return;

    const Rectangle panel = GetStepsPanelRect();
    DrawRectangleRec(panel, Color{ 10, 10, 10, 190 });
    DrawRectangleLinesEx(panel, 1.0f, Color{ 70, 70, 70, 220 });

    std::ostringstream header;
    header << "Steps " << state.completedSolutionSteps << "/" << state.activeSolutionMoves.size();
    if (state.stepMode) header << " (Step Mode)";
    DrawTextEx(state.font, header.str().c_str(), { panel.x + 10, panel.y + 8 }, 16, 1, WHITE);

    int currentIndex = state.completedSolutionSteps;
    int start = std::max(0, currentIndex - 4);
    int end = std::min(static_cast<int>(state.activeSolutionMoves.size()), start + 25);
    float y = panel.y + 34;
    for (int i = start; i < end; i++) {
        std::string move = cube::MoveToNotation(state.activeSolutionMoves[i]);
        std::ostringstream line;
        line << (i + 1) << ". " << move;

        Color lineColor = (i < currentIndex) ? Color{ 120, 120, 120, 255 } : LIGHTGRAY;
        if (i == currentIndex && state.solutionInProgress) {
            lineColor = Color{ 255, 220, 120, 255 };
        }
        DrawTextEx(state.font, line.str().c_str(), { panel.x + 10, y }, 14, 1, lineColor);
        y += 20.0f;
    }
}

int main() {
    AppState state;

    InitWindow(1920, 1080, "rubixcube");
    SetTargetFPS(60);

    state.font = GetFontDefault();
    if (FileExists("Roboto-Medium.ttf")) {
        Font loaded = LoadFont("Roboto-Medium.ttf");
        if (loaded.texture.id > 0) {
            state.font = loaded;
            state.customFontLoaded = true;
        }
    }

    state.solver = std::make_unique<cube::KociembaSolver>();

    Camera3D camera = { 0 };
    camera.position = { 6, 6, 6 };
    camera.target = { 0, 0, 0 };
    camera.up = { 0, 1, 0 };
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;

    float camYaw = 45;
    float camPitch = 35;
    float camDist = 8;
    Vector2 lastMouse = GetMousePosition();

    ResetCube(state);
    state.solverStatus = state.solver->Name();

    while (!WindowShouldClose()) {
        Vector2 mouse = GetMousePosition();
        bool solveRequested = IsKeyPressed(KEY_X);
        bool validateRequested = IsKeyPressed(KEY_V);
        bool nextStepRequested = IsKeyPressed(KEY_N);
        bool scrambleRequested = IsKeyPressed(KEY_SPACE);
        bool resetRequested = IsKeyPressed(KEY_ENTER);
        bool toggleUiRequested = IsKeyPressed(KEY_H);
        bool toggleDebugRequested = IsKeyPressed(KEY_T);
        bool toggleStepRequested = IsKeyPressed(KEY_M);
        bool togglePaintRequested = IsKeyPressed(KEY_P);
        bool toggleCubiesRequested = IsKeyPressed(KEY_C);
        bool toggleStickersRequested = IsKeyPressed(KEY_S);

        bool uCwRequested = false;
        bool uCcwRequested = false;
        bool dCwRequested = false;
        bool dCcwRequested = false;
        bool fCwRequested = false;
        bool fCcwRequested = false;
        bool bCwRequested = false;
        bool bCcwRequested = false;
        bool rCwRequested = false;
        bool rCcwRequested = false;
        bool lCwRequested = false;
        bool lCcwRequested = false;

        ControlPanelLayout controls = BuildControlPanelLayout();
        Rectangle debugPanelRect = GetDebugPanelRect();
        Rectangle stepsPanelRect = GetStepsPanelRect();
        Rectangle showUiButtonRect = GetShowUiButtonRect();

        Vector2 delta = Vector2Subtract(mouse, lastMouse);

        bool mouseOverUiPanels = false;
        if (state.showUI) {
            if (CheckCollisionPointRec(mouse, controls.panel)) mouseOverUiPanels = true;
            if (state.showSolverDebug && CheckCollisionPointRec(mouse, debugPanelRect)) mouseOverUiPanels = true;
            if (!state.activeSolutionMoves.empty() && CheckCollisionPointRec(mouse, stepsPanelRect)) mouseOverUiPanels = true;
        }
        else if (CheckCollisionPointRec(mouse, showUiButtonRect)) {
            mouseOverUiPanels = true;
        }

        bool rotateView = (!state.paintMode && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ||
            (state.paintMode && IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
        if (mouseOverUiPanels) rotateView = false;
        if (rotateView) {
            camYaw -= delta.x * 0.5f;
            camPitch += delta.y * 0.5f;
            camPitch = Clamp(camPitch, -89, 89);
        }

        camDist -= GetMouseWheelMove() * 0.5f;
        camDist = Clamp(camDist, 4, 15);

        camera.position.x = camDist * cosf(camPitch * DEG2RAD) * sinf(camYaw * DEG2RAD);
        camera.position.y = camDist * sinf(camPitch * DEG2RAD);
        camera.position.z = camDist * cosf(camPitch * DEG2RAD) * cosf(camYaw * DEG2RAD);

        lastMouse = mouse;

        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (IsKeyPressed(KEY_U)) {
            if (shift) uCcwRequested = true;
            else uCwRequested = true;
        }
        if (IsKeyPressed(KEY_D)) {
            if (shift) dCcwRequested = true;
            else dCwRequested = true;
        }
        if (IsKeyPressed(KEY_F)) {
            if (shift) fCcwRequested = true;
            else fCwRequested = true;
        }
        if (IsKeyPressed(KEY_B)) {
            if (shift) bCcwRequested = true;
            else bCwRequested = true;
        }
        if (IsKeyPressed(KEY_R)) {
            if (shift) rCcwRequested = true;
            else rCwRequested = true;
        }
        if (IsKeyPressed(KEY_L)) {
            if (shift) lCcwRequested = true;
            else lCwRequested = true;
        }

        bool idle = !state.isAnimating && state.moveQueue.empty();
        bool manualInputAllowed = !state.paintMode && !state.solutionInProgress && !state.isAnimating && state.moveQueue.empty();
        bool nextStepAllowed = idle && state.stepMode && state.solutionInProgress &&
            state.currentStepQuarterTurnsRemaining == 0 &&
            state.completedSolutionSteps < static_cast<int>(state.activeSolutionMoves.size());

        bool buttonClick = state.showUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (buttonClick) {
            if (CheckCollisionPointRec(mouse, controls.solve) && idle) solveRequested = true;
            if (CheckCollisionPointRec(mouse, controls.scramble) && manualInputAllowed) scrambleRequested = true;
            if (CheckCollisionPointRec(mouse, controls.reset) && manualInputAllowed) resetRequested = true;
            if (CheckCollisionPointRec(mouse, controls.paint) && idle) togglePaintRequested = true;
            if (CheckCollisionPointRec(mouse, controls.validate) && idle && state.paintMode) validateRequested = true;
            if (CheckCollisionPointRec(mouse, controls.step)) toggleStepRequested = true;
            if (CheckCollisionPointRec(mouse, controls.next) && nextStepAllowed) nextStepRequested = true;
            if (CheckCollisionPointRec(mouse, controls.cubies)) toggleCubiesRequested = true;
            if (CheckCollisionPointRec(mouse, controls.stickers)) toggleStickersRequested = true;
            if (CheckCollisionPointRec(mouse, controls.debug)) toggleDebugRequested = true;
            if (CheckCollisionPointRec(mouse, controls.ui)) toggleUiRequested = true;

            if (manualInputAllowed) {
                if (CheckCollisionPointRec(mouse, controls.u)) uCwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.up)) uCcwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.d)) dCwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.dp)) dCcwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.f)) fCwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.fp)) fCcwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.b)) bCwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.bp)) bCcwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.r)) rCwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.rp)) rCcwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.l)) lCwRequested = true;
                if (CheckCollisionPointRec(mouse, controls.lp)) lCcwRequested = true;
            }
        }

        if (!state.showUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mouse, showUiButtonRect)) {
            toggleUiRequested = true;
        }

        if (toggleUiRequested) state.showUI = !state.showUI;
        if (toggleDebugRequested) state.showSolverDebug = !state.showSolverDebug;
        if (toggleStepRequested) state.stepMode = !state.stepMode;
        if (togglePaintRequested && idle) {
            state.paintMode = !state.paintMode;
            state.validationStatus = state.paintMode ? "Paint mode enabled." : "";
            if (state.paintMode) {
                state.activeSolutionMoves.clear();
                state.completedSolutionSteps = 0;
                state.currentStepQuarterTurnsRemaining = 0;
                state.solutionInProgress = false;
                state.moveQueue.clear();
                state.logicalState.Reset();
            }
        }
        if (toggleCubiesRequested) state.showCubies = !state.showCubies;
        if (toggleStickersRequested) state.showStickers = !state.showStickers;

        idle = !state.isAnimating && state.moveQueue.empty();
        if (idle && state.stepMode && nextStepRequested && state.solutionInProgress &&
            state.currentStepQuarterTurnsRemaining == 0 &&
            state.completedSolutionSteps < static_cast<int>(state.activeSolutionMoves.size())) {
            QueueNextSolutionStep(state);
        }

        if (idle && state.paintMode && validateRequested) {
            cube::KociembaSolver::ExplicitState explicitState;
            std::string validationMessage;
            if (ValidatePaintedCubeState(state, explicitState, validationMessage)) {
                state.validationStatus = validationMessage;
                state.solverStatus = "Painted state is valid.";
            }
            else {
                state.validationStatus = validationMessage;
                state.solverStatus = "Painted state is invalid.";
            }
        }

        if (idle && solveRequested) {
            RequestSolve(state);
        }

        if (idle && !state.stepMode && state.solutionInProgress &&
            state.currentStepQuarterTurnsRemaining == 0 &&
            state.completedSolutionSteps < static_cast<int>(state.activeSolutionMoves.size())) {
            QueueNextSolutionStep(state);
        }

        if (idle && state.paintMode && !mouseOverUiPanels) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                std::optional<int> picked = PickStickerIndex(state.cube, camera, mouse);
                if (picked) {
                    Sticker& sticker = state.cube.stickers[*picked];
                    if (IsCenterSticker(sticker)) {
                        state.validationStatus = "Center stickers are locked.";
                    }
                    else {
                        int deltaColor = shift ? -1 : 1;
                        CycleStickerColor(sticker, deltaColor);
                        state.validationStatus = "Sticker painted. Press V to validate.";
                        state.activeSolutionMoves.clear();
                        state.completedSolutionSteps = 0;
                        state.currentStepQuarterTurnsRemaining = 0;
                        state.solutionInProgress = false;
                        state.moveQueue.clear();
                    }
                }
            }
        }

        if (!state.isAnimating && !state.moveQueue.empty()) {
            cube::Move queuedMove = state.moveQueue.front();
            state.moveQueue.pop_front();
            StartRotation(state, queuedMove.face, cube::IsClockwiseQuarter(queuedMove));
        }

        manualInputAllowed = !state.paintMode && !state.solutionInProgress && !state.isAnimating && state.moveQueue.empty();
        if (manualInputAllowed) {
            if (uCwRequested) StartRotation(state, UP, true);
            if (uCcwRequested) StartRotation(state, UP, false);
            if (dCwRequested) StartRotation(state, DOWN, true);
            if (dCcwRequested) StartRotation(state, DOWN, false);
            if (fCwRequested) StartRotation(state, FRONT, true);
            if (fCcwRequested) StartRotation(state, FRONT, false);
            if (bCwRequested) StartRotation(state, BACK, true);
            if (bCcwRequested) StartRotation(state, BACK, false);
            if (rCwRequested) StartRotation(state, RIGHT, true);
            if (rCcwRequested) StartRotation(state, RIGHT, false);
            if (lCwRequested) StartRotation(state, LEFT, true);
            if (lCcwRequested) StartRotation(state, LEFT, false);
            if (scrambleRequested) ScrambleCube(state);
            if (resetRequested) ResetCube(state);
        }

        if (state.isAnimating) {
            state.animAngle += GetFrameTime() * ANIM_SPEED * 90.0f;
            if (state.animAngle >= 90) {
                ApplyRotation(state.cube, state.animFace, state.animCW);
                state.logicalState.ApplyMove(state.animMove);
                state.isAnimating = false;
                state.animAngle = 0;

                if (state.solutionInProgress && state.currentStepQuarterTurnsRemaining > 0) {
                    state.currentStepQuarterTurnsRemaining--;
                    if (state.currentStepQuarterTurnsRemaining == 0 &&
                        state.completedSolutionSteps < static_cast<int>(state.activeSolutionMoves.size())) {
                        state.completedSolutionSteps++;
                    }
                }

                if (state.solutionInProgress &&
                    state.currentStepQuarterTurnsRemaining == 0 &&
                    state.moveQueue.empty() &&
                    state.completedSolutionSteps >= static_cast<int>(state.activeSolutionMoves.size())) {
                    state.solutionInProgress = false;
                    state.logicalState.Reset();
                    state.solverStatus = "Cube solved.";
                    state.lastSolution.clear();
                }
            }
        }

        BeginDrawing();
        ClearBackground(Color{ 30, 30, 35, 255 });

        BeginMode3D(camera);

        Matrix animTransform = MatrixIdentity();
        if (state.isAnimating) {
            int axis = GetFaceAxis(state.animFace);
            float angle = state.animAngle * state.animDir * DEG2RAD;
            animTransform = GetRotationMatrix(axis, angle);
        }

        if (state.showCubies) {
            for (const auto& cubie : state.cube.cubiePositions) {
                Matrix transform = MatrixIdentity();
                if (state.isAnimating && CubieOnFace(cubie.pos, state.animFace)) {
                    transform = animTransform;
                }
                DrawBlackCubie(cubie.pos, transform);
            }
        }

        if (state.showStickers) {
            rlDisableBackfaceCulling();
            for (const auto& sticker : state.cube.stickers) {
                Matrix transform = MatrixIdentity();
                if (state.isAnimating && CubieOnFace(sticker.cubiePos, state.animFace)) {
                    transform = animTransform;
                }
                DrawSticker(sticker, transform);
            }
            rlEnableBackfaceCulling();
        }

        EndMode3D();

        if (state.showUI) {
            bool idleUi = !state.isAnimating && state.moveQueue.empty();
            bool manualUi = !state.paintMode && !state.solutionInProgress && !state.isAnimating && state.moveQueue.empty();
            bool nextUi = idleUi && state.stepMode && state.solutionInProgress &&
                state.currentStepQuarterTurnsRemaining == 0 &&
                state.completedSolutionSteps < static_cast<int>(state.activeSolutionMoves.size());

            DrawControlPanel(state, controls, idleUi, manualUi, nextUi);

            float statusY = controls.panel.y + controls.panel.height + 12.0f;
            DrawTextEx(state.font, state.solverStatus.c_str(), { 20, statusY }, 16, 1, LIGHTGRAY);
            if (!state.validationStatus.empty()) {
                DrawTextEx(state.font, state.validationStatus.c_str(), { 20, statusY + 20.0f }, 14, 1, Color{ 180, 210, 255, 255 });
            }
            if (state.paintMode) {
                DrawTextEx(state.font, "PAINT MODE: Left click sticker to cycle colors", { 20, statusY + 40.0f }, 15, 1, Color{ 255, 220, 120, 255 });
            }
            else if (state.stepMode) {
                DrawTextEx(state.font, "STEP MODE: Use Next button (or N).", { 20, statusY + 40.0f }, 15, 1, Color{ 255, 220, 120, 255 });
            }

            if (!state.lastSolution.empty()) {
                std::string preview = state.lastSolution;
                if (preview.size() > 84) {
                    preview = preview.substr(0, 84) + "...";
                }
                DrawTextEx(state.font, preview.c_str(), { 20, statusY + 62.0f }, 14, 1, GRAY);
            }

            if (state.showSolverDebug && state.solver) {
                const Rectangle debugPanel = debugPanelRect;
                DrawRectangleRec(debugPanel, Color{ 12, 12, 12, 190 });
                DrawRectangleLinesEx(debugPanel, 1.0f, Color{ 70, 70, 70, 220 });
                DrawMultilineText(
                    state.font,
                    state.solver->LastDebugInfo(),
                    { debugPanel.x + 10, debugPanel.y + 10 },
                    14,
                    1,
                    LIGHTGRAY,
                    18.0f);
            }

            DrawSolutionStepsPanel(state);

            DrawFPS(GetScreenWidth() - 110, GetScreenHeight() - 30);
        }
        else {
            DrawActionButton(state.font, showUiButtonRect, "Show UI", true, false);
            DrawFPS(GetScreenWidth() - 110, GetScreenHeight() - 30);
        }

        EndDrawing();
    }

    if (state.customFontLoaded) {
        UnloadFont(state.font);
    }
    CloseWindow();
    return 0;
}
