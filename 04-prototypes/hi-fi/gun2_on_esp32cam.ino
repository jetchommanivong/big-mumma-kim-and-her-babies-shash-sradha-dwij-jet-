// GUN 2 on an ESP32-CAM board used as a plain ESP32 (no camera, no SD card needed).
// Fires the IR LED (and laser) when the trigger is pulled.
//
// Board: Tools > Board > "AI Thinker ESP32-CAM"   (upload through the MB base)
//
// >>> SET THIS FOR EACH GUN <<<   Gun 1 = 1 (800us pulses)   Gun 2 = 2 (1600us pulses)
const int GUN_ID = 2;
//
// Wiring (ESP32-CAM pins):
//   IO12 -> 1k resistor -> BC337 middle leg (B)
//   5V   -> 33 ohm -> IR LED long leg;  IR LED short leg -> BC337 left leg (C)
//   IO13 -> splice: trigger side A + sound board yellow
//   IO14 -> laser red wire
//   3V3  -> sound board + wire
//   GND  -> splice: trigger side B + BC337 right leg (E) + laser blue wire
// Keep the trigger OFF IO12: if IO12 is held HIGH at power-up the board won't boot.

const int IR_LED_PIN  = 12;   // IO12
const int TRIGGER_PIN = 13;   // IO13
const int LASER_PIN   = 14;   // IO14
const int STATUS_LED  = 33;   // small red LED on the back (LOW = on)

// Laser behaviour: LASER_ALWAYS (aiming sight), LASER_HELD (while trigger held), LASER_FLASH (flash per shot)
enum LaserMode { LASER_ALWAYS, LASER_HELD, LASER_FLASH };
const LaserMode LASER_MODE = LASER_ALWAYS;
const unsigned long LASER_FLASH_MS = 150;
unsigned long laserOffAt = 0;

const int CARRIER_HZ = 38000;           // what the receiver listens for
const int PULSES = 20;                  // pulses per shot (target needs MIN_PULSES of these)
const int PULSE_US = (GUN_ID == 1) ? 800 : 1600;   // pulse length = this gun's ID signature
const unsigned long DEBOUNCE_MS = 20;
const unsigned long COOLDOWN_MS = 250;  // minimum time between shots

void irOn() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(IR_LED_PIN, 128);
#else
  ledcWrite(0, 128);
#endif
}

void irOff() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(IR_LED_PIN, 0);
#else
  ledcWrite(0, 0);
#endif
}

void fireShot() {
  for (int i = 0; i < PULSES; i++) {
    irOn();
    delayMicroseconds(PULSE_US);
    irOff();
    delayMicroseconds(PULSE_US);
  }
}

void setup() {
  pinMode(IR_LED_PIN, OUTPUT);       // IR off while starting up
  digitalWrite(IR_LED_PIN, LOW);
  Serial.begin(115200);
  delay(300);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(IR_LED_PIN, CARRIER_HZ, 8);
#else
  ledcSetup(0, CARRIER_HZ, 8);
  ledcAttachPin(IR_LED_PIN, 0);
#endif
  irOff();

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);   // red LED off
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LASER_MODE == LASER_ALWAYS ? HIGH : LOW);

  Serial.printf("=== GUN %d ready: pull the trigger ===\n", GUN_ID);
}

int lastReading = HIGH;
int stableState = HIGH;
unsigned long lastChange = 0;
unsigned long lastShot = 0;

void loop() {
  int reading = digitalRead(TRIGGER_PIN);
  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }

  if (millis() - lastChange > DEBOUNCE_MS && reading != stableState) {
    stableState = reading;

    if (LASER_MODE == LASER_HELD) {
      digitalWrite(LASER_PIN, stableState == LOW ? HIGH : LOW);
    }

    // Trigger pulled (HIGH -> LOW): fire one shot
    if (stableState == LOW && millis() - lastShot > COOLDOWN_MS) {
      lastShot = millis();
      if (LASER_MODE == LASER_FLASH) {
        digitalWrite(LASER_PIN, HIGH);
        laserOffAt = millis() + LASER_FLASH_MS;
      }
      digitalWrite(STATUS_LED, LOW);    // red LED on while firing
      fireShot();
      digitalWrite(STATUS_LED, HIGH);
      Serial.printf("Gun %d shot fired\n", GUN_ID);
    }
  }

  if (LASER_MODE == LASER_FLASH && laserOffAt != 0 && millis() > laserOffAt) {
    digitalWrite(LASER_PIN, LOW);
    laserOffAt = 0;
  }
}
