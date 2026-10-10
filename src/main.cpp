#include <raylib.h>
#include <math.h>
#include <stdio.h>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

/*
FLAPPY PLANE v3 — NEON EDITION
Sund Origin Studio, 2026. Built on the original Flappy Plane (Programming 1, 2025).

Same framework as the original: raylib, one main.cpp, the same game states
(LOGO -> GAMESTART -> GAMEPLAY -> GAMEOVER / PAUSED), the same player physics
(gravity, jump force, hold-to-sustain, terminal velocity) and the same controls.

What's new in v3:
- Neon / grid visual style drawn in code (no image files needed)
- Light trail behind the plane, derez particles on crash
- Synthesized sound effects and two synthwave music loops
- Difficulty select (Easy / Normal / Hard): changes the gap between towers,
  the distance between towers, and the scroll speed
- Movement is time-based, so the game runs the same at 60, 120 or 144 FPS
- Separate high score per difficulty (saved in the browser)

Controls:
SPACE  flap (hold to climb harder)     X  swap paper plane / jet plane
1 2 3  or LEFT/RIGHT  pick difficulty  P  pause      M  mute
*/

//Gamestates------------------------------------------------------
typedef enum GameScreen { LOGO = 0, GAMESTART, GAMEPLAY, GAMEOVER, PAUSED } GameScreen;
GameScreen currentScreen = LOGO;
double screenTimer = 0.0;

// Window variables-----------------------------------------------
const float windowWidth = 1280;
const float windowHeight = 1024;
const float horizonY = 690.f;    // where the neon floor grid starts
const float floorY = 1024.f;     // touching the bottom ends the run (same as original)

// Player physics (unchanged from the original)--------------------
float playerXPosition = windowWidth / 2 - 300;
float playerYPosition = windowHeight / 2;
float playerSpeedY = 0;
const float playerRadius = 11.f;
const float gravity = 5000.f;
const float terminalVelocity = 1000.f;
const float jumpForce = -650.f;
const float jumpSustainForce = 6000.f;
const float maxUpwardSpeed = -500.f;

// Planes -----------------------------------------------------------
const int TotalPlanes = 2;
int planeChoice = 0;            // 0 = paper plane, 1 = jet plane
const char* planeNames[TotalPlanes] = { "PAPER PLANE", "JET PLANE" };
const float planeSpeedFactor[TotalPlanes] = { 1.0f, 1.45f };

// Difficulty -------------------------------------------------------
const int TotalDifficulties = 3;
int difficulty = 1;
const char* difficultyNames[TotalDifficulties] = { "EASY", "NORMAL", "HARD" };
const float diffGap[TotalDifficulties]     = { 300.f, 235.f, 185.f };  // opening between top and bottom tower
const float diffSpacing[TotalDifficulties] = { 760.f, 620.f, 520.f };  // distance between towers
const float diffSpeed[TotalDifficulties]   = { 260.f, 320.f, 380.f };  // pixels per second (paper plane)

float objectMovementSpeed = 0.f; // pixels per second, set when a run starts

// Scoring ----------------------------------------------------------
int Score = 0;
int Highscore[TotalDifficulties] = { 0, 0, 0 };
bool newBest = false;

// Colours ----------------------------------------------------------
const Color bgTop    = { 4, 5, 16, 255 };
const Color bgBottom = { 22, 8, 40, 255 };
const Color cyan     = { 60, 230, 255, 255 };
const Color magenta  = { 255, 60, 200, 255 };
const Color orange   = { 255, 150, 50, 255 };
const Color violet   = { 150, 90, 255, 255 };
const Color white    = { 235, 245, 255, 255 };

Color PlaneColor(void) { return planeChoice == 0 ? cyan : orange; }
Color AccentColor(void) { return planeChoice == 0 ? magenta : violet; }
Color WithAlpha(Color c, float a) { c.a = (unsigned char)(a < 0 ? 0 : a > 1 ? 255 : a * 255); return c; }

// Audio ------------------------------------------------------------
#define FlapSoundsMax 8
Sound flapSound, thrustSound, scoreSound, crashSound, selectSound;
Sound flapSounds[FlapSoundsMax];
Sound thrustSounds[FlapSoundsMax];
int currentSound = 0;
Music musicPaper, musicJet;
Music* chosenMusic = &musicPaper;
bool muted = false;

// Scrolling background ---------------------------------------------
float gridScroll = 0.f;
float skylineScroll = 0.f;

struct Star { Vector2 pos; float speed; float size; };
const int maxStars = 90;
Star stars[maxStars];

// Light trail behind the plane -------------------------------------
const int trailLength = 46;
Vector2 trail[trailLength];
int trailCount = 0;

// Crash particles --------------------------------------------------
struct Particle { Vector2 pos, vel; float life; Color color; float size; };
const int maxParticles = 90;
Particle particles[maxParticles];

// Score popups -----------------------------------------------------
struct Popup { Vector2 pos; float life; };
const int maxPopups = 6;
Popup popups[maxPopups];

// Hindrances (towers) ----------------------------------------------
struct Hindrance {
	Rectangle topRect;
	Rectangle bottomRect;
	bool scored;
	float seed;
};
const int maxHindrances = 4;
Hindrance hindrances[maxHindrances];
const float towerWidth = 120.f;

// Persistent high score (browser localStorage on web) ------------
void LoadHighscores(void)
{
#if defined(PLATFORM_WEB)
	for (int i = 0; i < TotalDifficulties; i++)
		Highscore[i] = EM_ASM_INT({ try { return parseInt(localStorage.getItem('fpv3-best-' + $0)) || 0; } catch (e) { return 0; } }, i);
#endif
}
void SaveHighscore(int d)
{
#if defined(PLATFORM_WEB)
	EM_ASM({ try { localStorage.setItem('fpv3-best-' + $0, $1); } catch (e) {} }, d, Highscore[d]);
#endif
}

// Helpers ------------------------------------------------------------
void PlaySfx(Sound s) { if (!muted) PlaySound(s); }

void DrawCenteredText(const char* text, int posY, int fontSize, Color color)
{
	int textWidth = MeasureText(text, fontSize);
	DrawText(text, (int)(windowWidth / 2 - textWidth / 2), posY, fontSize, color);
}

// Neon text: soft coloured glow behind crisp text
void DrawNeonText(const char* text, int posY, int fontSize, Color color)
{
	int textWidth = MeasureText(text, fontSize);
	int x = (int)(windowWidth / 2 - textWidth / 2);
	BeginBlendMode(BLEND_ADDITIVE);
	for (int i = 1; i <= 3; i++)
	{
		Color g = WithAlpha(color, 0.12f);
		DrawText(text, x - i, posY, fontSize, g);
		DrawText(text, x + i, posY, fontSize, g);
		DrawText(text, x, posY - i, fontSize, g);
		DrawText(text, x, posY + i, fontSize, g);
	}
	EndBlendMode();
	DrawText(text, x, posY, fontSize, color);
}

// Glowing line: wide faint passes plus a thin bright core
void DrawGlowLine(Vector2 a, Vector2 b, float thick, Color c)
{
	BeginBlendMode(BLEND_ADDITIVE);
	DrawLineEx(a, b, thick * 5, WithAlpha(c, 0.07f));
	DrawLineEx(a, b, thick * 2.5f, WithAlpha(c, 0.18f));
	EndBlendMode();
	DrawLineEx(a, b, thick, c);
}

void DrawGlowRectLines(Rectangle r, float thick, Color c)
{
	DrawGlowLine({ r.x, r.y }, { r.x + r.width, r.y }, thick, c);
	DrawGlowLine({ r.x + r.width, r.y }, { r.x + r.width, r.y + r.height }, thick, c);
	DrawGlowLine({ r.x + r.width, r.y + r.height }, { r.x, r.y + r.height }, thick, c);
	DrawGlowLine({ r.x, r.y + r.height }, { r.x, r.y }, thick, c);
}

// Stars ----------------------------------------------------------
void InitStars(void)
{
	for (int i = 0; i < maxStars; i++)
	{
		stars[i].pos = { (float)GetRandomValue(0, (int)windowWidth), (float)GetRandomValue(0, (int)horizonY - 120) };
		stars[i].speed = GetRandomValue(4, 30);
		stars[i].size = GetRandomValue(1, 3) * 0.7f;
	}
}

void UpdateStars(float dt, float speedScale)
{
	for (int i = 0; i < maxStars; i++)
	{
		stars[i].pos.x -= stars[i].speed * speedScale * dt;
		if (stars[i].pos.x < 0) { stars[i].pos.x += windowWidth; stars[i].pos.y = GetRandomValue(0, (int)horizonY - 120); }
	}
}

// Hindrances -------------------------------------------------------
float lastGapTop = 350.f;
const float maxGapShift = 330.f; // keeps every gap reachable from the previous one

void RandomizeGap(Hindrance& h)
{
	float gap = diffGap[difficulty];
	float margin = 90.f;
	float lo = margin, hi = floorY - margin - gap - 40;
	if (lo < lastGapTop - maxGapShift) lo = lastGapTop - maxGapShift;
	if (hi > lastGapTop + maxGapShift) hi = lastGapTop + maxGapShift;
	float gapTop = GetRandomValue((int)lo, (int)hi);
	lastGapTop = gapTop;
	h.topRect = { h.topRect.x, 0.f, towerWidth, gapTop };
	h.bottomRect = { h.topRect.x, gapTop + gap, towerWidth, floorY - (gapTop + gap) };
	h.scored = false;
	h.seed = (float)GetRandomValue(0, 1000);
}

void InitHindrances(void)
{
	lastGapTop = 350.f;
	for (int i = 0; i < maxHindrances; i++)
	{
		hindrances[i].topRect.x = windowWidth + 250 + i * diffSpacing[difficulty];
		RandomizeGap(hindrances[i]);
	}
}

void UpdateHindrance(Hindrance& h, float dt)
{
	h.topRect.x -= objectMovementSpeed * dt;
	h.bottomRect.x = h.topRect.x;
	if (h.topRect.x + towerWidth < 0)
	{
		// Move behind the furthest tower
		float furthest = 0;
		for (int i = 0; i < maxHindrances; i++) if (hindrances[i].topRect.x > furthest) furthest = hindrances[i].topRect.x;
		h.topRect.x = furthest + diffSpacing[difficulty];
		RandomizeGap(h);
	}
}

void DrawTower(Rectangle r, bool isTop, float seed, Color edge, Color accent)
{
	// Body
	DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, { 10, 14, 34, 255 }, { 6, 8, 20, 255 });
	// Circuit lines inside the tower
	for (int k = 0; k < 3; k++)
	{
		float x = r.x + 22 + k * 38;
		DrawLineEx({ x, r.y }, { x, r.y + r.height }, 1.f, WithAlpha(accent, 0.18f));
	}
	for (float y = isTop ? r.y + r.height - 60 : r.y + 60; isTop ? y > r.y : y < r.y + r.height; y += isTop ? -90 : 90)
	{
		float wobble = sinf(seed + y * 0.05f) * 30;
		DrawLineEx({ r.x + 10, y }, { r.x + r.width / 2 + wobble, y }, 1.f, WithAlpha(accent, 0.25f));
		DrawCircleV({ r.x + r.width / 2 + wobble, y }, 3, WithAlpha(accent, 0.6f));
	}
	// Outline and bright cap at the gap edge
	DrawGlowRectLines(r, 2.f, WithAlpha(edge, 0.85f));
	float capY = isTop ? r.y + r.height : r.y;
	DrawGlowLine({ r.x - 8, capY }, { r.x + r.width + 8, capY }, 4.f, white);
}

void DrawHindrance(const Hindrance& h)
{
	DrawTower(h.topRect, true, h.seed, PlaneColor(), AccentColor());
	DrawTower(h.bottomRect, false, h.seed + 50, PlaneColor(), AccentColor());
}

// Background --------------------------------------------------------
void DrawBackground(void)
{
	DrawRectangleGradientV(0, 0, (int)windowWidth, (int)horizonY, bgTop, bgBottom);

	// Stars
	for (int i = 0; i < maxStars; i++) DrawCircleV(stars[i].pos, stars[i].size, WithAlpha(white, 0.35f + stars[i].size * 0.2f));

	// Neon sun with stripes
	Vector2 sunC = { windowWidth * 0.72f, horizonY - 30 };
	BeginBlendMode(BLEND_ADDITIVE);
	DrawCircleV(sunC, 210, WithAlpha(AccentColor(), 0.06f));
	DrawCircleV(sunC, 170, WithAlpha(AccentColor(), 0.08f));
	EndBlendMode();
	DrawCircleSector(sunC, 140, 180, 360, 64, WithAlpha(AccentColor(), 0.85f));
	for (int s = 0; s < 6; s++)
	{
		float y = sunC.y - 20 - s * 22;
		DrawRectangle((int)(sunC.x - 150), (int)y, 300, 4 + s, bgBottom);
	}

	// Distant skyline silhouette (parallax), wraps every 1280 px
	for (int rep = 0; rep < 2; rep++)
	{
		float base = -fmodf(skylineScroll, windowWidth) + rep * windowWidth;
		for (int b = 0; b < 26; b++)
		{
			float w = 30 + (b * 37 % 50);
			float h = 50 + (b * 71 % 170);
			float x = base + b * 50.f;
			Rectangle rb = { x, horizonY - h, w, h };
			DrawRectangleRec(rb, { 8, 6, 24, 255 });
			DrawRectangleLinesEx(rb, 1, WithAlpha(PlaneColor(), 0.35f));
			if (b % 3 == 0) DrawRectangle((int)(x + w / 2 - 2), (int)(horizonY - h + 12), 4, 4, WithAlpha(PlaneColor(), 0.7f));
		}
	}

	// Floor
	DrawRectangleGradientV(0, (int)horizonY, (int)windowWidth, (int)(windowHeight - horizonY), { 10, 2, 26, 255 }, { 2, 2, 8, 255 });
	// Horizon line
	DrawGlowLine({ 0, horizonY }, { windowWidth, horizonY }, 2.f, PlaneColor());
	// Perspective grid: horizontal lines get closer together near the horizon
	for (int i = 1; i < 14; i++)
	{
		float p = fmodf(i + gridScroll * 0.02f, 14.f) / 14.f;
		float y = horizonY + (windowHeight - horizonY) * p * p;
		DrawLineEx({ 0, y }, { windowWidth, y }, 1.5f, WithAlpha(AccentColor(), 0.15f + p * 0.5f));
	}
	// Vertical lines converge to a vanishing point
	Vector2 vp = { windowWidth / 2, horizonY - 220 };
	for (int i = -16; i <= 16; i++)
	{
		float xb = windowWidth / 2 + i * 110 - fmodf(gridScroll * 0.6f, 110.f);
		Vector2 bottom = { xb, windowHeight };
		float t = (horizonY - vp.y) / (bottom.y - vp.y);
		Vector2 top = { vp.x + (bottom.x - vp.x) * t, horizonY };
		DrawLineEx(top, bottom, 1.5f, WithAlpha(AccentColor(), 0.35f));
	}
}

// Player ------------------------------------------------------------
void ResetTrail(void) { trailCount = 0; }

void UpdateTrail(float dt)
{
	// Existing points drift left with the world, newest point is the plane's tail
	for (int i = 0; i < trailCount; i++) trail[i].x -= objectMovementSpeed * dt;
	if (trailCount < trailLength) trailCount++;
	for (int i = trailCount - 1; i > 0; i--) trail[i] = trail[i - 1];
	trail[0] = { playerXPosition - 18, playerYPosition };
}

void DrawTrail(void)
{
	// Two passes: soft additive glow first, then the bright core (fewer blend-mode switches)
	Color c = PlaneColor();
	BeginBlendMode(BLEND_ADDITIVE);
	for (int i = 1; i < trailCount; i++)
	{
		float a = 1.f - (float)i / trailCount;
		DrawLineEx(trail[i - 1], trail[i], 12 * a, WithAlpha(c, 0.10f * a));
	}
	EndBlendMode();
	for (int i = 1; i < trailCount; i++)
	{
		float a = 1.f - (float)i / trailCount;
		DrawLineEx(trail[i - 1], trail[i], 3.5f * a + 0.5f, WithAlpha(c, 0.9f * a));
	}
}

void DrawPlayer(void)
{
	// Same rotation rule as the original: tilt with vertical speed
	float rotation = playerSpeedY * 0.025f;
	if (rotation > 45.0f) rotation = 60.0f;
	if (rotation < -30.0f) rotation = -30.0f;
	float r = rotation * DEG2RAD;
	float cs = cosf(r), sn = sinf(r);
	auto P = [&](float x, float y) { return Vector2{ playerXPosition + x * cs - y * sn, playerYPosition + x * sn + y * cs }; };

	Color c = PlaneColor();
	if (planeChoice == 0)
	{
		// Paper plane: two folded wings
		Vector2 nose = P(30, 0), tail = P(-26, 0), wingTop = P(-30, -16), wingBot = P(-22, 13), fold = P(-12, 3);
		DrawTriangle(nose, wingTop, fold, WithAlpha(c, 0.35f));
		DrawTriangle(nose, fold, wingBot, WithAlpha(c, 0.2f));
		DrawGlowLine(nose, wingTop, 2.f, c); DrawGlowLine(wingTop, fold, 2.f, c);
		DrawGlowLine(fold, wingBot, 2.f, c); DrawGlowLine(wingBot, nose, 2.f, c);
		DrawGlowLine(nose, tail, 1.5f, white);
	}
	else
	{
		// Jet: arrow body, swept wings, glowing engine
		Vector2 nose = P(34, 0), back = P(-24, 0), wingT = P(-14, -22), wingB = P(-14, 22), body1 = P(4, -6), body2 = P(4, 6), fin = P(-26, -12);
		DrawTriangle(nose, body1, back, WithAlpha(c, 0.3f));
		DrawTriangle(nose, back, body2, WithAlpha(c, 0.3f));
		DrawGlowLine(nose, wingT, 2.f, c); DrawGlowLine(wingT, back, 2.f, c);
		DrawGlowLine(back, wingB, 2.f, c); DrawGlowLine(wingB, nose, 2.f, c);
		DrawGlowLine(back, fin, 2.f, c);
		float flicker = 0.6f + 0.4f * sinf((float)GetTime() * 40);
		BeginBlendMode(BLEND_ADDITIVE);
		DrawCircleV(P(-30, 0), 12 * flicker, WithAlpha(orange, 0.35f));
		EndBlendMode();
		DrawCircleV(P(-28, 0), 4, white);
	}
}

// Particles & popups -------------------------------------------------
void SpawnDerez(void)
{
	for (int i = 0; i < maxParticles; i++)
	{
		float ang = GetRandomValue(0, 360) * DEG2RAD;
		float spd = GetRandomValue(80, 520);
		particles[i] = { { playerXPosition, playerYPosition }, { cosf(ang) * spd, sinf(ang) * spd - 120 },
			GetRandomValue(50, 120) / 100.f, i % 3 == 0 ? white : (i % 3 == 1 ? PlaneColor() : AccentColor()), (float)GetRandomValue(2, 7) };
	}
}

void UpdateParticles(float dt)
{
	for (int i = 0; i < maxParticles; i++)
	{
		if (particles[i].life <= 0) continue;
		particles[i].life -= dt;
		particles[i].vel.y += 600 * dt;
		particles[i].pos.x += particles[i].vel.x * dt;
		particles[i].pos.y += particles[i].vel.y * dt;
	}
	for (int i = 0; i < maxPopups; i++)
	{
		if (popups[i].life <= 0) continue;
		popups[i].life -= dt;
		popups[i].pos.y -= 70 * dt;
	}
}

void DrawParticles(void)
{
	for (int i = 0; i < maxParticles; i++)
	{
		if (particles[i].life <= 0) continue;
		float a = particles[i].life > 1 ? 1 : particles[i].life;
		DrawRectangleV(particles[i].pos, { particles[i].size, particles[i].size }, WithAlpha(particles[i].color, a));
	}
	for (int i = 0; i < maxPopups; i++)
	{
		if (popups[i].life <= 0) continue;
		DrawText("+1", (int)popups[i].pos.x, (int)popups[i].pos.y, 30, WithAlpha(white, popups[i].life));
	}
}

void SpawnPopup(void)
{
	for (int i = 0; i < maxPopups; i++) if (popups[i].life <= 0) { popups[i] = { { playerXPosition + 10, playerYPosition - 50 }, 0.8f }; return; }
}

// Game flow ----------------------------------------------------------
void ResetGame(void)
{
	playerXPosition = windowWidth / 2 - 300;
	playerYPosition = windowHeight / 2 - 60;
	playerSpeedY = 0.0f;
	Score = 0;
	newBest = false;
	objectMovementSpeed = 0;
	InitHindrances();
	ResetTrail();
	for (int i = 0; i < maxParticles; i++) particles[i].life = 0;
	for (int i = 0; i < maxPopups; i++) popups[i].life = 0;
}

void SwitchMusic(void)
{
	StopMusicStream(*chosenMusic);
	chosenMusic = planeChoice == 0 ? &musicPaper : &musicJet;
	PlayMusicStream(*chosenMusic);
}

void HandleMenuInput(void)
{
	if (IsKeyPressed(KEY_X))
	{
		planeChoice = (planeChoice + 1) % TotalPlanes;
		SwitchMusic();
		PlaySfx(selectSound);
	}
	int before = difficulty;
	if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) difficulty = 0;
	if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) difficulty = 1;
	if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) difficulty = 2;
	if (IsKeyPressed(KEY_LEFT)) difficulty = (difficulty + TotalDifficulties - 1) % TotalDifficulties;
	if (IsKeyPressed(KEY_RIGHT)) difficulty = (difficulty + 1) % TotalDifficulties;
	if (difficulty != before) { InitHindrances(); PlaySfx(selectSound); }
}

void GameOver(void)
{
	if (currentScreen == GAMEOVER) return;
	currentScreen = GAMEOVER;
	screenTimer = GetTime();
	objectMovementSpeed = 0;
	SpawnDerez();
	PlaySfx(crashSound);
	if (newBest) SaveHighscore(difficulty);
}

void CollisionDetection(void)
{
	Vector2 p = { playerXPosition, playerYPosition };
	for (int i = 0; i < maxHindrances; i++)
		if (CheckCollisionCircleRec(p, playerRadius, hindrances[i].topRect) || CheckCollisionCircleRec(p, playerRadius, hindrances[i].bottomRect))
			GameOver();
	if (playerYPosition < 0 || playerYPosition > floorY) GameOver();
}

void PlayerController(float dt)
{
	if (IsKeyPressed(KEY_SPACE))
	{
		playerSpeedY = jumpForce;
		PlaySfx(planeChoice == 0 ? flapSounds[currentSound] : thrustSounds[currentSound]);
		currentSound = (currentSound + 1) % FlapSoundsMax;
	}
	else if (IsKeyDown(KEY_SPACE))
	{
		playerSpeedY -= jumpSustainForce * dt;
	}
	playerSpeedY += gravity * dt;
	if (playerSpeedY > terminalVelocity) playerSpeedY = terminalVelocity;
	if (playerSpeedY < maxUpwardSpeed) playerSpeedY = maxUpwardSpeed;
	playerYPosition += playerSpeedY * dt;
}

void UpdateGameScore(void)
{
	for (int i = 0; i < maxHindrances; i++)
	{
		if (!hindrances[i].scored && hindrances[i].topRect.x + towerWidth < playerXPosition)
		{
			Score++;
			hindrances[i].scored = true;
			PlaySfx(scoreSound);
			SpawnPopup();
		}
	}
	if (Score > Highscore[difficulty]) { Highscore[difficulty] = Score; newBest = true; }
}

// Drawing ------------------------------------------------------------
void DrawHud(void)
{
	DrawText(TextFormat("SCORE %i", Score), 40, 34, 34, white);
	DrawText(TextFormat("BEST  %i", Highscore[difficulty]), 40, 76, 22, WithAlpha(PlaneColor(), 0.9f));
	const char* mode = TextFormat("%s  /  %s", difficultyNames[difficulty], planeNames[planeChoice]);
	DrawText(mode, (int)(windowWidth - MeasureText(mode, 20) - 40), 40, 20, WithAlpha(white, 0.7f));
	if (muted) DrawText("MUTED (M)", (int)(windowWidth - MeasureText("MUTED (M)", 18) - 40), 70, 18, WithAlpha(AccentColor(), 0.9f));
}

void DrawGameplayScene(void)
{
	DrawBackground();
	for (int i = 0; i < maxHindrances; i++) DrawHindrance(hindrances[i]);
	if (currentScreen != GAMEOVER) { DrawTrail(); DrawPlayer(); }
	else DrawTrail();
	DrawParticles();
	DrawHud();
}

void DrawDifficultyPicker(int y)
{
	int boxW = 210, boxH = 64, gapX = 30;
	int total = TotalDifficulties * boxW + (TotalDifficulties - 1) * gapX;
	int x0 = (int)(windowWidth / 2 - total / 2);
	for (int i = 0; i < TotalDifficulties; i++)
	{
		Rectangle r = { (float)(x0 + i * (boxW + gapX)), (float)y, (float)boxW, (float)boxH };
		bool sel = i == difficulty;
		DrawRectangleRec(r, sel ? WithAlpha(PlaneColor(), 0.18f) : WithAlpha(bgTop, 0.7f));
		if (sel) DrawGlowRectLines(r, 2.f, PlaneColor());
		else DrawRectangleLinesEx(r, 1.5f, WithAlpha(white, 0.3f));
		const char* label = TextFormat("%i  %s", i + 1, difficultyNames[i]);
		DrawText(label, (int)(r.x + boxW / 2 - MeasureText(label, 26) / 2), (int)(r.y + 18), 26, sel ? white : WithAlpha(white, 0.55f));
	}
}

void DrawLogoScreen(float t)
{
	ClearBackground(bgTop);
	// Sund Origin sun mark, drawn in neon
	Vector2 c = { windowWidth / 2, windowHeight / 2 - 90 };
	float pulse = 0.8f + 0.2f * sinf(t * 3);
	BeginBlendMode(BLEND_ADDITIVE);
	DrawCircleV(c, 70 * pulse, WithAlpha(orange, 0.12f));
	EndBlendMode();
	DrawCircleV(c, 38, { 230, 185, 106, 255 });
	Vector2 l0 = { c.x - 120, c.y + 55 }, l1 = { c.x - 50, c.y + 70 }, l2 = { c.x - 6, c.y + 150 };
	Vector2 r0 = { c.x + 120, c.y + 55 }, r1 = { c.x + 50, c.y + 70 }, r2 = { c.x + 6, c.y + 150 };
	DrawGlowLine(l0, l1, 5, white); DrawGlowLine(l1, l2, 5, white);
	DrawGlowLine(r0, r1, 5, white); DrawGlowLine(r1, r2, 5, white);
	float a = t < 0.6f ? t / 0.6f : 1.f;
	DrawNeonText("SUND ORIGIN STUDIO", (int)(windowHeight / 2 + 110), 40, WithAlpha(white, a));
	DrawCenteredText("presents", (int)(windowHeight / 2 + 165), 22, WithAlpha(cyan, a * 0.8f));
}

void DrawStartScreen(void)
{
	DrawGameplayScene();
	DrawRectangle(0, 0, (int)windowWidth, (int)windowHeight, WithAlpha(bgTop, 0.45f));
	DrawNeonText("FLAPPY PLANE", 120, 96, PlaneColor());
	DrawNeonText("V3  NEON EDITION", 228, 30, AccentColor());
	DrawCenteredText("DIFFICULTY  (1 2 3  or  LEFT / RIGHT)", 300, 22, WithAlpha(white, 0.75f));
	DrawDifficultyPicker(338);
	DrawCenteredText(TextFormat("PLANE: %s   (X to swap)", planeNames[planeChoice]), 560, 26, white);
	float blink = 0.55f + 0.45f * sinf((float)GetTime() * 4);
	DrawNeonText("PRESS SPACE TO FLY", 620, 40, WithAlpha(PlaneColor(), blink));
	DrawCenteredText("SPACE flap / hold   P pause   M mute", 690, 20, WithAlpha(white, 0.55f));
}

void DrawGameOverScreen(void)
{
	DrawGameplayScene();
	DrawRectangle(0, 0, (int)windowWidth, (int)windowHeight, WithAlpha(bgTop, 0.55f));
	DrawNeonText("DEREZZED", (int)(windowHeight / 2 - 150), 90, AccentColor());
	DrawCenteredText(TextFormat("SCORE  %i", Score), (int)(windowHeight / 2 - 30), 44, white);
	DrawCenteredText(TextFormat("BEST ON %s  %i", difficultyNames[difficulty], Highscore[difficulty]), (int)(windowHeight / 2 + 30), 26, PlaneColor());
	if (newBest && Score > 0) DrawNeonText("NEW BEST!", (int)(windowHeight / 2 + 75), 34, orange);
	if (GetTime() - screenTimer > 0.6) DrawCenteredText("SPACE to try again", (int)(windowHeight / 2 + 135), 26, WithAlpha(white, 0.8f));
}

void DrawPausedScreen(void)
{
	DrawGameplayScene();
	DrawRectangle(0, 0, (int)windowWidth, (int)windowHeight, WithAlpha(bgTop, 0.5f));
	DrawNeonText("PAUSED", (int)(windowHeight / 2 - 60), 90, PlaneColor());
	DrawCenteredText("P to resume", (int)(windowHeight / 2 + 50), 26, white);
}

// Main ----------------------------------------------------------------
int main(void)
{
	InitWindow((int)windowWidth, (int)windowHeight, "Flappy Plane v3 - Neon Edition");
	InitAudioDevice();
	SetTargetFPS(120);

	musicPaper = LoadMusicStream("resources/music_paper.ogg");
	musicJet = LoadMusicStream("resources/music_jet.ogg");
	SetMusicVolume(musicPaper, 0.55f);
	SetMusicVolume(musicJet, 0.55f);
	PlayMusicStream(musicPaper);

	flapSound = LoadSound("resources/flap.wav");
	thrustSound = LoadSound("resources/thrust.wav");
	scoreSound = LoadSound("resources/score.wav");
	crashSound = LoadSound("resources/crash.wav");
	selectSound = LoadSound("resources/select.wav");
	SetSoundVolume(scoreSound, 0.7f);
	// Aliases so rapid flaps overlap instead of cutting each other off (same trick as the original)
	for (int i = 0; i < FlapSoundsMax; i++)
	{
		flapSounds[i] = LoadSoundAlias(flapSound);
		thrustSounds[i] = LoadSoundAlias(thrustSound);
	}

	LoadHighscores();
	InitStars();
	ResetGame();
	screenTimer = GetTime();

	while (!WindowShouldClose())
	{
		float dt = GetFrameTime();
		if (dt > 1.f / 20) dt = 1.f / 20;   // avoid huge jumps if the tab was in the background

		if (IsKeyPressed(KEY_M)) { muted = !muted; if (muted) PauseMusicStream(*chosenMusic); else ResumeMusicStream(*chosenMusic); }
		if (!muted) UpdateMusicStream(*chosenMusic);

		// UPDATE ---------------------------------------------------
		switch (currentScreen)
		{
		case LOGO:
		{
			UpdateStars(dt, 1);
			if (GetTime() - screenTimer > 2.6 || IsKeyPressed(KEY_SPACE)) { ResetGame(); currentScreen = GAMESTART; }
		} break;
		case GAMESTART:
		{
			HandleMenuInput();
			UpdateStars(dt, 1);
			gridScroll += 60 * dt;
			skylineScroll += 10 * dt;
			playerYPosition = windowHeight / 2 - 60 + sinf((float)GetTime() * 2.5f) * 14;
			UpdateTrail(dt);
			for (int i = 0; i < trailCount; i++) trail[i].x -= 120 * dt;
			if (IsKeyPressed(KEY_SPACE))
			{
				objectMovementSpeed = diffSpeed[difficulty] * planeSpeedFactor[planeChoice];
				playerSpeedY = jumpForce;
				currentScreen = GAMEPLAY;
			}
		} break;
		case GAMEPLAY:
		{
			if (IsKeyPressed(KEY_P)) { currentScreen = PAUSED; PauseMusicStream(*chosenMusic); break; }
			PlayerController(dt);
			for (int i = 0; i < maxHindrances; i++) UpdateHindrance(hindrances[i], dt);
			UpdateTrail(dt);
			CollisionDetection();
			if (currentScreen == GAMEPLAY) UpdateGameScore();
			UpdateStars(dt, objectMovementSpeed / 100.f);
			gridScroll += objectMovementSpeed * dt;
			skylineScroll += objectMovementSpeed * 0.15f * dt;
			UpdateParticles(dt);
		} break;
		case GAMEOVER:
		{
			UpdateParticles(dt);
			UpdateStars(dt, 0.3f);
			if (IsKeyPressed(KEY_SPACE) && GetTime() - screenTimer > 0.6) { ResetGame(); currentScreen = GAMESTART; }
		} break;
		case PAUSED:
		{
			if (IsKeyPressed(KEY_P)) { currentScreen = GAMEPLAY; if (!muted) ResumeMusicStream(*chosenMusic); }
		} break;
		default: break;
		}

		// DRAW -----------------------------------------------------
		BeginDrawing();
		switch (currentScreen)
		{
		case LOGO: DrawLogoScreen((float)(GetTime() - screenTimer)); break;
		case GAMESTART: DrawStartScreen(); break;
		case GAMEPLAY: DrawGameplayScene(); break;
		case GAMEOVER: DrawGameOverScreen(); break;
		case PAUSED: DrawPausedScreen(); break;
		default: break;
		}
		EndDrawing();
	}

	for (int i = 0; i < FlapSoundsMax; i++) { UnloadSoundAlias(flapSounds[i]); UnloadSoundAlias(thrustSounds[i]); }
	UnloadSound(flapSound); UnloadSound(thrustSound); UnloadSound(scoreSound); UnloadSound(crashSound); UnloadSound(selectSound);
	UnloadMusicStream(musicPaper); UnloadMusicStream(musicJet);
	CloseAudioDevice();
	CloseWindow();
	return 0;
}
