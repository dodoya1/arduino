/*
 * サーボ単体テスト (Arduino Uno)
 *
 * 目的: ポモドーロ本体のロジックを切り離し、
 *       「サーボ + 配線 + 電源」だけが正常かを確認する。
 *
 * 配線: サーボ信号=D9 / 赤線=5V(赤レール) / 茶線=GND(青レール)
 *
 * 期待動作: 書き込み後、サーボが 0° ⇔ 180° を約1秒ごとに往復し続ける。
 *           シリアルモニタ(9600)に現在の角度が出る。
 */

#include <Servo.h>

const int PIN_SERVO = 9;
Servo servo;

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== Servo test start ==="));
  servo.attach(PIN_SERVO);
}

void loop() {
  Serial.println(F("move -> 0 deg"));
  servo.write(0);
  delay(1000);

  Serial.println(F("move -> 90 deg"));
  servo.write(90);
  delay(1000);

  Serial.println(F("move -> 180 deg"));
  servo.write(180);
  delay(1000);
}
