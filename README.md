# Quick, Duck!

**A two-player laser tag game where every hit takes a candid photo.** Players are too busy dodging to pose, so the camera catches real reactions. Afterwards, each player privately chooses which of their photos to keep.

**No login needed.** Nothing in the prototype needs an account or password.

## Links

| | |
| --- | --- |
| **Wiki** | [Home and index](https://github.com/jetchommanivong/big-mumma-kim-and-her-babies-shash-sradha-dwij-jet-/wiki) |
| **Design Process Overview** | [Summary, process and who did what](https://github.com/jetchommanivong/big-mumma-kim-and-her-babies-shash-sradha-dwij-jet-/wiki/1.-Design-Process-Overview) |
| **Poster** | [Exhibition posters](https://github.com/jetchommanivong/big-mumma-kim-and-her-babies-shash-sradha-dwij-jet-/wiki/1.-Design-Process-Overview#posters) |
| **Promo video** | [Watch on YouTube](https://www.youtube.com/watch?v=GJFhy7H8UHM) |
| **Instagram** | [@quickduckgame](https://www.instagram.com/quickduckgame) |
| **How to run a game** | [Prototype Deployment & Gameplay](https://github.com/jetchommanivong/big-mumma-kim-and-her-babies-shash-sradha-dwij-jet-/wiki/4.-Prototype-Deployment-&-Gameplay) |
| **Ethics** | [Ethical Considerations](https://github.com/jetchommanivong/big-mumma-kim-and-her-babies-shash-sradha-dwij-jet-/wiki/5.-Ethical-Considerations) |

## The prototype

- **2 guns:** toy gun shells, each with an ESP32, an infrared LED (driven through a BC337 transistor), a laser aiming dot and the original sound board.
- **2 cameras:** an ESP32-CAM with a microSD card on each gun. The camera takes a photo when its own gun scores a hit.
- **2 vests:** each with an ESP32 and an infrared receiver target. The vest works out which gun hit it and tells that gun's camera over ESP-NOW. No Wi-Fi network is needed.
- **1 shock collar:** a servo on the collar vest presses the collar's remote on each hit (level 13, upper arm, 3-second cooldown).
- **Photo review app:** runs on a laptop. Players pick their photos and scan a QR code to save them to their phone.
- **Power:** one power bank per gun, with separate cables for the gun board and the camera.

## Run the photo review app

You need Python 3 on the laptop.

1. Install the requirements once:
   ```
   cd 04-prototypes/hi-fi/QR_App
   pip install -r requirements_1.txt
   ```
2. Optional: install `cloudflared` so phones on any network, including mobile data, can open the photo link. Without it, phones must be on the same Wi-Fi as the laptop.
   - Mac: `brew install cloudflared`
   - Windows: `winget install --id Cloudflare.cloudflared`
3. Start the app with `python app.py`. It opens at http://localhost:8000. The badge at the top shows **Online** (cloudflared working) or **Wi-Fi only**.
4. After each game:
   1. Put the cameras' SD cards in the laptop and press **Import from SD card**.
   2. Each player taps the photos they want to keep, presses **Send** and scans the QR code with their phone.
   3. Press **New game** to archive anything left over.
5. After the event, delete the `data/` folder so no photos stay on the laptop.

Full details are in [QR_App/README_1.md](04-prototypes/hi-fi/QR_App/README_1.md).

## Flash the boards

Use the Arduino IDE with the **esp32** board package by Espressif. The vest sketch also needs the **ESP32Servo** library by Kevin Harrington (Sketch > Include Library > Manage Libraries). All sketches are in [`04-prototypes/hi-fi/`](04-prototypes/hi-fi/).

| Board | Sketch | Board setting | Before uploading |
| --- | --- | --- | --- |
| Vest (×2) | [`target_with_id.ino`](04-prototypes/hi-fi/target_with_id.ino) | ESP32 Dev Module | Wiring is at the top of the file: receiver on pin 15, servo on pin 13. Wire the servo only on the collar vest. |
| Gun camera (×2) | [`esp32cam_with_id.ino`](04-prototypes/hi-fi/esp32cam_with_id.ino) | AI Thinker ESP32-CAM | Set `GUN_ID` to the gun it's mounted on (1 or 2). Use a microSD card formatted FAT32, 32 GB or smaller. |
| Gun | [`gun2_on_esp32cam.ino`](04-prototypes/hi-fi/gun2_on_esp32cam.ino) | AI Thinker ESP32-CAM | Set `GUN_ID` (gun 1 = 800 µs pulses, gun 2 = 1600 µs). Gun 1 runs the same code on an ESP32 dev board, with only the pin numbers changed. |

ESP32-CAM boards upload through the ESP32-CAM-MB base. If the upload gets stuck on "Connecting…", hold IO0. Open the Serial Monitor at 115200 baud and press RST to see each hit and photo as it happens.

All boards must run matching code, because the vest tells the two guns apart by their pulse lengths.

## What's in the repo

| Folder | What's in it |
| --- | --- |
| [`00-admin/`](00-admin/) | Meeting notes and who did what |
| [`01-research/`](01-research/) | Anonymised interview transcripts (P1–P10) |
| [`02-define/`](02-define/) | Thematic analysis of the interviews |
| [`03-ideation/`](03-ideation/) | Storyboard and gear design sketches |
| [`04-prototypes/`](04-prototypes/) | Paper prototype (`low-fi`), electronics tests (`electronics`), and the final firmware and photo app (`hi-fi`) |

Research findings, test results, the build process and stand-up records are on the [wiki](https://github.com/jetchommanivong/big-mumma-kim-and-her-babies-shash-sradha-dwij-jet-/wiki).

## Team

Kim, Shash, Sradha, Dwij and Jet, for DECO3500 Social & Mobile Computing at UQ, Semester 2, 2026. See [who did what](https://github.com/jetchommanivong/big-mumma-kim-and-her-babies-shash-sradha-dwij-jet-/wiki/1.-Design-Process-Overview#who-did-what).
