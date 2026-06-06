/*
 * 踊るオルゴール / ジュークボックス (Arduino Uno)
 *
 * 内蔵した曲をブザーで単音演奏し、針が音の高さに合わせて踊る。
 * 収録曲はパブリックドメインの名曲のみ。
 *
 *   - 短押し  : 次の曲へ送って頭から再生
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

// ===== 音程(Hz) =====
const int REST    = 0;
const int NOTE_C4 = 262, NOTE_D4 = 294, NOTE_E4 = 330, NOTE_F4 = 349;
const int NOTE_G4 = 392, NOTE_A4 = 440, NOTE_B4 = 494;
const int NOTE_C5 = 523, NOTE_D5 = 587, NOTE_E5 = 659;

// 踊り(角度マッピング)の基準となる音域
const int DANCE_FREQ_LOW  = NOTE_C4;
const int DANCE_FREQ_HIGH = NOTE_C5;

// ===== 針(サーボ角度) =====
const int SWING_MIN_ANGLE = 45;   // 低い音
const int SWING_MAX_ANGLE = 135;  // 高い音
const int REST_ANGLE      = 90;   // 停止中の中央
const int SERVO_STEP_DEG  = 4;    // 1回の更新で動く角度
const unsigned long SERVO_STEP_MS = 12;  // 角度更新の間隔

// ===== 発音 =====
const float TONE_GATE = 0.9;  // 音長の何割を鳴らすか(残りは音の切れ目)

// ===== ボタン判定 =====
const unsigned long DEBOUNCE_MS   = 30;
const unsigned long LONG_PRESS_MS = 1000;

// ===== その他 =====
const long BAUD_RATE = 9600;

enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };

// 1音 = {音程Hz, 長さ(8分音符を1とした単位)}
struct Note { int freq; byte len; };

// ----- 収録曲(すべてパブリックドメイン) -----

// きらきら星
const Note SONG_TWINKLE[] = {
  {NOTE_C4,2},{NOTE_C4,2},{NOTE_G4,2},{NOTE_G4,2},{NOTE_A4,2},{NOTE_A4,2},{NOTE_G4,4},
  {NOTE_F4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_D4,2},{NOTE_D4,2},{NOTE_C4,4},
  {NOTE_G4,2},{NOTE_G4,2},{NOTE_F4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_D4,4},
  {NOTE_G4,2},{NOTE_G4,2},{NOTE_F4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_D4,4},
  {NOTE_C4,2},{NOTE_C4,2},{NOTE_G4,2},{NOTE_G4,2},{NOTE_A4,2},{NOTE_A4,2},{NOTE_G4,4},
  {NOTE_F4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_D4,2},{NOTE_D4,2},{NOTE_C4,4},
};

// 歓喜の歌(ベートーヴェン 第九)
const Note SONG_ODE[] = {
  {NOTE_E4,2},{NOTE_E4,2},{NOTE_F4,2},{NOTE_G4,2},{NOTE_G4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_D4,2},
  {NOTE_C4,2},{NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,2},{NOTE_E4,3},{NOTE_D4,1},{NOTE_D4,4},
  {NOTE_E4,2},{NOTE_E4,2},{NOTE_F4,2},{NOTE_G4,2},{NOTE_G4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_D4,2},
  {NOTE_C4,2},{NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,2},{NOTE_D4,3},{NOTE_C4,1},{NOTE_C4,4},
};

// かえるの合唱
const Note SONG_FROG[] = {
  {NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_D4,2},{NOTE_C4,2},{REST,2},
  {NOTE_E4,2},{NOTE_F4,2},{NOTE_G4,2},{NOTE_A4,2},{NOTE_G4,2},{NOTE_F4,2},{NOTE_E4,2},{REST,2},
  {NOTE_C4,1},{NOTE_C4,1},{NOTE_D4,1},{NOTE_D4,1},{NOTE_E4,1},{NOTE_E4,1},{NOTE_F4,1},{NOTE_F4,1},
  {NOTE_E4,2},{NOTE_D4,2},{NOTE_C4,4},
};

// メリーさんのひつじ
const Note SONG_MARY[] = {
  {NOTE_E4,2},{NOTE_D4,2},{NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_E4,4},
  {NOTE_D4,2},{NOTE_D4,2},{NOTE_D4,4},{NOTE_E4,2},{NOTE_G4,2},{NOTE_G4,4},
  {NOTE_E4,2},{NOTE_D4,2},{NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_E4,2},
  {NOTE_D4,2},{NOTE_D4,2},{NOTE_E4,2},{NOTE_D4,2},{NOTE_C4,4},
};

// ジングルベル(サビ)
const Note SONG_JINGLE[] = {
  {NOTE_E4,2},{NOTE_E4,2},{NOTE_E4,4},{NOTE_E4,2},{NOTE_E4,2},{NOTE_E4,4},
  {NOTE_E4,2},{NOTE_G4,2},{NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,8},
  {NOTE_F4,2},{NOTE_F4,2},{NOTE_F4,2},{NOTE_F4,2},{NOTE_F4,2},{NOTE_E4,2},{NOTE_E4,2},{NOTE_E4,1},{NOTE_E4,1},
  {NOTE_G4,2},{NOTE_G4,2},{NOTE_F4,2},{NOTE_D4,2},{NOTE_C4,8},
};

// ちょうちょう
const Note SONG_BUTTERFLY[] = {
  {NOTE_G4,2},{NOTE_E4,2},{NOTE_E4,4},{NOTE_F4,2},{NOTE_D4,2},{NOTE_D4,4},
  {NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,2},{NOTE_F4,2},{NOTE_G4,2},{NOTE_G4,2},{NOTE_G4,4},
  {NOTE_G4,2},{NOTE_E4,2},{NOTE_E4,4},{NOTE_F4,2},{NOTE_D4,2},{NOTE_D4,4},
  {NOTE_C4,2},{NOTE_E4,2},{NOTE_G4,2},{NOTE_G4,2},{NOTE_E4,4},
};

// 蛍の光(オールド・ラング・サイン)
const Note SONG_AULD[] = {
  {NOTE_G4,2},{NOTE_C4,3},{NOTE_C4,1},{NOTE_C4,2},{NOTE_E4,2},{NOTE_D4,3},{NOTE_C4,1},{NOTE_D4,2},{NOTE_E4,2},
  {NOTE_C4,3},{NOTE_C4,1},{NOTE_E4,2},{NOTE_G4,2},{NOTE_A4,6},
  {NOTE_A4,2},{NOTE_G4,2},{NOTE_E4,3},{NOTE_C4,1},{NOTE_C4,2},{NOTE_D4,2},{NOTE_E4,3},{NOTE_D4,1},{NOTE_C4,2},{NOTE_D4,2},{NOTE_C4,4},
};

struct Song { const char* name; const Note* notes; int count; int eighthMs; };

const Song SONGS[] = {
  { "Twinkle Twinkle Little Star", SONG_TWINKLE,   sizeof(SONG_TWINKLE)   / sizeof(Note), 220 },
  { "Ode to Joy",                  SONG_ODE,       sizeof(SONG_ODE)       / sizeof(Note), 200 },
  { "Frog Song",                   SONG_FROG,      sizeof(SONG_FROG)      / sizeof(Note), 220 },
  { "Mary Had a Little Lamb",      SONG_MARY,      sizeof(SONG_MARY)      / sizeof(Note), 200 },
  { "Jingle Bells",                SONG_JINGLE,    sizeof(SONG_JINGLE)    / sizeof(Note), 180 },
  { "Butterfly (Choucho)",         SONG_BUTTERFLY, sizeof(SONG_BUTTERFLY) / sizeof(Note), 220 },
  { "Auld Lang Syne (Hotaru)",     SONG_AULD,      sizeof(SONG_AULD)      / sizeof(Note), 240 },
};
const int NUM_SONGS = sizeof(SONGS) / sizeof(Song);

Servo servo;

bool          playing      = true;
int           currentSong  = 0;
int           noteIndex    = 0;
unsigned long noteStartMs  = 0;
unsigned long currentNoteMs = 0;
int           targetAngle  = REST_ANGLE;
int           currentAngle = REST_ANGLE;

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  servo.attach(PIN_SERVO);
  servo.write(currentAngle);
  startSong(0);
}

void loop() {
  ButtonEvent ev = readButton();

  if (ev == BTN_LONG) {
    togglePlaying();
  } else if (ev == BTN_SHORT) {
    nextSong();
  }

  if (playing) advanceSequencer();
  updateServo();
}

// 曲を頭から再生開始
void startSong(int index) {
  currentSong = index;
  noteIndex = 0;
  playing = true;
  Serial.print(F("PLAY: "));
  Serial.println(SONGS[currentSong].name);
  playCurrentNote();
}

// 次の曲へ
void nextSong() {
  startSong((currentSong + 1) % NUM_SONGS);
}

// 再生 / 停止
void togglePlaying() {
  if (playing) {
    playing = false;
    noTone(PIN_BUZZER);
    targetAngle = REST_ANGLE;
    Serial.println(F("STOP"));
  } else {
    startSong(currentSong);
  }
}

// 現在の音を鳴らし、針の目標角度と音長を決める
void playCurrentNote() {
  Note n = SONGS[currentSong].notes[noteIndex];
  currentNoteMs = (unsigned long)n.len * SONGS[currentSong].eighthMs;
  noteStartMs = millis();

  if (n.freq > REST) {
    tone(PIN_BUZZER, n.freq, (unsigned long)(currentNoteMs * TONE_GATE));
    targetAngle = freqToAngle(n.freq);
  }
  // 休符のときは音を出さず、針の目標はそのまま保つ
}

// 音長が経過したら次の音へ。曲末で停止。
void advanceSequencer() {
  if (millis() - noteStartMs < currentNoteMs) return;

  noteIndex++;
  if (noteIndex >= SONGS[currentSong].count) {
    playing = false;
    noTone(PIN_BUZZER);
    targetAngle = REST_ANGLE;
    Serial.println(F("DONE"));
    return;
  }
  playCurrentNote();
}

// 音の高さを針の角度へ
int freqToAngle(int freq) {
  int angle = map(freq, DANCE_FREQ_LOW, DANCE_FREQ_HIGH, SWING_MIN_ANGLE, SWING_MAX_ANGLE);
  return constrain(angle, SWING_MIN_ANGLE, SWING_MAX_ANGLE);
}

// 目標角度へ滑らかに近づける(ノンブロッキング)
void updateServo() {
  static unsigned long lastStep = 0;
  unsigned long now = millis();
  if (now - lastStep < SERVO_STEP_MS) return;
  lastStep = now;

  if (currentAngle < targetAngle) {
    currentAngle += min(SERVO_STEP_DEG, targetAngle - currentAngle);
  } else if (currentAngle > targetAngle) {
    currentAngle -= min(SERVO_STEP_DEG, currentAngle - targetAngle);
  }
  servo.write(currentAngle);
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
