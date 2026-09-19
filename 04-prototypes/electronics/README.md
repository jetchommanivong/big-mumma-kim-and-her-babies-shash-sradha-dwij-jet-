# Design Process Overview (Electronics)
Here lies the electronics vault - dangerous, dangerous place... Beware.. you have been warned..

## 1. Laser Pen Thing (Transmitter) + Light Dependant Resistor (LDR)/Photoresistor (Receiver) test

### Technology

Light increases conductivity -> resistance decreases with light. When the laser is pointed toward the photoresistor, it will passthrough a higher amount of current which will result in a higher analog output signal.

### Setup

- First LED leg → 3.3V pin on ESP32.
- Second LED leg → analog-capable pin on ESP32 (e.g. GPIO 34)
- Second LED leg → 10kΩ resistor. Connect the other end of the 10kΩ resistor to GND on ESP32.

### Results

The difference between a regular light and the laser light is not too large; this may be problematic if outdoor light inteferes. The test was also done at close range and although the laser is a very.. concentrated? sharp? I can't think of the word.. light, the distance factor mayyy weaken the beam (although the beam used seems great!) but yeh... it wasn't too bad, but a photodiode may be better. Oh well.. more testing for us I guess..

### Code

The code, here you may find: [code](laser-detection/ldr.ino)


## 2. ZD1952 IR Receiver (Receiver) + ZD1945 IR Transmitter (Transmitter) test

### Technology
This is how TV/roller gates work. 
### About

ZD1952:
- Center Frequency (frequency of when the receiver "hears" the IR light blinking and tunes out everything else (steady ambient light, other flicker rates, noise)): 37.9kHz
- 12m range typical — plenty for indoor laser tag.
- 42° horizontal / 37° vertical field of view — nice and forgiving for a vest sensor
- Documentation: [Datasheet](https://media.jaycar.com.au/product/resources/ZD1952_datasheetMain_41535.pdf?_gl=1*1404h4h*_gcl_au*MjEwOTkxMzQ2LjE3ODg4Mzc1MjUuLS4tLjE3ODg4Mzc3NzQuMzIwNzE1MjY5LjE3ODg4Mzc3NzQuMTc4ODgzODA1Nw..)

ZD1945:
- Wavelength: 940nm peak spectral (invisible IR — matches your ZD1952 receiver perfectly)
- Viewing angle: 30° — moderately narrow, good "aimed shot" feel rather than floodlight
- Operating current: rated/tested at 20mA continuous
- Input voltage: regulated with a resistor (or transistor which is better) -> if 5V: 220 Ohm or more; if 3.3V: 100 Ohm or more.
- Documentation: [Datasheet](https://media.jaycar.com.au/product/resources/ZD1945_datasheetMain_41161.pdf?_gl=1*13bzrdf*_gcl_au*MjEwOTkxMzQ2LjE3ODg4Mzc1MjUuLS4tLjE3ODg4Mzc3NzQuMzIwNzE1MjY5LjE3ODg4Mzc3NzQuMTc4ODgzODA1Nw..)

### Setup

Transmitter (Gun) ESP32:
- LED anode (long leg) → 220 Ohm resistor → GPIO;
- The LED cathode (short leg) → GND

Reciever (Vest) ESP32:
- Connect ZD1952 OUT → GPIO pin of ESP32
- Connect ZD1952 GND → GND
- Connect ZD1952 VCC → 3V3

### Results

Precision much higher than LDR and isn't too disturbed by ambient light. More testing is required for long lengths + different lighting conditions + the vertical and horizontal viewing angles and whether its too much, but theoretically it should work quite well. If a narrower and stronger IR beam or a stronger receiver is required, need to check other models.  

### Code

IR transmitter code: [code](laser-detection/ir-transmitter.ino)
IR receiver code: [code](laser-detection/ir-receiver.ino)
