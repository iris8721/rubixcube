#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <vector>
#include <cmath>

#define COL_WHITE   Color{255, 255, 255, 255}
#define COL_YELLOW  Color{255, 213, 0, 255}
#define COL_GREEN   Color{0, 155, 72, 255}
#define COL_BLUE    Color{0, 69, 173, 255}
#define COL_RED     Color{185, 0, 0, 255}
#define COL_ORANGE  Color{255, 89, 0, 255}
#define COL_BLACK   Color{20, 20, 20, 255}

enum Face { UP, DOWN, FRONT, BACK, RIGHT, LEFT };

const float ANIM_SPEED = 5.0f;

Font font;
bool isAnimating = false;
bool showUI = true;
bool showCubies = true;
bool showStickers = true;
float animAngle = 0.0f;
int animFace = -1;
int animDir = 1;

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

std::vector<Sticker> stickers;
std::vector<CubieData> cubiePositions;

int GetFaceAxis(int face) {
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

void AddSticker(Vector3 cubiePos, Vector3 offset, Vector3 up, Vector3 right, Color color) {
    Sticker s;
    s.cubiePos = cubiePos;
    s.offset = offset;
    s.up = up;
    s.right = right;
    s.color = color;
    stickers.push_back(s);
}

void InitCube() {
    stickers.clear();
    cubiePositions.clear();

    float off = 0.5f;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                CubieData c;
                c.pos = { (float)x, (float)y, (float)z };
                cubiePositions.push_back(c);
            }
        }
    }

    for (int x = -1; x <= 1; x++) {
        for (int z = -1; z <= 1; z++) {
            AddSticker({ (float)x, 1, (float)z }, { 0, off, 0 }, { 0,0,-1 }, { 1,0,0 }, COL_WHITE);
            AddSticker({ (float)x, -1, (float)z }, { 0, -off, 0 }, { 0,0,1 }, { 1,0,0 }, COL_YELLOW);
        }
    }
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            AddSticker({ (float)x, (float)y, 1 }, { 0, 0, off }, { 0,1,0 }, { 1,0,0 }, COL_GREEN);
            AddSticker({ (float)x, (float)y, -1 }, { 0, 0, -off }, { 0,1,0 }, { -1,0,0 }, COL_BLUE);
        }
    }
    for (int y = -1; y <= 1; y++) {
        for (int z = -1; z <= 1; z++) {
            AddSticker({ 1, (float)y, (float)z }, { off, 0, 0 }, { 0,1,0 }, { 0,0,-1 }, COL_RED);
            AddSticker({ -1, (float)y, (float)z }, { -off, 0, 0 }, { 0,1,0 }, { 0,0,1 }, COL_ORANGE);
        }
    }
}

bool CubieOnFace(Vector3 pos, int face) {
    switch (face) {
    case UP:    return pos.y > 0.5f;
    case DOWN:  return pos.y < -0.5f;
    case FRONT: return pos.z > 0.5f;
    case BACK:  return pos.z < -0.5f;
    case RIGHT: return pos.x > 0.5f;
    case LEFT:  return pos.x < -0.5f;
    }
    return false;
}

float GetFaceDir(int face, bool cw) {
    float d = cw ? -1.0f : 1.0f;
    if (face == DOWN || face == BACK || face == LEFT) d = -d;
    return d * PI / 2.0f;
}

void ApplyRotation(int face, bool cw) {
    int axis = GetFaceAxis(face);
    float angle = GetFaceDir(face, cw);
    Matrix rot = GetRotationMatrix(axis, angle);

    for (auto& s : stickers) {
        if (CubieOnFace(s.cubiePos, face)) {
            s.cubiePos = Vector3Transform(s.cubiePos, rot);
            s.offset = Vector3Transform(s.offset, rot);
            s.up = Vector3Transform(s.up, rot);
            s.right = Vector3Transform(s.right, rot);
            s.cubiePos.x = roundf(s.cubiePos.x);
            s.cubiePos.y = roundf(s.cubiePos.y);
            s.cubiePos.z = roundf(s.cubiePos.z);
        }
    }

    for (auto& c : cubiePositions) {
        if (CubieOnFace(c.pos, face)) {
            c.pos = Vector3Transform(c.pos, rot);
            c.pos.x = roundf(c.pos.x);
            c.pos.y = roundf(c.pos.y);
            c.pos.z = roundf(c.pos.z);
        }
    }
}

void StartRotation(int face, bool cw) {
    if (isAnimating) return;
    isAnimating = true;
    animFace = face;
    animDir = cw ? -1 : 1;
    if (face == DOWN || face == BACK || face == LEFT) animDir = -animDir;
    animAngle = 0.0f;
}

void ScrambleCube() {
    for (int i = 0; i < 25; i++) {
        ApplyRotation(GetRandomValue(0, 5), GetRandomValue(0, 1));
    }
}

void DrawSticker(const Sticker& s, Matrix transform) {
    Vector3 cubieWorld = Vector3Transform(s.cubiePos, transform);
    Vector3 offsetWorld = Vector3Transform(s.offset, transform);
    Vector3 pos = Vector3Add(cubieWorld, offsetWorld);

    Vector3 up = Vector3Transform(s.up, transform);
    Vector3 right = Vector3Transform(s.right, transform);

    float hs = 0.38f;
    Vector3 v1 = Vector3Add(pos, Vector3Add(Vector3Scale(right, -hs), Vector3Scale(up, -hs)));
    Vector3 v2 = Vector3Add(pos, Vector3Add(Vector3Scale(right, hs), Vector3Scale(up, -hs)));
    Vector3 v3 = Vector3Add(pos, Vector3Add(Vector3Scale(right, hs), Vector3Scale(up, hs)));
    Vector3 v4 = Vector3Add(pos, Vector3Add(Vector3Scale(right, -hs), Vector3Scale(up, hs)));

    rlDisableBackfaceCulling();
    DrawTriangle3D(v1, v2, v3, s.color);
    DrawTriangle3D(v1, v3, v4, s.color);
    rlEnableBackfaceCulling();
}

void DrawBlackCubie(Vector3 center, Matrix transform) {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(transform));
    DrawCube(center, 0.88f, 0.88f, 0.88f, COL_BLACK);
    rlPopMatrix();
}

int main() {
    InitWindow(1200, 800, "Rubik's Cube");
    SetTargetFPS(60);

    font = LoadFont("Roboto-Medium.ttf");

    Camera3D camera = { 0 };
    camera.position = { 6, 6, 6 };
    camera.target = { 0, 0, 0 };
    camera.up = { 0, 1, 0 };
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;

    float camYaw = 45, camPitch = 35, camDist = 8;
    Vector2 lastMouse = GetMousePosition();

    InitCube();

    while (!WindowShouldClose()) {
        Vector2 mouse = GetMousePosition();
        Vector2 delta = Vector2Subtract(mouse, lastMouse);

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
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
        if (IsKeyPressed(KEY_H)) showUI = !showUI;
        if (IsKeyPressed(KEY_C)) showCubies = !showCubies;
        if (IsKeyPressed(KEY_S)) showStickers = !showStickers;
        if (!isAnimating) {
            if (IsKeyPressed(KEY_U)) StartRotation(UP, !shift);
            if (IsKeyPressed(KEY_D)) StartRotation(DOWN, !shift);
            if (IsKeyPressed(KEY_F)) StartRotation(FRONT, !shift);
            if (IsKeyPressed(KEY_B)) StartRotation(BACK, !shift);
            if (IsKeyPressed(KEY_R)) StartRotation(RIGHT, !shift);
            if (IsKeyPressed(KEY_L)) StartRotation(LEFT, !shift);
            if (IsKeyPressed(KEY_SPACE)) ScrambleCube();
            if (IsKeyPressed(KEY_ENTER)) InitCube();
        }

        if (isAnimating) {
            animAngle += GetFrameTime() * ANIM_SPEED * 90.0f;
            if (animAngle >= 90) {
                ApplyRotation(animFace, animDir == -1);
                isAnimating = false;
                animAngle = 0;
            }
        }

        BeginDrawing();
        ClearBackground(Color{ 30, 30, 35, 255 });

        BeginMode3D(camera);

        Matrix animTransform = MatrixIdentity();
        if (isAnimating) {
            int axis = GetFaceAxis(animFace);
            float angle = animAngle * animDir * DEG2RAD;
            animTransform = GetRotationMatrix(axis, angle);
        }

        if (showCubies) {
            for (const auto& c : cubiePositions) {
                Matrix transform = MatrixIdentity();

                if (isAnimating && CubieOnFace(c.pos, animFace)) {
                    transform = animTransform;
                }

                DrawBlackCubie(c.pos, transform);
            }
        }

        if (showStickers) {
            for (const auto& s : stickers) {
                Matrix transform = MatrixIdentity();
                if (isAnimating && CubieOnFace(s.cubiePos, animFace)) {
                    transform = animTransform;
                }
                DrawSticker(s, transform);
            }
        }

        EndMode3D();

        if (showUI) {
            DrawTextEx(font, "RUBIK'S CUBE", { 20, 20 }, 30, 1, WHITE);
            DrawTextEx(font, "Mouse Drag - Rotate view", { 20, 60 }, 16, 1, GRAY);
            DrawTextEx(font, "Scroll - Zoom", { 20, 80 }, 16, 1, GRAY);
            DrawTextEx(font, "U/D/L/R/F/B - Rotate faces", { 20, 100 }, 16, 1, GRAY);
            DrawTextEx(font, "SHIFT + key - Reverse", { 20, 120 }, 16, 1, GRAY);
            DrawTextEx(font, "SPACE - Scramble | ENTER - Reset", { 20, 140 }, 16, 1, GRAY);
            DrawTextEx(font, "H - Toggle UI | C - Toggle Cubies | S - Toggle Stickers", { 20, 160 }, 16, 1, GRAY);
            DrawFPS(1110, 770);
        }

        EndDrawing();
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}