# Photo Station

Post-game photo handoff: SD card → laptop → player picks photos → QR code → photos on their phone.

## Setup (once)

```
pip install -r requirements.txt
```

Optional but recommended — lets phones on **any** network (incl. mobile data) open the link:

- Mac: `brew install cloudflared`
- Windows: `winget install --id Cloudflare.cloudflared`

No account needed. Without it, phones must be on the same Wi-Fi as the laptop (uni Wi-Fi often blocks this — use a phone hotspot that both laptop and players join).

## Run

```
python app.py
```

The station opens at http://localhost:8000. The badge at the top says **Online** (cloudflared working) or **Wi-Fi only**.

## After each game

1. Pull the SD card(s) from the ESP32-CAMs and put them in the laptop.
2. **Import from SD card** — copies new photos in (re-importing the same card won't duplicate). You can also drag photos onto the page.
3. Player 1 taps the photos they want → **Send** → they scan the QR. Their page has *Save all to Photos* (phones), per-photo save, and a .zip.
4. Repeat for Player 2 with the remaining photos.
5. **New game** archives anything left over.

## Notes

- Only the laptop can see the full gallery; phones can only open their own link.
- Files live in `data/` — `inbox/` (unsent), `sessions/` (one folder per QR), `archive/`. Delete `data/` after the tradeshow.
- The online link changes every time you restart the app; QR codes made before a restart stop working.
