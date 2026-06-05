/*
 * 集中ポモドーロ物理タイマー (Arduino Uno)
 *
 * サーボの針が残り時間を物理表示し、終了時にブザーで知らせる。
 *   - 短押し  : 待機中=集中開始 / 動作中=ポーズ⇔再開
 *   - 長押し  : いつでも待機(IDLE)へリセット
 *   - サイクル: 集中(25分) → 休憩(5分) → 待機
 *
 * 配線:
 *   サーボ信号 = D9 / ブザー = D8 / ボタン = D2(INPUT_PULLUPでGND直結)
 */

#include <Servo.h>

// ===== ピン =====
const int PIN_SERVO  = 9;
const int PIN_BUZZER = 8;
const int PIN_BUTTON = 2;

// ===== テストモード =====
// true : 「分」を「秒」に置換(集中25秒/休憩5秒)。全体を素早く確認できる。
// false: 本番(集中25分/休憩5分)。
const bool TEST_MODE = true;

// ===== 時間設定 =====
const unsigned long FOCUS_UNITS = 25;  // 集中
const unsigned long BREAK_UNITS = 5;   // 休憩
const unsigned long UNIT_MS = TEST_MODE ? 1000UL : 60000UL;
const unsigned long FOCUS_MS = FOCUS_UNITS * UNIT_MS;
const unsigned long BREAK_MS = BREAK_UNITS * UNIT_MS;

// ===== サーボ角度(針) =====
const int ANGLE_FULL  = 180;  // 残り満タン
const int ANGLE_EMPTY = 0;    // 残りゼロ

// ===== ボタン判定 =====
const unsigned long DEBOUNCE_MS   = 30;
const unsigned long LONG_PRESS_MS = 1000;

// ===== その他 =====
const unsigned long LOG_INTERVAL_MS = 1000;
const long          BAUD_RATE       = 9600;

// ===== 状態 =====
enum State { IDLE_STATE, FOCUS_STATE, BREAK_STATE };
enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };
enum MelodyKind { MELODY_START, MELODY_FOCUS_END, MELODY_BREAK_END };

Servo servo;
State state = IDLE_STATE;
bool paused = false;
unsigned long phaseStartMs = 0;     // 現在区間の計測開始時刻
unsigned long accumulatedMs = 0;    // ポーズをまたいで積算した経過
unsigned long phaseDurationMs = 0;  // 現在区間の長さ

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  servo.attach(PIN_SERVO);
  enterState(IDLE_STATE);
}

void loop() {
  ButtonEvent ev = readButton();

  if (ev == BTN_LONG) {
    Serial.println(F("RESET"));
    enterState(IDLE_STATE);
    return;
  }

  switch (state) {
    case IDLE_STATE:
      if (ev == BTN_SHORT) enterState(FOCUS_STATE);
      break;
    case FOCUS_STATE:
    case BREAK_STATE:
      if (ev == BTN_SHORT) togglePause();
      runCountdown();
      break;
  }
}

// 状態遷移時の初期化
void enterState(State next) {
  state = next;
  paused = false;
  accumulatedMs = 0;
  phaseStartMs = millis();

  switch (next) {
    case IDLE_STATE:
      servo.write(ANGLE_FULL);
      Serial.println(F("STATE: IDLE (press to start focus)"));
      break;
    case FOCUS_STATE:
      phaseDurationMs = FOCUS_MS;
      servo.write(ANGLE_FULL);
      playMelody(MELODY_START);
      Serial.println(F("STATE: FOCUS start"));
      break;
    case BREAK_STATE:
      phaseDurationMs = BREAK_MS;
      servo.write(ANGLE_FULL);
      Serial.println(F("STATE: BREAK start"));
      break;
  }
}

// 経過を進め、針を更新し、区間終了を処理する
void runCountdown() {
  unsigned long now = millis();
  unsigned long elapsed = accumulatedMs + (paused ? 0 : now - phaseStartMs);
  if (elapsed > phaseDurationMs) elapsed = phaseDurationMs;
  unsigned long remaining = phaseDurationMs - elapsed;

  updateNeedle(remaining, phaseDurationMs);
  logRemaining(remaining);

  if (remaining == 0) {
    if (state == FOCUS_STATE) {
      playMelody(MELODY_FOCUS_END);
      enterState(BREAK_STATE);
    } else {
      playMelody(MELODY_BREAK_END);
      enterState(IDLE_STATE);
    }
  }
}

// ポーズ⇔再開
void togglePause() {
  unsigned long now = millis();
  if (!paused) {
    accumulatedMs += now - phaseStartMs;
    paused = true;
    Serial.println(F("PAUSED"));
  } else {
    phaseStartMs = now;
    paused = false;
    Serial.println(F("RESUMED"));
  }
}

// 残り時間を角度に変換して針を動かす
void updateNeedle(unsigned long remaining, unsigned long total) {
  long angle = map((long)remaining, 0, (long)total, ANGLE_EMPTY, ANGLE_FULL);
  servo.write((int)angle);
}

// 1秒ごとに残り秒数をシリアル出力
void logRemaining(unsigned long remaining) {
  static unsigned long lastLog = 0;
  unsigned long now = millis();
  if (now - lastLog >= LOG_INTERVAL_MS) {
    lastLog = now;
    Serial.print(F("remaining(s): "));
    Serial.println(remaining / 1000);
  }
}

// 短押し / 長押しを検出(デバウンス付き)
ButtonEvent readButton() {
  static bool stablePressed = false;
  static bool lastReading = false;
  static unsigned long lastChange = 0;
  static unsigned long pressStart = 0;
  static bool longFired = false;

  bool reading = (digitalRead(PIN_BUTTON) == LOW);  // PULLUP: 押下=LOW
  unsigned long now = millis();

  if (reading != lastReading) {
    lastReading = reading;
    lastChange = now;
  }

  ButtonEvent event = BTN_NONE;
  if (now - lastChange > DEBOUNCE_MS && reading != stablePressed) {
    stablePressed = reading;
    if (stablePressed) {
      pressStart = now;
      longFired = false;
    } else if (!longFired) {
      event = BTN_SHORT;  // 離した瞬間に短押し確定
    }
  }

  if (stablePressed && !longFired && (now - pressStart > LONG_PRESS_MS)) {
    longFired = true;
    event = BTN_LONG;  // 押しっぱなしで長押し確定
  }

  return event;
}

// ブザーのメロディ再生(状態遷移時のみの短い音)
void playMelody(MelodyKind kind) {
  switch (kind) {
    case MELODY_START:
      playTone(880, 120);
      break;
    case MELODY_FOCUS_END:  // 上昇音「休憩して！」
      playTone(660, 150);
      playTone(880, 150);
      playTone(1175, 300);
      break;
    case MELODY_BREAK_END:  // 下降音「再開！」
      playTone(880, 150);
      playTone(660, 150);
      playTone(523, 300);
      break;
  }
}

void playTone(int freq, int durMs) {
  tone(PIN_BUZZER, freq, durMs);
  delay(durMs + 30);
  noTone(PIN_BUZZER);
}
