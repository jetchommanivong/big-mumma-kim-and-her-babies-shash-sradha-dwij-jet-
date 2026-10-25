// TARGET + SERVO (two-gun version): watches the IR receiver(s). On a hit it:
//   1. works out which gun hit it and tells THAT gun's ESP32-CAM to take a photo ("HIT1"/"HIT2")
//   2. moves the servo to press the remote's button, then lets go
//
// Board: ESP32 Dev Module
// Library needed: "ESP32Servo" by Kevin Harrington (Sketch > Include Library > Manage Libraries)
//
// Wiring:
//   Receiver OUT -> 15      Receiver VCC -> 3.3V      Receiver GND -> GND
//   Servo signal (orange/yellow) -> 13
//   Servo power  (red)           -> 5V
//   Servo ground (brown/black)   -> GND
//   Optional: 470uF capacitor across servo 5V and GND (stripe to GND) to stop resets

#include <WiFi.h>
#include <esp_now.h>
#include <ESP32Servo.h>

const int IR_RX_PIN  = 15;
const int STATUS_LED = 2;
const int SERVO_PIN  = 13;

// ---- Servo settings: tune these so it just presses the button ----
const int REST_ANGLE  = 90;    // arm resting, not touching the button
const int PRESS_ANGLE = 60;    // arm pressing the button (try 90 -> 70 -> 60 ... until it clicks)
const int PRESS_MS    = 300;   // how long to hold the button down
// Minimum time between button presses, even if hit again sooner.
const unsigned long SERVO_COOLDOWN_MS = 3000;

// ---- Hit detection ----
const int MIN_PULSES = 8;                    // out of the gun's 20 pulses
const unsigned long HIT_COOLDOWN_MS = 500;
const unsigned long GUN_SPLIT_US = 1200;     // pulses shorter than this = gun 1, longer = gun 2

uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned long lastHit = 0;
unsigned long lastPress = 0;
Servo pressServo;

// Counts valid gun pulses and works out WHICH gun fired from the pulse length.
//   Gun 1 pulses ~800us, Gun 2 pulses ~1600us. Average under 1200us = gun 1.
int countPulses(int &gunId) {
  int valid = 0;
  unsigned long totalLow = 0;
  for (int i = 0; i < 40; i++) {
    unsigned long t = micros();
    bool stuck = false;
    while (digitalRead(IR_RX_PIN) == LOW) {
      if (micros() - t > 4000) { stuck = true; break; }
    }
    if (stuck) break;
    unsigned long lowLen = micros() - t;
    if (lowLen >= 300 && lowLen <= 2400) { valid++; totalLow += lowLen; }
    t = micros();
    bool ended = false;
    while (digitalRead(IR_RX_PIN) == HIGH) {
      if (micros() - t > 4000) { ended = true; break; }
    }
    if (ended) break;
  }
  gunId = 0;
  if (valid > 0) gunId = (totalLow / valid < GUN_SPLIT_US) ? 1 : 2;
  return valid;
}

void waitForQuiet() {
  unsigned long start = millis();
  unsigned long quietSince = micros();
  while (millis() - start < 100) {
    if (digitalRead(IR_RX_PIN) == LOW) quietSince = micros();
    if (micros() - quietSince > 5000) return;
  }
}

void pressButton() {
  if (millis() - lastPress < SERVO_COOLDOWN_MS && lastPress != 0) {
    Serial.println("  (servo cooling down, not pressed)");
    return;
  }
  lastPress = millis();
  pressServo.write(PRESS_ANGLE);
  delay(PRESS_MS);
  pressServo.write(REST_ANGLE);
  Serial.println("  servo pressed");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(IR_RX_PIN, INPUT);
  pinMode(STATUS_LED, OUTPUT);

  pressServo.setPeriodHertz(50);
  pressServo.attach(SERVO_PIN, 500, 2400);
  pressServo.write(REST_ANGLE);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
  } else {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  if (digitalRead(IR_RX_PIN) == HIGH) Serial.println("Receiver OK");
  else Serial.println("WARNING: receiver OUT is LOW. Check VCC/GND and OUT -> 15.");
  Serial.println("=== TARGET + SERVO ready (2-gun mode): shoot me ===");
}

void loop() {
  if (digitalRead(IR_RX_PIN) == LOW) {
    int gunId = 0;
    int pulses = countPulses(gunId);
    if (pulses >= MIN_PULSES && millis() - lastHit > HIT_COOLDOWN_MS) {
      lastHit = millis();
      char msg[5] = {'H', 'I', 'T', (char)('0' + gunId), 0};   // "HIT1" or "HIT2"
      esp_now_send(BROADCAST, (const uint8_t*)msg, 4);          // photo first, so the moment is captured
      Serial.printf("HIT by GUN %d! (%d pulses) -> photo requested\n", gunId, pulses);
      digitalWrite(STATUS_LED, HIGH);
      pressButton();
      digitalWrite(STATUS_LED, LOW);
    } else if (pulses > 0) {
      Serial.printf("Ignored (%d pulses)\n", pulses);
    }
    waitForQuiet();
  }
}
