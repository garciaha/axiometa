/*
 * BREAKOUT — Atari 2600-style brick-smashing game (PORTRAIT)
 *
 * Turn the rotary encoder to slide the paddle. Press the button to launch
 * the ball. Clear every brick to advance a level; the ball speeds up each
 * level (and again once the board is nearly clear, like the original).
 * Miss the ball three times and it's game over.
 *
 * Hardware (identical rig to the Kaboom! port):
 *   P1  ST7735 Display     (AX22-0034)  CS=P1_IO0, RST=P1_IO1, DC=P1_IO2 + shared SPI
 *   P2  Passive Buzzer     (AX22-0018)  signal=P2_IO1
 *   P3  Tactile LED Button (AX22-0050)  button=P3_IO1, LED=P3_IO2
 *   P4  Rotary Encoder     (AX22-0003)  BTN=P4_IO0, CLK=P4_IO1, DT=P4_IO2
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <RotaryEncoder.h>

// ---- Pins ----
#define TFT_CS    P1_IO0
#define TFT_RST   P1_IO1
#define TFT_DC    P1_IO2
#define BUZZER    P2_IO1
#define BTN_PIN   P3_IO1   // tactile button: LOW when pressed (hardware debounced)
#define LED_PIN   P3_IO2   // button LED
#define ENC_BTN   P4_IO0   // rotary encoder push switch (active-low, unused here but wired)
#define ENC_CLK   P4_IO1
#define ENC_DT    P4_IO2

SPIClass mySPI(FSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&mySPI, TFT_CS, TFT_DC, TFT_RST);

// ---- Portrait playfield ----
const int W = 80;
const int H = 160;
GFXcanvas16 canvas(80, 160);
RotaryEncoder encoder(ENC_CLK, ENC_DT, RotaryEncoder::LatchMode::TWO03);

// ---- Types ----
enum GameState { TITLE, PLAYING, BALL_LOST, LEVEL_CLEAR, GAME_OVER };

struct Shard { bool active; float x; float y; float vx; float vy; unsigned long t0; uint16_t color; };

// ---- Colors (panel is BGR: pass color565(B, G, R) to get the perceptual color) ----
uint16_t COL_BG, COL_WHITE, COL_PADDLE, COL_BALL, COL_ACCENT, COL_TITLE, COL_HUD;
uint16_t ROW_COLOR[8];

// ---- Game state ----
GameState state = TITLE;
bool screenDrawn = false;
bool justCleared = false;   // true while a freshly-prepared next level waits for launch

// Paddle — single rect driven by the encoder, same mapping as the Kaboom bucket stack
const int PADDLE_W = 20;
const int PADDLE_H = 3;
const int PADDLE_Y = H - 14;
const int ENC_STEP = 6;     // pixels per detent
float paddleX = (W - PADDLE_W) / 2.0f;
int lastEncPos = 0;

// Ball
const float BALL_R = 2;
const float BASE_SPEED = 1.4f;
float ballX, ballY, ballVX, ballVY;
bool ballLaunched = false;

// Bricks
const int BRICK_ROWS = 6;
const int BRICK_COLS = 5;
const int BRICK_W = 16;
const int BRICK_H = 6;
const int BRICK_TOP = 42;   // wider channel above the wall so the ball has room to bounce behind it
const int HUD_H = 14;            // text band above the playfield (numbers live here, outside the border)
const int TOP_BORDER_THICK = 3;  // top border — thicker than the sides
const int SIDE_BORDER = 1;       // slim left/right border
const int CEILING_Y = HUD_H + TOP_BORDER_THICK;  // ball bounces just below the top border
bool brickActive[BRICK_ROWS][BRICK_COLS];
int bricksRemaining = 0;
// Atari Breakout point scheme, top to bottom: red=7, orange=7, green=4, yellow=4, aqua=1, blue=1
const int ROW_POINTS[BRICK_ROWS] = {7, 7, 4, 4, 1, 1};

const int MAX_SHARD = 12;
Shard shards[MAX_SHARD];

// 1981-style deterministic randomness, same generator pattern as the Kaboom port
uint8_t lfsr = 0x55;

int lives = 3;
int score = 0;
int level = 1;
float speedMult = 1.0f;
// Original Breakout speed ramp — four one-time bumps that persist for the whole game
int hitCount = 0;
bool spedUp4 = false, spedUp12 = false, spedUpOrange = false, spedUpRed = false;

// Button edge detection
bool lastButton = HIGH;
bool lastEncBtn = HIGH;

unsigned long lastDraw = 0, lastBall = 0;
unsigned long lostStep = 0;

// ---- Non-blocking sound engine ----
const int MAX_SEQ = 8;
int seqFreq[MAX_SEQ];
int seqDur[MAX_SEQ];
int seqLen = 0;
int seqIdx = -1;
unsigned long seqNext = 0;

void playSeq(const int* f, const int* d, int n) {
  if (n > MAX_SEQ) n = MAX_SEQ;
  for (int i = 0; i < n; i++) { seqFreq[i] = f[i]; seqDur[i] = d[i]; }
  seqLen = n;
  seqIdx = 0;
  seqNext = millis();
}

void updateSound() {
  if (seqIdx < 0) return;
  unsigned long now = millis();
  if (now < seqNext) return;
  if (seqIdx >= seqLen) { noTone(BUZZER); seqIdx = -1; return; }
  int f = seqFreq[seqIdx];
  if (f > 0) tone(BUZZER, f); else noTone(BUZZER);
  seqNext = now + seqDur[seqIdx];
  seqIdx++;
}

void sfxWall()     { static int f[] = {440};                static int d[] = {30};                 playSeq(f, d, 1); }
void sfxPaddle()   { static int f[] = {659};                static int d[] = {35};                 playSeq(f, d, 1); }
void sfxBrick(int row) {
  static int f[1]; static int d[1] = {40};
  f[0] = 300 + (BRICK_ROWS - 1 - row) * 80;   // higher pitch for higher-value rows
  playSeq(f, d, 1);
}
void sfxLifeLost() { static int f[] = {300, 200, 120};       static int d[] = {100, 100, 160};      playSeq(f, d, 3); }
void sfxLevelUp()  { static int f[] = {523, 659, 784, 1047}; static int d[] = {70, 70, 70, 140};     playSeq(f, d, 4); }
void sfxGameOver() { static int f[] = {392, 330, 262, 196};  static int d[] = {180, 180, 180, 380};  playSeq(f, d, 4); }
void sfxStart()    { static int f[] = {659, 988};            static int d[] = {80, 140};             playSeq(f, d, 2); }
void sfxLaunch()   { static int f[] = {784, 1047};           static int d[] = {60, 90};              playSeq(f, d, 2); }

// ---- Button helpers (HIGH->LOW edge, fires once per physical press) ----
bool buttonPressed() {
  bool b = digitalRead(BTN_PIN);
  bool pressed = (b == LOW && lastButton == HIGH);
  lastButton = b;
  return pressed;
}

bool encButtonPressed() {
  bool b = digitalRead(ENC_BTN);
  bool pressed = (b == LOW && lastEncBtn == HIGH);
  lastEncBtn = b;
  return pressed;
}

// ---- Brick-break particles ----
void triggerShard(float x, float y, uint16_t color) {
  for (int i = 0; i < MAX_SHARD; i++) {
    if (!shards[i].active) {
      shards[i].active = true;
      shards[i].x = x; shards[i].y = y;
      float ang = ((lfsr >> 1) % 8) * 0.785f;   // 8 deterministic directions, same LFSR as the bomber used
      shards[i].vx = cosf(ang) * 1.4f;
      shards[i].vy = sinf(ang) * 1.4f - 0.6f;   // slight upward bias
      shards[i].t0 = millis();
      shards[i].color = color;
      return;
    }
  }
}

void clearShards() { for (int i = 0; i < MAX_SHARD; i++) shards[i].active = false; }

void drawShards() {
  unsigned long now = millis();
  const unsigned long DUR = 260;
  for (int i = 0; i < MAX_SHARD; i++) {
    if (!shards[i].active) continue;
    unsigned long e = now - shards[i].t0;
    if (e >= DUR) { shards[i].active = false; continue; }
    float p = e / (float)DUR;
    int x = (int)(shards[i].x + shards[i].vx * e * 0.06f);
    int y = (int)(shards[i].y + shards[i].vy * e * 0.06f + p * p * 6);  // light gravity arc
    canvas.drawPixel(x, y, shards[i].color);
    canvas.drawPixel(x + 1, y, shards[i].color);
  }
}

// ---- Bricks / ball / level setup ----
void setupBricks() {
  bricksRemaining = 0;
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      brickActive[r][c] = true;
      bricksRemaining++;
    }
  }
}

void resetBall() {
  ballX = paddleX + PADDLE_W / 2.0f;
  ballY = PADDLE_Y - BALL_R - 1;
  ballVX = 0; ballVY = 0;
  ballLaunched = false;
}

void launchBall() {
  // Deterministic kickoff angle via the same LFSR pattern as the Kaboom bomber
  uint8_t lsb = lfsr & 1;
  lfsr >>= 1;
  if (lsb) lfsr ^= 0xB4;
  float speed = BASE_SPEED * speedMult;
  float angle = (lfsr & 1) ? 0.5f : -0.5f;   // launch slightly left or right of straight up
  ballVX = speed * angle;
  ballVY = -speed * 0.9f;
  ballLaunched = true;
  justCleared = false;
  sfxLaunch();
}

// Rescale the ball's current velocity to the new speedMult, preserving direction
void applyBallSpeed() {
  float speed = sqrtf(ballVX * ballVX + ballVY * ballVY);
  if (speed < 0.001f) return;
  float target = BASE_SPEED * speedMult;
  ballVX = ballVX / speed * target;
  ballVY = ballVY / speed * target;
}

void resetGame() {
  lives = 3;
  score = 0;
  level = 1;
  speedMult = 1.0f;
  hitCount = 0;
  spedUp4 = spedUp12 = spedUpOrange = spedUpRed = false;
  paddleX = (W - PADDLE_W) / 2.0f;
  lastEncPos = encoder.getPosition();
  lfsr = 0x55;
  justCleared = false;
  clearShards();
  setupBricks();
  resetBall();
}

void nextLevel() {
  level++;
  // Speed persists from the hit-count / orange-red ramp; no per-level boost
  clearShards();
  setupBricks();
  resetBall();
}

void updatePaddle() {
  int maxX = W - PADDLE_W;
  int newPos = encoder.getPosition();
  int delta = newPos - lastEncPos;
  lastEncPos = newPos;
  if (delta != 0) paddleX -= delta * ENC_STEP;
  if (paddleX < 0) paddleX = 0;
  if (paddleX > maxX) paddleX = maxX;
}

void startBallLost() {
  state = BALL_LOST;
  lostStep = millis();
  sfxLifeLost();
}

void updateBallLost() {
  if (millis() - lostStep >= 500) {
    lives--;
    if (lives <= 0) {
      state = GAME_OVER;
      screenDrawn = false;
      sfxGameOver();
    } else {
      resetBall();
      state = PLAYING;
    }
  }
}

// Resolve at most ONE brick contact at the ball's CURRENT position. Returns
// true if a brick was struck (velocity already bounced + ball ejected clear of
// it), so the caller can stop advancing this frame.
bool collideBricks() {
  int hitR = -1, hitC = -1;
  float bestD = 1e9f;
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      if (!brickActive[r][c]) continue;
      int bx = c * BRICK_W;
      int by = BRICK_TOP + r * BRICK_H;
      // Expand the brick rect by the ball radius (Minkowski sum) for a simple overlap test
      if (ballX + BALL_R >= bx && ballX - BALL_R <= bx + BRICK_W - 1 &&
          ballY + BALL_R >= by && ballY - BALL_R <= by + BRICK_H - 1) {
        float cx = bx + BRICK_W / 2.0f;
        float cy = by + BRICK_H / 2.0f;
        float d = (ballX - cx) * (ballX - cx) + (ballY - cy) * (ballY - cy);
        if (d < bestD) { bestD = d; hitR = r; hitC = c; }
      }
    }
  }
  if (hitR < 0) return false;

  int r = hitR, c = hitC;
  int bx = c * BRICK_W;
  int by = BRICK_TOP + r * BRICK_H;
  brickActive[r][c] = false;
  bricksRemaining--;
  hitCount++;
  score += ROW_POINTS[r];
  triggerShard(bx + BRICK_W / 2.0f, by + BRICK_H / 2.0f, ROW_COLOR[r]);
  sfxBrick(r);

  // Bounce on whichever axis has the shallower penetration, then PUSH the ball
  // back out of the brick along that axis so it ends clear of every cell.
  float overlapX = min(ballX + BALL_R - bx, bx + BRICK_W - (ballX - BALL_R));
  float overlapY = min(ballY + BALL_R - by, by + BRICK_H - (ballY - BALL_R));
  if (overlapX < overlapY) {
    ballVX = -ballVX;
    if (ballX < bx + BRICK_W / 2.0f) ballX = bx - BALL_R; else ballX = bx + BRICK_W + BALL_R;
  } else {
    ballVY = -ballVY;
    if (ballY < by + BRICK_H / 2.0f) ballY = by - BALL_R; else ballY = by + BRICK_H + BALL_R;
  }

  // Original Breakout speed ramp — each event bumps speed once and persists
  // for the whole game: 4th hit, 12th hit, first orange-row hit, first red-row hit.
  bool spedUp = false;
  if (!spedUp4     && hitCount >= 4)        { spedUp4 = true;      speedMult *= 1.20f; spedUp = true; }
  if (!spedUp12    && hitCount >= 12)       { spedUp12 = true;     speedMult *= 1.20f; spedUp = true; }
  if (!spedUpOrange && r == 1)              { spedUpOrange = true; speedMult *= 1.20f; spedUp = true; }
  if (!spedUpRed   && r == 0)               { spedUpRed = true;    speedMult *= 1.20f; spedUp = true; }
  if (spedUp) applyBallSpeed();

  if (bricksRemaining == 0) {
    // Prepare the next level immediately but leave it paused — drawn with the
    // ball resting on the paddle, not started until the button is pressed.
    nextLevel();
    justCleared = true;
    state = PLAYING;
    screenDrawn = false;
    sfxLevelUp();
    Serial.println("Level cleared. Next level ready — press button to launch.");
  }
  return true;
}

void updateBall() {
  // Ball rests on the paddle until launched — check every loop tick so the
  // launch press is never missed (untthrottled, since nothing is moving yet)
  if (!ballLaunched) {
    ballX = paddleX + PADDLE_W / 2.0f;
    if (buttonPressed()) launchBall();
    return;
  }

  unsigned long now = millis();
  if (now - lastBall < 16) return;   // ~60Hz physics step once the ball is in flight
  lastBall = now;

  // Sub-step the motion so a fast ball can't skip through a brick row in one
  // tick — that single jump is what cleared two bricks (or a whole column) at
  // high speed. Cap each micro-step to ~1px of travel and run the full
  // wall/paddle/brick resolution every step; bricks resolve one-per-contact, so
  // we stop the instant one is struck.
  float dist = sqrtf(ballVX * ballVX + ballVY * ballVY);
  int steps = (int)ceilf(dist);
  if (steps < 1) steps = 1;
  float dx = ballVX / steps;
  float dy = ballVY / steps;

  for (int s = 0; s < steps; s++) {
    ballX += dx;
    ballY += dy;

    // Side walls (inside the slim border)
    if (ballX - BALL_R <= SIDE_BORDER) { ballX = SIDE_BORDER + BALL_R; ballVX = -ballVX; dx = -dx; }
    if (ballX + BALL_R >= W - SIDE_BORDER) { ballX = W - SIDE_BORDER - BALL_R; ballVX = -ballVX; dx = -dx; }
    // Ceiling
    if (ballY - BALL_R <= CEILING_Y) { ballY = CEILING_Y + BALL_R; ballVY = -ballVY; dy = -dy; }

    // Paddle collision (only while the ball is moving downward, into the paddle band)
    if (ballVY > 0 &&
        ballY + BALL_R >= PADDLE_Y && ballY + BALL_R <= PADDLE_Y + PADDLE_H + 2 &&
        ballX >= paddleX - BALL_R && ballX <= paddleX + PADDLE_W + BALL_R) {
      float hitPos = (ballX - (paddleX + PADDLE_W / 2.0f)) / (PADDLE_W / 2.0f);
      hitPos = constrain(hitPos, -1.0f, 1.0f);
      float speed = sqrtf(ballVX * ballVX + ballVY * ballVY);
      // Map paddle hit position to a bounded launch angle measured from straight
      // up, so an edge hit never produces a near-horizontal screen sweep.
      const float MAX_ANGLE = 1.0f;            // ~57 deg from vertical at the very edge
      float angle = hitPos * MAX_ANGLE;
      ballVX = speed * sinf(angle);
      ballVY = -speed * cosf(angle);           // cos(MAX_ANGLE) ~ 0.54 -> vy always meaningful
      ballY = PADDLE_Y - BALL_R;
      dx = ballVX / steps;                      // remaining sub-steps follow the new heading
      dy = ballVY / steps;
      sfxPaddle();
    }

    // One brick per contact — stop advancing the moment we strike one
    if (collideBricks()) break;
  }

  // Lost past the paddle
  if (ballY - BALL_R > H) {
    startBallLost();
  }
}

// ---- Drawing ----
void drawCentered(const char* s, int y, int size, uint16_t color, bool bold) {
  int w = (int)strlen(s) * 6 * size;
  int x = (W - w) / 2;
  if (x < 0) x = 0;
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(x, y);
  tft.print(s);
  if (bold) { tft.setCursor(x + 1, y); tft.print(s); }
}

void drawHUD() {
  char buf[12];
  snprintf(buf, sizeof(buf), "L%d", level);
  canvas.setTextSize(1);
  canvas.setTextColor(COL_WHITE);
  canvas.setCursor(2, 2);
  canvas.print(buf);

  snprintf(buf, sizeof(buf), "%d", score);
  int x = W - (int)strlen(buf) * 6 - 2;
  canvas.setTextColor(COL_ACCENT);
  canvas.setCursor(x, 2);     canvas.print(buf);
  canvas.setCursor(x + 1, 2); canvas.print(buf);   // faux-bold

  // lives/balls — a ball pip + count, centered in the top row
  snprintf(buf, sizeof(buf), "%d", lives);
  int gw = 5 + 2 + (int)strlen(buf) * 6;   // pip + gap + digits
  int gx = (W - gw) / 2;
  canvas.fillCircle(gx + 2, 6, 2, COL_PADDLE);
  canvas.setTextColor(COL_PADDLE);
  canvas.setCursor(gx + 7, 2); canvas.print(buf);
  canvas.setCursor(gx + 8, 2); canvas.print(buf);   // faux-bold
}

void drawBricks() {
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      if (!brickActive[r][c]) continue;
      int bx = c * BRICK_W;
      int by = BRICK_TOP + r * BRICK_H;
      // Full cell, no inset — adjacent bricks in a row merge into one solid bar
      // (the Atari 2600 wall showed no seams; rows read apart only by colour).
      canvas.fillRect(bx, by, BRICK_W, BRICK_H, ROW_COLOR[r]);
    }
  }
}

void drawPaddleAndBall() {
  canvas.fillRect((int)paddleX, PADDLE_Y, PADDLE_W, PADDLE_H, COL_PADDLE);
  if (state != BALL_LOST) canvas.fillCircle((int)ballX, (int)ballY, (int)BALL_R, COL_BALL);
}

void drawPlaying() {
  canvas.fillScreen(COL_BG);
  drawHUD();                                   // numbers sit above the field, outside the border
  drawBricks();
  drawPaddleAndBall();
  drawShards();
  // Playfield border: thick top, slim sides, no bottom — drawn on top of the field
  canvas.fillRect(0, HUD_H, W, TOP_BORDER_THICK, COL_WHITE);
  canvas.fillRect(0, HUD_H, SIDE_BORDER, H - HUD_H, COL_WHITE);
  canvas.fillRect(W - SIDE_BORDER, HUD_H, SIDE_BORDER, H - HUD_H, COL_WHITE);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
}

void drawTitle() {
  // W=80. size-2 char=12px wide; "BREAKOUT" at size 2 = 96px > 80, so split.
  tft.fillScreen(COL_BG);
  drawCentered("BREAK",         10, 2, COL_TITLE,  true);   // 5*12=60px
  drawCentered("OUT",           28, 2, COL_TITLE,  true);   // 3*12=36px
  drawCentered("Smash the",     54, 1, COL_WHITE,  false);  // 9*6=54px
  drawCentered("brick wall",    65, 1, COL_WHITE,  false);  // 10*6=60px
  drawCentered("By B. Stewart", 84, 1, COL_ACCENT, false);  // 13*6=78px
  drawCentered("Port by HL2J",  95, 1, COL_ACCENT, false);  // 12*6=72px
  drawCentered("Turn: move",   115, 1, COL_WHITE,  false);  // 10*6=60px
  drawCentered("Btn: launch",  126, 1, COL_WHITE,  false);  // 11*6=66px
  drawCentered("Press to play",147, 1, COL_ACCENT, false);  // 13*6=78px
}

void drawLevelClear() {
  tft.fillScreen(COL_BG);
  drawCentered("LEVEL",        30, 2, COL_TITLE,  true);   // 5*12=60px
  drawCentered("CLEAR",        52, 2, COL_TITLE,  true);   // 5*12=60px
  char buf[12];
  drawCentered("SCORE",        84, 1, COL_WHITE,  false);  // 30px
  snprintf(buf, sizeof(buf), "%d", score);
  drawCentered(buf,            96, 2, COL_ACCENT, true);
  drawCentered("Press button", 134, 1, COL_ACCENT, false); // 12*6=72px
  drawCentered("next level",   146, 1, COL_ACCENT, false); // 10*6=60px
}

void drawGameOver() {
  tft.fillScreen(COL_BG);
  drawCentered("GAME",         26, 2, COL_TITLE,  true);   // 4*12=48px
  drawCentered("OVER",         48, 2, COL_TITLE,  true);   // 4*12=48px
  drawCentered("SCORE",        82, 1, COL_WHITE,  false);  // 30px
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", score);
  drawCentered(buf,            94, 2, COL_ACCENT, true);
  drawCentered("Press button", 128, 1, COL_ACCENT, false); // 12*6=72px
  drawCentered("to play again",140, 1, COL_ACCENT, false); // 13*6=78px
}

void setup() {
  Serial.begin(115200);
  Serial.println("BREAKOUT booting...");

  pinMode(BTN_PIN, INPUT);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  mySPI.begin(SCK, MISO, MOSI);
  tft.initR(INITR_MINI160x80);
  tft.setRotation(0);        // portrait: 80 wide x 160 tall

  // BGR color order: color565(B, G, R)
  COL_BG     = tft.color565(10, 10, 14);       // near-black playfield, faint cool tint
  COL_WHITE  = tft.color565(255, 255, 255);    // pure white
  COL_PADDLE = tft.color565(220, 220, 80);     // cyan paddle
  COL_BALL   = tft.color565(180, 240, 255);    // warm white ball
  COL_ACCENT = tft.color565(40, 200, 255);     // yellow HUD text
  COL_TITLE  = tft.color565(0, 85, 255);       // red-orange title
  COL_HUD    = tft.color565(40, 40, 40);       // dark grey HUD band

  // Atari Breakout rows, top to bottom: red=7, orange=7, green=4, yellow=4, aqua=1, blue=1.
  // Panel is BGR: color565(B, G, R).
  ROW_COLOR[0] = tft.color565(40, 40, 255);    // red
  ROW_COLOR[1] = tft.color565(0, 140, 255);    // orange
  ROW_COLOR[2] = tft.color565(40, 255, 40);    // green
  ROW_COLOR[3] = tft.color565(0, 255, 255);    // yellow
  ROW_COLOR[4] = tft.color565(255, 255, 0);    // aqua
  ROW_COLOR[5] = tft.color565(255, 40, 40);    // blue

  randomSeed(esp_random());
  lastEncPos = encoder.getPosition();

  Serial.println("Ready. Press the button to start.");
}

void loop() {
  encoder.tick();          // MUST run every iteration
  updateSound();

  // Poll the encoder button edge ONCE per loop, unconditionally, so lastEncBtn
  // never goes stale. (If it were only read inside a guarded branch, the first
  // read after a level clear would compare against an ancient HIGH and any
  // line bounce would register as a phantom press — popping the screen on its
  // own.) Consume this single value wherever an encoder press is needed.
  bool encEdge = encButtonPressed();

  switch (state) {
    case TITLE:
      if (!screenDrawn) { drawTitle(); screenDrawn = true; }
      digitalWrite(LED_PIN, (millis() / 400) % 2);   // pulse "press me"
      if (buttonPressed()) {
        resetGame();
        sfxStart();
        digitalWrite(LED_PIN, LOW);
        state = PLAYING;
        Serial.println("Round start!");
      }
      break;

    case PLAYING:
      digitalWrite(LED_PIN, ballLaunched ? LOW : (millis() / 300) % 2);  // pulse while waiting to launch
      // While a freshly-prepared level waits to launch, the rotary encoder
      // press pops the optional LEVEL CLEAR screen.
      if (justCleared && !ballLaunched && encEdge) {
        state = LEVEL_CLEAR;
        screenDrawn = false;
        break;
      }
      updatePaddle();
      updateBall();
      if (millis() - lastDraw >= 33) {   // ~30fps
        drawPlaying();
        lastDraw = millis();
      }
      break;

    case BALL_LOST:
      digitalWrite(LED_PIN, LOW);
      updateBallLost();
      if (millis() - lastDraw >= 33) {
        drawPlaying();
        lastDraw = millis();
      }
      break;

    case LEVEL_CLEAR:
      // On-demand celebration screen, reached only by pressing the rotary
      // encoder on the prepared next level. The tactile button launches the
      // ball straight into play; the rotary encoder dismisses back to the
      // prepared field.
      if (!screenDrawn) { drawLevelClear(); screenDrawn = true; }
      digitalWrite(LED_PIN, (millis() / 400) % 2);
      if (buttonPressed()) {
        launchBall();
        digitalWrite(LED_PIN, LOW);
        state = PLAYING;
        Serial.println("Launch from clear screen!");
      } else if (encEdge) {
        state = PLAYING;
        screenDrawn = false;
        Serial.println("Back to prepared level.");
      }
      break;

    case GAME_OVER:
      if (!screenDrawn) { drawGameOver(); screenDrawn = true; }
      digitalWrite(LED_PIN, (millis() / 400) % 2);
      if (buttonPressed()) {
        resetGame();
        sfxStart();
        digitalWrite(LED_PIN, LOW);
        state = PLAYING;
        Serial.println("New round start!");
      }
      break;
  }
}
