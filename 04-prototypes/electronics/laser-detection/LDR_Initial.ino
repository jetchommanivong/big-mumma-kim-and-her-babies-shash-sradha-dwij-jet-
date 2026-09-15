const int ldrAnalogPin = 34;  // Analog pin connected to the voltage divider


void setup() {
  // Initiate sereial comms
  Serial.begin(115200);

  Serial.println("ESP32 LDR Sensor Reading Initialised...");
}

void loop() {
  int analogValue = analogRead(ldrAnalogPin);

  // Print the raw results to the Serial Monitor
  Serial.print("Analog Level: ");
  Serial.println(analogValue);

  delay(500); 

}
