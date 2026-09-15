# 1. ESP32 + LDR/Photoresistor test

## Technology

Light increases conductivity -> resistance decreases with light. When the laser is pointed toward the photoresistor, it will passthrough a higher amount of current which will result in a higher analog output signal.

## Setup

- 3.3V Power: Connect one leg of the LDR to the 3.3V pin on the ESP32.
- The Junction (Signal): Connect the second leg of the LDR to a row on the breadboard. From that same row, run a jumper wire to an analog-capable pin on the ESP32, such as GPIO 34
- Resistor to Ground: In that same breadboard row, connect one end of the 10kΩ resistor. Connect the other end of the 10kΩ resistor to a GND (ground) pin on the ESP32.

## Results

The difference between a regular light and the laser light is not too large; this may be problematic if outdoor light inteferes. The test was also done at close range and although the laser is a very.. concentrated? sharp? I can't think of the word.. light, the distance factor mayyy weaken the beam (although the beam used seems great!) but yeh... it wasn't too bad, but a photodiode may be better. Oh well.. more testing for us I guess..

## Code

The code, here you may find: [code](LDR_Initial.ino)
