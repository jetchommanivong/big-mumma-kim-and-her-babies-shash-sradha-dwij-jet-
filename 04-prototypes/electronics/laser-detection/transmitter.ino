#define IR_PIN 32

void setup() {
  ledcAttach(IR_PIN, 38000, 8);   // pin, freq, resolution — no channel number needed
  ledcWrite(IR_PIN, 128);          // write using the pin, not a channel
}

void loop() {
  ledcWrite(0, 128);           // LED on, buzzing at 38kHz
  delay(500);
  ledcWrite(0, 0);             // LED off
  delay(500);
}
