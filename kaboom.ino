/*
 * KABOOM! — Atari 2600-style bomb-catching game (PORTRAIT)
 *
 * A Mad Bomber roams the top of the screen dropping bombs.
 * Turn the rotary encoder to slide your stack of buckets and catch them.
 * Miss one and every falling bomb explodes — you lose a bucket.
 * Survive, score, and the bomber speeds up.
 * Press the lit button to start a round; press again after game over to play a new round.
 *
 * Hardware:
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
#define ENC_BTN   P4_IO0   // rotary encoder push switch (active-low)
#define ENC_CLK   P4_IO1
#define ENC_DT    P4_IO2

SPIClass mySPI(FSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&mySPI, TFT_CS, TFT_DC, TFT_RST);

// ---- Portrait playfield ----
const int W = 80;
const int H = 160;
GFXcanvas16 canvas(80, 160);
RotaryEncoder encoder(ENC_CLK, ENC_DT, RotaryEncoder::LatchMode::TWO03);

// ---- Types (declared above all functions) ----
enum GameState { TITLE, PLAYING, DYING, WAVE_CLEAR, GAME_OVER };

struct Bomb { bool active; float x; float y; };
struct Splash { bool active; int x; int y; unsigned long t0; };
struct Blast  { bool active; int x; int y; unsigned long t0; };

// ---- Colors (panel is BGR: pass color565(B, G, R)) ----
uint16_t COL_BG, COL_WHITE, COL_GREY, COL_BOMBER, COL_BOMB, COL_BOMBHI, COL_FUSE, COL_SPARK, COL_BUCKET, COL_RIM, COL_ACCENT, COL_SKIN, COL_BOMBERBG, COL_EXPO, COL_TITLE;

// ---- Game state ----
GameState state = TITLE;
bool screenDrawn = false;

const int MAX_BOMBS = 10;
Bomb bombs[MAX_BOMBS];

const int MAX_SPLASH = 4;
Splash splashes[MAX_SPLASH];

const int MAX_BLAST = MAX_BOMBS;
Blast blasts[MAX_BLAST];

// Paddle: a vertical STACK of buckets (one column), driven by the encoder
const int BUCKET_W    = 16;   // column width
const int BUCKET_H    = 8;    // each bucket's height
const int BUCKET_PITCH = 9;   // vertical spacing between stacked buckets
const int STACK_BOTTOM = H - 2;             // bottom edge of the lowest bucket
const int ENC_STEP    = 6;    // pixels per detent
float paddleX = (W - BUCKET_W) / 2;
int lastEncPos = 0;

// Bomber
float bomberX = 8;
int bomberDir = 1;
const int bomberY = 12;
const int bomberW = 14;
const int bomberH = 6;

// Data-driven progression tables
const int BOMBS_PER_GROUP[] = {0, 10, 20, 30, 40, 50, 75, 100, 150};
const float WAVE_SPEEDS[]   = {0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f};

// 1981 Deterministic Randomness
uint8_t lfsr = 0x55;  // Fixed starting seed for authentic repeating patterns

// Top bucket's top edge = fixed catch line; the stack shrinks from the bottom as lives are lost
const int CATCH_Y = (STACK_BOTTOM - BUCKET_H) - 2 * BUCKET_PITCH;
int catchLineY() { return CATCH_Y; }

// Button edge detection
bool lastButton = HIGH;
bool lastEncBtn = HIGH;
bool showWaveScreen = false;   // wave-clear screen only appears if you press the encoder

// Bomb size (round bomb body)
const int bombW = 5;
const int bombH = 5;

int lives = 3;
int score = 0;
int group = 1;              // current bomb group / level (original KABOOM! progression)
int caughtInGroup = 0;      // bombs caught in the current wave
int droppedInGroup = 0;     // bombs the bomber has released this wave (stops at waveTarget)
int waveTarget = 10;        // bombs to drop AND catch to clear this wave
int returnGroup = 0;        // >0 during recovery: the wave to jump to after clearing
int awardedMilestones = 0;  // # of 1000-pt bucket awards already processed

unsigned long lastCascadeStep = 0;
unsigned long lastDraw = 0, lastBomber = 0, lastFall = 0, lastDrop = 0;
unsigned long deathStep = 0;   // death-animation start time
int fuseBlink = 0;

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

void sfxCatch()     { static int f[] = {988};                 static int d[] = {45};                 playSeq(f, d, 1); }
void sfxExplosion() { static int f[] = {300, 240, 190, 150, 120, 95, 70, 50}; static int d[] = {120, 120, 120, 130, 130, 130, 130, 120}; playSeq(f, d, 8); }   // ~1.0s blast
void sfxLevelUp()   { static int f[] = {523, 659, 784, 1047}; static int d[] = {70, 70, 70, 140};    playSeq(f, d, 4); }
void sfxGameOver()  { static int f[] = {392, 330, 262, 196};  static int d[] = {180, 180, 180, 380}; playSeq(f, d, 4); }
void sfxStart()     { static int f[] = {659, 988};            static int d[] = {80, 140};            playSeq(f, d, 2); }

// ---- Helpers ----
int activeBombs() {
  int n = 0;
  for (int i = 0; i < MAX_BOMBS; i++) if (bombs[i].active) n++;
  return n;
}

bool anyBlastActive() {
  for (int i = 0; i < MAX_BLAST; i++) if (blasts[i].active) return true;
  return false;
}

// ---- 1981 LFSR Generator ----
bool checkBomberTurn(int currentWave) {
  // Classic 8-bit Galois LFSR
  uint8_t lsb = lfsr & 1;
  lfsr >>= 1;
  if (lsb) {
    lfsr ^= 0xB4;   // Tap polynomial for maximal 255 length sequence
  }
  
  // Even waves: Sweeping pattern. 
  // Stricter mask (0x7F) means a 1-in-128 chance to turn mid-screen.
  if (currentWave % 2 == 0) {
    return (lfsr & 0x7F) == 0;
  } 
  // Odd waves: Scattered pattern.
  // Looser mask (0x0F) means a 1-in-16 chance to turn mid-screen.
  else {
    return (lfsr & 0x0F) == 0;
  }
}

void clearBombs() {
  for (int i = 0; i < MAX_BOMBS; i++) bombs[i].active = false;
}

void clearSplashes() {
  for (int i = 0; i < MAX_SPLASH; i++) splashes[i].active = false;
}

// A quick water-style splash where a bomb is caught
void triggerSplash(int x, int y) {
  int slot = 0;
  for (int i = 0; i < MAX_SPLASH; i++) {
    if (!splashes[i].active) { slot = i; break; }
    slot = i;   // none free yet -> remember last as fallback
  }
  splashes[slot].active = true;
  splashes[slot].x = x;
  splashes[slot].y = y;
  splashes[slot].t0 = millis();
}

void drawSplashes() {
  unsigned long now = millis();
  const unsigned long DUR = 200;
  for (int i = 0; i < MAX_SPLASH; i++) {
    if (!splashes[i].active) continue;
    unsigned long e = now - splashes[i].t0;
    if (e >= DUR) { splashes[i].active = false; continue; }
    float p = e / (float)DUR;                 // 0..1 progress
    float arc = sinf(p * 3.14159f);           // rises then falls
    int cx = splashes[i].x;
    int cy = splashes[i].y - 3;
    for (int d = -2; d <= 2; d++) {           // droplets flung up and out
      if (d == 0) continue;
      int dx = (int)(d * 4 * p);
      int dy = (int)(-9 * arc) + (int)(p * 3);
      uint16_t c = (e < DUR / 2) ? COL_RIM : COL_BUCKET;
      canvas.drawPixel(cx + dx, cy + dy, c);
    }
    if (p < 0.45) canvas.drawFastHLine(cx - 2, cy - (int)(arc * 3), 4, COL_RIM);  // central pop
  }
}

void clearBlasts() {
  for (int i = 0; i < MAX_BLAST; i++) blasts[i].active = false;
}

// A small expanding burst where a bomb detonates (death animation)
void triggerBlast(int x, int y) {
  for (int i = 0; i < MAX_BLAST; i++) {
    if (!blasts[i].active) { blasts[i].active = true; blasts[i].x = x; blasts[i].y = y; blasts[i].t0 = millis(); return; }
  }
}

void drawBlasts() {
  unsigned long now = millis();
  const unsigned long DUR = 280;
  for (int i = 0; i < MAX_BLAST; i++) {
    if (!blasts[i].active) continue;
    unsigned long e = now - blasts[i].t0;
    if (e >= DUR) { blasts[i].active = false; continue; }
    float p = e / (float)DUR;
    int r = 2 + (int)(p * 7);
    uint16_t c = (p < 0.5) ? COL_WHITE : COL_ACCENT;
    if (p < 0.4) canvas.fillCircle(blasts[i].x, blasts[i].y, 2, COL_EXPO);
    canvas.drawCircle(blasts[i].x, blasts[i].y, r, c);
  }
}

// ---- Original KABOOM! scoring tables ----
// Bombs per group: 10,20,30,40,50,75,100,150 (group 8 repeats thereafter)

int bombsInGroup(int g) {
  int index = (g > 8) ? 8 : g;
  return BOMBS_PER_GROUP[index];
}

// Each bomb is worth its group number; capped at 8 (group 8 is the highest)
int pointsPerBomb(int g) { return g > 8 ? 8 : g; }

// Per-wave speed multiplier — the original "feel": 1.0,1.5,2.0,2.5,3.0,4.0,5.0,6.0
float waveSpeed(int g) {
  int index = (g > 8) ? 8 : g;
  return WAVE_SPEEDS[index];
}

// For every 1000 points, replace a missing bucket — never exceed 3, none if full
void awardBonusBuckets() {
  int milestones = score / 1000;
  while (awardedMilestones < milestones) {
    awardedMilestones++;
    if (lives < 3) {
      lives++;
      sfxLevelUp();
      Serial.println("Bonus bucket awarded (1000 pts)!");
    }
  }
}

// Returns true exactly once per physical press (HIGH->LOW edge)
bool buttonPressed() {
  bool b = digitalRead(BTN_PIN);
  bool pressed = (b == LOW && lastButton == HIGH);
  lastButton = b;
  return pressed;
}

// Rotary-encoder push switch: true once per press (HIGH->LOW edge)
bool encButtonPressed() {
  bool b = digitalRead(ENC_BTN);
  bool pressed = (b == LOW && lastEncBtn == HIGH);
  lastEncBtn = b;
  return pressed;
}

void resetGame() {
  clearBombs();
  clearSplashes();
  clearBlasts();
  lives = 3;
  score = 0;
  group = 1;
  caughtInGroup = 0;
  droppedInGroup = 0;
  waveTarget = bombsInGroup(1);
  returnGroup = 0;
  awardedMilestones = 0;
  bomberX = 8;
  bomberDir = (lfsr & 1) ? 1 : -1;
  paddleX = (W - BUCKET_W) / 2;
  lastEncPos = encoder.getPosition();
  lfsr = 0x55;  // Reset LFSR so wave patterns repeat exactly every game
}

void updatePaddle() {
  int maxX = W - BUCKET_W;
  int newPos = encoder.getPosition();
  int delta = newPos - lastEncPos;
  lastEncPos = newPos;
  if (delta != 0) paddleX -= delta * ENC_STEP;   // inverted: knob now turns the same way the buckets slide
  if (paddleX < 0) paddleX = 0;
  if (paddleX > maxX) paddleX = maxX;
}

void spawnBomb() {
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active) {
      bombs[i].active = true;
      bombs[i].x = bomberX + bomberW / 2 - bombW / 2;
      bombs[i].y = bomberY + bomberH;
      return;
    }
  }
}

// A miss freezes the encoder and detonates every bomb on screen,
// bottom-to-top, then flashes the background before the penalty applies.
void startDeath() {
  clearSplashes();
  clearBlasts();
  state = DYING;
  deathStep = millis();
  lastCascadeStep = 0; // Reset our new cascade timer
  sfxExplosion();   
  // Notice we DO NOT clear or detonate the bombs here! They freeze in mid-air.
  Serial.println("Hit! Starting chain-reaction cascade...");
}

void updateDying() {
  unsigned long now = millis();
  
  // Every 120ms, find the lowest active bomb and detonate it
  if (now - lastCascadeStep >= 120) {
    lastCascadeStep = now;
    
    int lowestIdx = -1;
    float max_y = -999;
    
    // Scan for the active bomb closest to the bottom of the screen
    for (int i = 0; i < MAX_BOMBS; i++) {
      if (bombs[i].active && bombs[i].y > max_y) {
        max_y = bombs[i].y;
        lowestIdx = i;
      }
    }
    
    // Detonate it and remove it from the active falling list
    if (lowestIdx != -1) {
      triggerBlast((int)bombs[lowestIdx].x + bombW / 2, (int)bombs[lowestIdx].y + bombH / 2);
      bombs[lowestIdx].active = false;
    }
  }

  // Hold the death screen until every bomb has detonated AND its blast animation
  // has finished expanding — otherwise the screen cuts away mid-cascade.
  if (now - deathStep >= 800 && activeBombs() == 0 && !anyBlastActive()) {
    applyMissPenalty();
  }
}

void applyMissPenalty() {
  lives--;
  bomberX = 8;
  bomberDir = (lfsr & 1) ? 1 : -1;
  if (lives <= 0) {
    state = GAME_OVER;
    screenDrawn = false;
    sfxGameOver();
    Serial.print("Game over. Final score: ");
    Serial.println(score);
    return;
  }

  // Miss penalty:
  //  Wave 1  -> stay on wave 1, catch the full 10 again.
  //  Wave 2+ -> drop one wave, catch 50% of that lower wave, then return to the death wave.
  int deathWave = group;
  if (deathWave <= 1) {
    group = 1;
    waveTarget = bombsInGroup(1);
    returnGroup = 0;
  } else {
    group = deathWave - 1;
    waveTarget = max(1, bombsInGroup(group) / 2);
    returnGroup = deathWave;
  }
  caughtInGroup = 0;
  droppedInGroup = 0;
  lastDrop = millis();
  clearBombs();
  clearBlasts();
  state = PLAYING;
  Serial.print("Missed! Now wave ");
  Serial.print(group > 8 ? 8 : group);
  Serial.print(", catch ");
  Serial.println(waveTarget);
}

// Miss animation: a color-cycling background strobe with every bomb shown as an
// explosion sprite, held for 1.0 second.
void drawDeathStrobe() {
  unsigned long now = millis();
  uint16_t strobe[4] = { COL_WHITE, COL_EXPO, COL_ACCENT, COL_BOMBER };
  canvas.fillScreen(strobe[(now / 80) % 4]);   // color-cycling strobe

  // 1. Draw the active blasts — each animates from its own t0 so the cascade is visible:
  //    the oldest blast is the largest, the newest is still a small hot core.
  const unsigned long BLAST_DUR = 380;
  for (int i = 0; i < MAX_BLAST; i++) {
    if (!blasts[i].active) continue;
    unsigned long e = now - blasts[i].t0;
    if (e >= BLAST_DUR) { blasts[i].active = false; continue; }
    float p = e / (float)BLAST_DUR;
    int   r = 2 + (int)(p * 12);               // radius grows 2→14 over its lifetime
    int   x = blasts[i].x, y = blasts[i].y;
    if (p < 0.35f) canvas.fillCircle(x, y, 3, COL_EXPO);              // hot core
    canvas.fillCircle(x, y, r, (p < 0.5f) ? COL_WHITE : COL_ACCENT);
    canvas.drawLine(x - r - 2, y,     x + r + 2, y,     COL_EXPO);   // burst spikes
    canvas.drawLine(x,     y - r - 2, x,     y + r + 2, COL_EXPO);
    canvas.drawLine(x - r, y - r,     x + r, y + r,     COL_EXPO);
    canvas.drawLine(x - r, y + r,     x + r, y - r,     COL_EXPO);
  }

  // 2. Draw the frozen, unexploded bombs waiting in the chain reaction
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active) continue;
    int bcx = (int)bombs[i].x + bombW / 2;
    int bcy = (int)bombs[i].y + bombH / 2;
    canvas.fillCircle(bcx, bcy, bombW / 2, COL_BOMB);            
    canvas.drawFastHLine(bcx - 2, bcy - 1, 5, COL_GREY);         
    canvas.drawFastHLine(bcx - 2, bcy + 1, 5, COL_GREY);
    canvas.drawFastHLine(bcx - 2, bcy,     5, COL_WHITE);        
    canvas.drawPixel(bcx - 1, bcy - 2, COL_BOMBHI);             
    canvas.drawPixel(bcx + 1, bcy - bombH / 2 - 1, COL_FUSE);   
  }

  // 3. Draw the Mad Bomber (Smiling!)
  int bx_bomber = (int)bomberX;
  drawBomber(bx_bomber + bomberW / 2, bomberY, true);

  // 4. Draw Buckets (Top-Down so bottom bucket is lost)
  int bx_paddle = (int)paddleX;
  for (int i = 0; i < lives; i++) {
    int by = CATCH_Y + i * BUCKET_PITCH;  
    canvas.fillRect(bx_paddle + 1, by + 1, BUCKET_W - 2, BUCKET_H - 1, COL_BUCKET);
    canvas.drawFastHLine(bx_paddle, by, BUCKET_W, COL_RIM);   
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
}

void updatePlaying() {
  unsigned long now = millis();

  // Difficulty follows the original per-wave speed multiplier
  float mult = waveSpeed(group);
  int bomberInterval = max(6,   (int)(40   / mult));
  int fallInterval   = max(10,  (int)(60   / mult));
  int dropInterval   = max(220, (int)(1500 / mult));
  int maxConcurrent  = min(MAX_BOMBS, 2 + (int)mult);

  // Wave parity: even waves are a sweeping stream, odd waves are scattered.
  // checkBomberTurn() already handles direction (rare turns on even, frequent on odd).
  // Here we match the bomb DROP RATE to that pattern so the difference is clearly visible:
  // even waves drop bombs ~2× faster and allow more in the air at once, creating a dense
  // curtain that trails the bomber's arc; odd waves keep the standard spacing.
  if (group % 2 == 0) {
    dropInterval  = max(120, (int)(dropInterval * 0.55f));
    maxConcurrent = min(MAX_BOMBS, maxConcurrent + 2);
  }

  // Bomber movement
  if (now - lastBomber >= (unsigned long)bomberInterval) {
    lastBomber = now;
    bomberX += bomberDir * 2;
    if (bomberX < 0) { bomberX = 0; bomberDir = 1; }
    if (bomberX > W - bomberW) { bomberX = W - bomberW; bomberDir = -1; }
    // 1981 Way: Poll the deterministic LFSR to dictate mid-screen direction changes
    if (checkBomberTurn(group)) bomberDir = -bomberDir;
  }

  // Drop new bombs — the bomber only releases this wave's quota, then stops
  if (now - lastDrop >= (unsigned long)dropInterval && droppedInGroup < waveTarget && activeBombs() < maxConcurrent) {
    lastDrop = now;
    spawnBomb();
    droppedInGroup++;
  }

  // Bombs fall + catch/miss
  int catchY = catchLineY();
  if (now - lastFall >= (unsigned long)fallInterval) {
    lastFall = now;
    for (int i = 0; i < MAX_BOMBS; i++) {
      if (!bombs[i].active) continue;
      bombs[i].y += 2;
      float cx = bombs[i].x + bombW / 2.0;

      // Reached the top bucket's catch line
      if (bombs[i].y + bombH >= catchY) {
        if (cx >= paddleX && cx <= paddleX + BUCKET_W) {
          bombs[i].active = false;
          triggerSplash((int)cx, catchY);
          score += pointsPerBomb(group);
          caughtInGroup++;
          sfxCatch();
          awardBonusBuckets();
          if (caughtInGroup >= waveTarget) {       // caught everything the bomber sent
            caughtInGroup = 0;
            droppedInGroup = 0;
            if (returnGroup != 0) { group = returnGroup; returnGroup = 0; }  // recovered: back to the death wave
            else group++;
            waveTarget = bombsInGroup(group);
            clearBombs();
            state = WAVE_CLEAR;
            showWaveScreen = false;   // frozen playfield first; screen only on encoder press
            screenDrawn = false;
            lastEncBtn = digitalRead(ENC_BTN);   // capture a clean baseline so a held/stale encoder press can't false-trigger the wave screen
            lastButton = digitalRead(BTN_PIN);   // same for the tactile button
            sfxLevelUp();
            Serial.print("Wave cleared! Next wave ");
            Serial.println(group > 8 ? 8 : group);
            return;
          }
          continue;
        }
      }
      // Past the bottom without a catch
      if (bombs[i].y > H) {
        startDeath();
        return;
      }
    }
  }

  fuseBlink = (now / 120) % 2;
}

// Draws an authentic 16x8 pixel-art Kaboom! bucket
// Uses COL_WHITE for the rim, COL_RIM for the blue water, and COL_BUCKET for the tub
void drawBucket(int x, int y) {
canvas.fillRect(x, y, 16, 8, COL_BUCKET);
  
  // 2. Draw the blue water near the top rim
  canvas.drawFastHLine(x + 1, y + 1, 14, COL_RIM);
  // Optional: A small white sparkle on the water for depth
  canvas.drawFastHLine(x + 2, y + 1, 4, COL_WHITE); 
  
  // 4. Wooden staves (thin vertical separators)
  canvas.drawFastVLine(x + 3,  y + 2, 6, COL_GREY);
  canvas.drawFastVLine(x + 7,  y + 2, 6, COL_GREY);
  canvas.drawFastVLine(x + 11, y + 2, 6, COL_GREY);
}

// Draws an authentic pixel-art Mad Bomber sprite
void drawBomber(int cx, int y, bool isSmiling) {
  // 1. The Hat
  canvas.fillRect(cx - 3, y - 10, 6, 2, COL_GREY);     // Hat Top (Black)
  canvas.drawFastHLine(cx - 4, y - 8, 8, COL_BOMBER);  // Hat Band (Red)
  canvas.drawFastHLine(cx - 5, y - 7, 10, COL_GREY);   // Hat Brim (Black)

  // 2. The Face
  canvas.fillRect(cx - 4, y - 6, 8, 6, COL_SKIN);      // Head shape
  canvas.drawFastHLine(cx - 4, y - 4, 8, COL_GREY);    // Domino Mask (Black)
  
  // 3. The Mouth
  if (isSmiling) {
    // A big red grin for when you miss
    canvas.drawFastHLine(cx - 2, y - 2, 4, COL_BOMBER);
    canvas.drawFastHLine(cx - 1, y - 1, 2, COL_BOMBER);
  } else {
    // A tight frown for normal gameplay
    canvas.drawFastHLine(cx - 2, y - 1, 4, COL_GREY);
    canvas.drawFastHLine(cx - 1, y - 2, 2, COL_GREY);
  }

  // 4. The Prisoner Shirt (Alternating horizontal white and black stripes)
  for (int i = 0; i < 7; i++) {
    uint16_t stripeCol = (i % 2 == 0) ? COL_WHITE : COL_GREY;
    canvas.drawFastHLine(cx - 6, y + i, 12, stripeCol);
  }

  // 5. The Hands (Dangling below the bottom corners of the sleeves)
  canvas.fillRect(cx - 6, y + 7, 2, 2, COL_SKIN); // Left hand
  canvas.fillRect(cx + 4, y + 7, 2, 2, COL_SKIN); // Right hand
}

// Score + wave readout: score is yellow faux-bold right-aligned; wave number is white left-aligned
void drawScore() {
  char buf[12];

  // Wave number — left-aligned, white
  int displayWave = (group > 8) ? 8 : group;
  snprintf(buf, sizeof(buf), "W%d", displayWave);
  canvas.setTextSize(1);
  canvas.setTextColor(COL_WHITE);
  canvas.setCursor(2, 2);
  canvas.print(buf);

  // Score — right-aligned, yellow, faux-bold (drawn four times for weight)
  snprintf(buf, sizeof(buf), "%d", score);
  int x = W - (int)strlen(buf) * 6 - 2;
  canvas.setTextSize(1);
  canvas.setTextColor(COL_ACCENT);
  canvas.setCursor(x, 2);     canvas.print(buf);
  canvas.setCursor(x + 1, 2); canvas.print(buf);   // heavier faux-bold: x + y offsets
  canvas.setCursor(x, 3);     canvas.print(buf);
  canvas.setCursor(x + 1, 3); canvas.print(buf);
}

void drawPlaying() {
  canvas.fillScreen(COL_BG);

  // Light grey band behind the bomber
  canvas.fillRect(0, 0, W, bomberY + bomberH + 3, COL_BOMBERBG);

  // Score — yellow, bold, numbers only, right-aligned
  drawScore();

  // Bomber — Frowning while he drops bombs!
  int bx = (int)bomberX;
  drawBomber(bx + bomberW / 2, bomberY, false);

  // Bombs — round bombs with a lit fuse (Atari style)
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active) continue;
    int bcx = (int)bombs[i].x + bombW / 2;
    int bcy = (int)bombs[i].y + bombH / 2;
    canvas.fillCircle(bcx, bcy, bombW / 2, COL_BOMB);            // round body
    canvas.drawFastHLine(bcx - 2, bcy - 1, 5, COL_GREY);         // light grey equator strip
    canvas.drawFastHLine(bcx - 2, bcy + 1, 5, COL_GREY);
    canvas.drawFastHLine(bcx - 2, bcy,     5, COL_WHITE);        // white line exactly at the middle
    canvas.drawPixel(bcx - 1, bcy - 2, COL_BOMBHI);             // glossy highlight on the dark cap
    canvas.drawPixel(bcx + 1, bcy - bombH / 2 - 1, COL_FUSE);   // fuse stem
    if (fuseBlink) canvas.drawPixel(bcx + 2, bcy - bombH / 2 - 2, COL_SPARK);  // blinking spark
  }

  // Buckets — stacked vertically (i=0 is the lowest, top bucket is the catcher)
  int bx_paddle = (int)paddleX;
  for (int i = 0; i < lives; i++) {
    int by = CATCH_Y + i * BUCKET_PITCH;  
    drawBucket(bx_paddle, by);
  }

  drawSplashes();   // catch splashes drawn on top of everything
  drawBlasts();     // detonation bursts (death animation)

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
}

// Draw a string horizontally centered on the 80px screen at row y.
// bold = draw a second time one pixel right for a faux-bold weight.
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

void drawTitle() {
  tft.fillScreen(COL_BG);

  // Main title
  drawCentered("KABOOM", 24, 2, COL_TITLE, true);

  // Tagline
  drawCentered("Catch the", 52, 1, COL_WHITE, false);
  drawCentered("falling bombs", 64, 1, COL_WHITE, false);

  // Credits — short enough to fit the 80px width
  drawCentered("By L. Kaplan", 90, 1, COL_BOMBHI, false);
  drawCentered("Port by HL2J", 102, 1, COL_BOMBHI, false);
  // Bold just the "2" in HL2J (string is 12 chars wide -> x=4, '2' at index 10)
  tft.setTextSize(1);
  tft.setTextColor(COL_BOMBHI);
  tft.setCursor(4 + 10 * 6, 102);     tft.print("2");
  tft.setCursor(4 + 10 * 6 + 1, 102); tft.print("2");

  // Start prompt anchored near the bottom
  drawCentered("Press button", 130, 1, COL_ACCENT, false);
  drawCentered("to start", 142, 1, COL_ACCENT, false);
}

void drawWaveClear() {
  tft.fillScreen(COL_BG);
  drawCentered("WAVE", 26, 2, COL_TITLE, true);
  drawCentered("CLEAR", 48, 2, COL_TITLE, true);

  int shownWave = (group > 8 ? 8 : group);   // group already advanced to the NEXT wave
  char buf[12];

  drawCentered("SCORE", 78, 1, COL_WHITE, false);
  snprintf(buf, sizeof(buf), "%d", score);
  drawCentered(buf, 90, 2, COL_ACCENT, true);   // full number, bold, never clipped

  snprintf(buf, sizeof(buf), "NEXT: W%d", shownWave);
  drawCentered(buf, 116, 1, COL_WHITE, false);

  drawCentered("Press", 134, 1, COL_ACCENT, false);
  drawCentered("button", 146, 1, COL_ACCENT, false);
}

void drawGameOver() {
  tft.fillScreen(COL_BG);
  drawCentered("GAME", 26, 2, COL_TITLE, true);
  drawCentered("OVER", 48, 2, COL_TITLE, true);

  drawCentered("SCORE", 82, 1, COL_WHITE, false);
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", score);
  drawCentered(buf, 94, 2, COL_ACCENT, true);   // full number, bold, never clipped

  drawCentered("Press", 122, 1, COL_ACCENT, false);
  drawCentered("button", 134, 1, COL_ACCENT, false);
  drawCentered("to play", 146, 1, COL_ACCENT, false);
}

void setup() {
  Serial.begin(115200);
  Serial.println("KABOOM! booting...");

  pinMode(BTN_PIN, INPUT);   // hardware pull-up + RC debounce on module
  pinMode(ENC_BTN, INPUT_PULLUP);   // encoder push switch (active-low)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  mySPI.begin(SCK, MISO, MOSI);
  tft.initR(INITR_MINI160x80);
  tft.setRotation(0);        // portrait: 80 wide x 160 tall

// BGR color order: color565(B, G, R)
  COL_BG     = tft.color565(8, 42, 8);        // very dark green playfield
  COL_WHITE  = tft.color565(255, 255, 255);   // pure white
  COL_GREY   = tft.color565(0, 0, 0);         // black (authentic bomb equator strip)
  COL_SKIN   = tft.color565(140, 175, 235);   // peach skin tone (the bomber's face)
  COL_BOMBER = tft.color565(40, 40, 130);     // dark red (the bomber's hatband)
  COL_BOMB   = tft.color565(150, 150, 150);   // grey (bomb body)
  COL_BOMBHI = tft.color565(200, 200, 200);   // lighter grey (glossy highlight)
  COL_FUSE   = tft.color565(30, 60, 100);     // dark brown fuse stem
  COL_SPARK  = tft.color565(210, 120, 210);   // pink/purple spark (authentic NTSC look)
  COL_BUCKET = tft.color565(40, 100, 190);    // orange/brown (authentic bucket color)
  COL_RIM    = tft.color565(230, 150, 60);    // bright blue (use this for the water inside!)
  COL_ACCENT = tft.color565(60, 200, 220);    // Atari score yellow
  COL_BOMBERBG = tft.color565(60, 60, 60);    // dark grey band behind the bomber
  COL_EXPO   = tft.color565(0, 140, 255);     // hot orange (detonation core / flash)
  COL_TITLE  = tft.color565(0, 85, 255);      // red-orange (KABOOM / GAME OVER titles)

  randomSeed(esp_random());
  lastEncPos = encoder.getPosition();

  Serial.println("Ready. Press the button to start.");
}

void loop() {
  encoder.tick();          // MUST run every iteration
  updateSound();

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
      digitalWrite(LED_PIN, LOW);
      buttonPressed();       // keep edge state fresh while playing
      updatePaddle();
      updatePlaying();
      if (millis() - lastDraw >= 33) {   // ~30fps (mandatory with audio + SPI)
        drawPlaying();
        lastDraw = millis();
      }
      break;

    case DYING:
      // Entity movement paused (no updatePaddle/updatePlaying). Strobe + explosions for 1.0s.
      digitalWrite(LED_PIN, LOW);
      updateDying();
      if (millis() - lastDraw >= 33) {
        drawDeathStrobe();
        lastDraw = millis();
      }
      break;

    case WAVE_CLEAR:
      // Between waves: the buckets stay live so you can reposition them, and the
      // next wave only starts on a button press. The wave-clear screen is opt-in —
      // it appears only when you press the rotary encoder.
      digitalWrite(LED_PIN, (millis() / 400) % 2);   // pulse "press me"
      if (showWaveScreen) {
        if (!screenDrawn) { drawWaveClear(); screenDrawn = true; }
      } else {
        updatePaddle();                              // keep the buckets movable
        if (millis() - lastDraw >= 33) {             // live ~30fps playfield
          drawPlaying();
          lastDraw = millis();
        }
      }
      // Encoder press reveals the wave-clear screen
      if (encButtonPressed() && !showWaveScreen) {
        showWaveScreen = true;
        screenDrawn = false;
      }
      // Tactile button launches the next wave immediately (no screen)
      if (buttonPressed()) {
        clearBombs();
        bomberX = 8;
        bomberDir = (lfsr & 1) ? 1 : -1;
        lastDrop = millis();
        showWaveScreen = false;
        sfxStart();
        digitalWrite(LED_PIN, LOW);
        screenDrawn = false;
        state = PLAYING;
        Serial.println("Next wave start!");
      }
      break;

    case GAME_OVER:
      if (!screenDrawn) { drawGameOver(); screenDrawn = true; }
      digitalWrite(LED_PIN, (millis() / 400) % 2);   // pulse "press me"
      if (buttonPressed()) {                          // one press starts a fresh round
        resetGame();
        sfxStart();
        digitalWrite(LED_PIN, LOW);
        state = PLAYING;
        Serial.println("New round start!");
      }
      break;
  }
}
