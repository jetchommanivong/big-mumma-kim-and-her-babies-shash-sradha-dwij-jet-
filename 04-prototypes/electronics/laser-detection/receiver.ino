#define IR_RECEIVER_PIN 5

void setup() {
  Serial.begin(115200);
  pinMode(IR_RECEIVER_PIN, INPUT);
}

void loop() {
  if (digitalRead(IR_RECEIVER_PIN) == LOW) {
    Serial.println("HIT!");
    delay(200); // debounce
  }
  Serial.println("IDLE");
  delay(200);
}
