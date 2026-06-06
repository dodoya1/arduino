/*
 * 振り子メトロノーム (Arduino Uno)
 *
 * サーボの針が本物の振り子のように左右へ滑らかに往復し、
 * 1拍ごとにブザーが鳴る。小節の頭(1拍目)だけ高いアクセント音。
 *
 *   - 短押し  : タップテンポ(叩いた間隔から BPM を自動算出)
 *   - 長押し  : 再生 / 停止
 *
 * 配線(ポモドーロタイマーと共通):
 *   サーボ信号 = D9 / ブザー = D8 / ボタン = D2(INPUT_PULLUPでGND直結)
 */

#include <Servo.h>

// ===== ピン =====
const int PIN_SERVO  = 9;
const int PIN_BUZZER = 8;
const int PIN_BUTTON = 2;

// ===== テンポ =====
const int DEFAULT_BPM = 100;
const int MIN_BPM     = 40;
const int MAX_BPM     = 240;

// ===== 拍子 =====
const int BEATS_PER_BAR = 4;  // 4/4拍子(1拍目をアクセント)

// ===== 振り子(サーボ角度) =====
const int SWING_MIN_ANGLE = 50;   // 左端
const int SWING_MAX_ANGLE = 130;  // 右端
const int REST_ANGLE      = 90;   // 停止中の中央位置

// ===== 音 =====
const int ACCENT_FREQ      = 1760;  // 1拍目(高い)
const int BEAT_FREQ        = 880;   // 2拍目以降(低い)
const int TICK_DURATION_MS = 40;    // クリック音の長さ

// ===== タップテンポ =====
const int           TAP_AVERAGE_COUNT = 4;  // 直近何回の間隔を平均するか
const unsigned long MAX_TAP_GAP_MS    = 60000UL / MIN_BPM;  // これ以上空いたら測り直し

// ===== ボタン判定 =====
const unsigned long DEBOUNCE_MS   = 30;
const unsigned long LONG_PRESS_MS = 1000;

// ===== その他 =====
const long BAUD_RATE = 9600;

enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };

Servo servo;

bool          playing       = true;
int           bpm           = DEFAULT_BPM;
unsigned long beatIntervalMs = 60000UL / DEFAULT_BPM;
unsigned long beatStartMs    = 0;     // 現在の拍が始まった時刻
int           beatIndex      = 1;     // 1..BEATS_PER_BAR
bool          swingToRight   = true;  // 今の拍で針が右へ向かうか

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  servo.attach(PIN_SERVO);

  beatStartMs = millis();
  servo.write(SWING_MIN_ANGLE);
  Serial.print(F("Metronome start: "));
  Serial.print(bpm);
  Serial.println(F(" BPM"));
  playTick(beatIndex);  // 起動の1拍目を鳴らす
}

void loop() {
  ButtonEvent ev = readButton();

  if (ev == BTN_LONG) {
    togglePlaying();
  } else if (ev == BTN_SHORT) {
    tapTempo();
  }

  if (playing) {
    updateBeat();
    updatePendulum();
  }
}

// 拍の境界を検出し、音とカウントを進める
void updateBeat() {
  unsigned long now = millis();
  if (now - beatStartMs >= beatIntervalMs) {
    beatStartMs += beatIntervalMs;
    if (now - beatStartMs >= beatIntervalMs) beatStartMs = now;  // 遅延しすぎたら追従

    beatIndex = (beatIndex % BEATS_PER_BAR) + 1;
    swingToRight = !swingToRight;
    playTick(beatIndex);

    Serial.print(F("beat "));
    Serial.print(beatIndex);
    Serial.print(F("/"));
    Serial.println(BEATS_PER_BAR);
  }
}

// 拍の中での進み具合に応じて針を振る(端で減速する自然な動き)
void updatePendulum() {
  unsigned long now = millis();
  float progress = (float)(now - beatStartMs) / (float)beatIntervalMs;
  if (progress < 0.0) progress = 0.0;
  if (progress > 1.0) progress = 1.0;

  float eased = (1.0 - cos(progress * PI)) / 2.0;  // 0→1 のイージング
  int range = SWING_MAX_ANGLE - SWING_MIN_ANGLE;
  int angle = swingToRight
                ? SWING_MIN_ANGLE + (int)(range * eased)
                : SWING_MAX_ANGLE - (int)(range * eased);
  servo.write(angle);
}

// 再生 / 停止の切替
void togglePlaying() {
  playing = !playing;
  if (playing) {
    beatStartMs = millis();
    beatIndex = 1;
    swingToRight = true;
    Serial.println(F("PLAY"));
    playTick(beatIndex);
  } else {
    servo.write(REST_ANGLE);
    noTone(PIN_BUZZER);
    Serial.println(F("STOP"));
  }
}

// 叩いたリズムの間隔から BPM を決める
void tapTempo() {
  static unsigned long lastTap = 0;
  static unsigned long intervals[TAP_AVERAGE_COUNT];
  static int           count = 0;

  unsigned long now = millis();

  if (lastTap == 0 || now - lastTap > MAX_TAP_GAP_MS) {
    count = 0;  // 初回 or 間が空きすぎ → 測り直し
  } else {
    // 直近の間隔をリングバッファに記録して平均する
    for (int i = TAP_AVERAGE_COUNT - 1; i > 0; i--) intervals[i] = intervals[i - 1];
    intervals[0] = now - lastTap;
    if (count < TAP_AVERAGE_COUNT) count++;

    unsigned long sum = 0;
    for (int i = 0; i < count; i++) sum += intervals[i];
    unsigned long avg = sum / count;

    setBpm((int)(60000UL / avg));
  }
  lastTap = now;
}

// BPM を範囲内に収めて反映
void setBpm(int newBpm) {
  if (newBpm < MIN_BPM) newBpm = MIN_BPM;
  if (newBpm > MAX_BPM) newBpm = MAX_BPM;
  bpm = newBpm;
  beatIntervalMs = 60000UL / bpm;
  Serial.print(F("tempo -> "));
  Serial.print(bpm);
  Serial.println(F(" BPM"));
}

// 拍のクリック音(1拍目はアクセント)
void playTick(int beat) {
  int freq = (beat == 1) ? ACCENT_FREQ : BEAT_FREQ;
  tone(PIN_BUZZER, freq, TICK_DURATION_MS);  // 非ブロッキング
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
