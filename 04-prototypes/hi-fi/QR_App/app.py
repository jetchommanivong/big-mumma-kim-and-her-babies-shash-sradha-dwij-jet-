#!/usr/bin/env python3
"""
Photo Station — post-game photo handoff for the laser-tag camera game.

Flow:
  1. After a game, put the ESP32-CAM SD card(s) in the laptop.
  2. Open http://localhost:8000 on the laptop, press "Import from SD card".
  3. The player taps the photos they want, presses "Send".
  4. A QR code appears; the player scans it and saves the photos on their phone.
  5. Repeat for the second player, then "New game" to archive the rest.

Phones reach the laptop through a free Cloudflare quick tunnel if `cloudflared`
is installed (works on any network / mobile data). Otherwise they must be on
the same Wi-Fi as the laptop.

Run:  pip install -r requirements.txt   then   python app.py
"""
import atexit
import hashlib
import io
import json
import os
import platform
import re
import secrets
import shutil
import socket
import string
import subprocess
import threading
import time
import webbrowser
import zipfile
from pathlib import Path

import segno
from flask import (Flask, abort, jsonify, render_template_string, request,
                   send_file, send_from_directory)
from werkzeug.utils import secure_filename

PORT = int(os.environ.get("PORT", 8000))
ROOT = Path(__file__).resolve().parent
DATA = ROOT / "data"
INBOX = DATA / "inbox"
SESSIONS = DATA / "sessions"
ARCHIVE = DATA / "archive"
HASHES = DATA / "imported_hashes.json"
for d in (INBOX, SESSIONS, ARCHIVE):
    d.mkdir(parents=True, exist_ok=True)

IMG_EXT = {".jpg", ".jpeg", ".png"}
MAX_CARD_BYTES = 256 * 1024**3  # ignore drives bigger than this (not an SD card)
SID_RE = re.compile(r"^[A-Za-z0-9_-]{6,24}$")

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 1024 * 1024 * 1024
PUBLIC = {"url": None, "mode": "wifi"}
_lock = threading.Lock()


# ---------------------------------------------------------------- networking
def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def start_tunnel():
    exe = shutil.which("cloudflared")
    if not exe or os.environ.get("NO_TUNNEL"):
        print("  cloudflared not found — phones must be on the same Wi-Fi as this laptop.")
        return
    proc = subprocess.Popen(
        [exe, "tunnel", "--no-autoupdate", "--url", f"http://localhost:{PORT}"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    atexit.register(proc.terminate)
    for line in proc.stdout:
        m = re.search(r"https://[a-z0-9-]+\.trycloudflare\.com", line)
        if m:
            PUBLIC.update(url=m.group(0), mode="internet")
            print(f"  Online link ready: {m.group(0)}")
            break
    for _ in proc.stdout:  # keep draining so the pipe never blocks
        pass


def operator_only():
    """Gallery/admin routes are for the laptop only, never for tunnel visitors."""
    via_tunnel = request.headers.get("Cf-Connecting-Ip") or request.headers.get("X-Forwarded-For")
    if via_tunnel or request.remote_addr not in ("127.0.0.1", "::1"):
        abort(403)


# ---------------------------------------------------------------- import
def load_hashes():
    try:
        return set(json.loads(HASHES.read_text()))
    except (OSError, ValueError):
        return set()


def save_hashes(h):
    HASHES.write_text(json.dumps(sorted(h)))


def add_image(data: bytes, hint: str, mtime: float, hashes: set) -> bool:
    digest = hashlib.sha1(data).hexdigest()
    if digest in hashes:
        return False
    stem, ext = os.path.splitext(secure_filename(hint) or "photo.jpg")
    ext = ext.lower() if ext.lower() in IMG_EXT else ".jpg"
    out = INBOX / f"{stem}_{digest[:6]}{ext}"
    out.write_bytes(data)
    if mtime:
        os.utime(out, (mtime, mtime))
    hashes.add(digest)
    return True


def card_volumes():
    system = platform.system()
    vols = []
    if system == "Darwin":
        base = Path("/Volumes")
        vols = [p for p in base.iterdir() if p.is_dir() and not p.is_symlink()] if base.exists() else []
    elif system == "Windows":
        for letter in string.ascii_uppercase:
            p = Path(f"{letter}:/")
            if letter != "C" and p.exists():
                vols.append(p)
    else:
        user = os.environ.get("USER", "")
        for base in (Path("/media") / user, Path("/run/media") / user, Path("/mnt")):
            if base.exists():
                vols += [p for p in base.iterdir() if p.is_dir()]
    out = []
    for v in vols:
        try:
            if shutil.disk_usage(v).total <= MAX_CARD_BYTES:
                out.append(v)
        except OSError:
            pass
    return out


def import_from(folder: Path, hashes: set, label: str) -> int:
    n = 0
    for dirpath, dirnames, filenames in os.walk(folder):
        dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "System Volume Information"]
        for f in filenames:
            if f.startswith(".") or Path(f).suffix.lower() not in IMG_EXT:
                continue
            p = Path(dirpath) / f
            try:
                if add_image(p.read_bytes(), f"{label}_{p.stem}{p.suffix}", p.stat().st_mtime, hashes):
                    n += 1
            except OSError:
                pass
    return n


def inbox_files():
    files = [p for p in INBOX.iterdir() if p.suffix.lower() in IMG_EXT]
    files.sort(key=lambda p: (p.stat().st_mtime, p.name))
    return [p.name for p in files]


# ---------------------------------------------------------------- operator API
@app.get("/")
def operator_page():
    operator_only()
    return render_template_string(OPERATOR_HTML)


@app.get("/api/state")
def api_state():
    operator_only()
    return jsonify(url=PUBLIC["url"], mode=PUBLIC["mode"], inbox=inbox_files())


@app.post("/api/import")
def api_import():
    operator_only()
    with _lock:
        hashes = load_hashes()
        vols = card_volumes()
        if not vols:
            return jsonify(message="No SD card found. Insert one, or drag the photos onto this page.")
        total = sum(import_from(v, hashes, secure_filename(v.name) or "card") for v in vols)
        save_hashes(hashes)
    names = ", ".join(v.name or str(v) for v in vols)
    msg = f"Imported {total} new photo{'s' if total != 1 else ''} from {names}." if total \
        else f"No new photos on {names}."
    return jsonify(message=msg)


@app.post("/api/upload")
def api_upload():
    operator_only()
    with _lock:
        hashes = load_hashes()
        n = sum(add_image(f.read(), f.filename or "photo.jpg", time.time(), hashes)
                for f in request.files.getlist("files")
                if Path(f.filename or "").suffix.lower() in IMG_EXT)
        save_hashes(hashes)
    return jsonify(message=f"Added {n} photo{'s' if n != 1 else ''}.")


@app.post("/api/send")
def api_send():
    operator_only()
    names = [n for n in (request.json or {}).get("names", []) if (INBOX / n).is_file() and "/" not in n and "\\" not in n]
    if not names:
        abort(400, "No photos selected")
    sid = secrets.token_urlsafe(8)
    dest = SESSIONS / sid
    dest.mkdir()
    for i, n in enumerate(names):
        shutil.move(str(INBOX / n), dest / n)
    return jsonify(id=sid, url=f"{PUBLIC['url']}/g/{sid}")


@app.post("/api/clear")
def api_clear():
    operator_only()
    dest = ARCHIVE / time.strftime("%Y%m%d-%H%M%S")
    dest.mkdir(exist_ok=True)
    for n in inbox_files():
        shutil.move(str(INBOX / n), dest / n)
    return jsonify(ok=True)


@app.get("/inbox/<path:name>")
def inbox_img(name):
    operator_only()
    return send_from_directory(INBOX, name)


@app.get("/qr/<sid>.svg")
def qr(sid):
    operator_only()
    if not SID_RE.match(sid):
        abort(404)
    buf = io.BytesIO()
    segno.make(f"{PUBLIC['url']}/g/{sid}", error="m").save(buf, kind="svg", scale=10, border=2)
    buf.seek(0)
    return send_file(buf, mimetype="image/svg+xml")


# ---------------------------------------------------------------- guest pages
def session_dir(sid):
    if not SID_RE.match(sid) or not (SESSIONS / sid).is_dir():
        abort(404)
    return SESSIONS / sid


@app.get("/g/<sid>")
def guest(sid):
    d = session_dir(sid)
    files = sorted(p.name for p in d.iterdir() if p.suffix.lower() in IMG_EXT)
    return render_template_string(GUEST_HTML, sid=sid, files=files)


@app.get("/g/<sid>/<path:name>")
def guest_img(sid, name):
    return send_from_directory(session_dir(sid), name)


@app.get("/g/<sid>.zip")
def guest_zip(sid):
    d = session_dir(sid)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        for p in sorted(d.iterdir()):
            if p.suffix.lower() in IMG_EXT:
                z.write(p, p.name)
    buf.seek(0)
    return send_file(buf, mimetype="application/zip", as_attachment=True, download_name="laser-tag-photos.zip")


# ---------------------------------------------------------------- HTML
OPERATOR_HTML = r"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Photo Station</title>
<style>
:root{--bg:#111;--panel:#1b1b1b;--fg:#eee;--mute:#8a8a8a;--acc:#ff3b5c}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{display:flex;gap:10px;align-items:center;padding:14px 20px;background:var(--panel);position:sticky;top:0;z-index:2;flex-wrap:wrap}
h1{font-size:18px;margin:0}
.spacer{flex:1}
button{font:inherit;padding:9px 14px;border-radius:8px;border:1px solid #333;background:#262626;color:var(--fg);cursor:pointer}
button.primary{background:var(--acc);border-color:var(--acc);font-weight:600}
button:disabled{opacity:.4;cursor:default}
.badge{font-size:12px;padding:4px 10px;border-radius:99px;background:#5a4a12}
.badge.internet{background:#165a33}
#msg{color:var(--mute);padding:10px 20px 0;min-height:1.4em}
#drop{margin:10px 20px 16px;padding:14px;border:2px dashed #333;border-radius:12px;text-align:center;color:var(--mute)}
#drop.over{border-color:var(--acc);color:var(--fg)}
#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:10px;padding:0 20px 60px}
.tile{position:relative;border-radius:10px;overflow:hidden;cursor:pointer;outline:4px solid transparent;outline-offset:-4px;background:#000;aspect-ratio:4/3}
.tile img{width:100%;height:100%;object-fit:cover;display:block}
.tile.sel{outline-color:var(--acc)}
.tick{position:absolute;top:8px;right:8px;width:28px;height:28px;border-radius:50%;border:2px solid #fff;background:rgba(0,0,0,.35);display:grid;place-items:center;font-weight:700}
.tile.sel .tick{background:var(--acc)}
.tile.sel .tick::after{content:"✓"}
.empty{color:var(--mute);padding:60px 20px;text-align:center;grid-column:1/-1}
#modal{position:fixed;inset:0;background:rgba(0,0,0,.88);display:none;align-items:center;justify-content:center;z-index:5}
#modal.show{display:flex}
.card{background:#fff;color:#111;padding:28px 32px;border-radius:18px;text-align:center}
.card h2{margin:0 0 6px}.card p{margin:0 0 14px;color:#555}
.card img{width:min(62vh,80vw);display:block;margin:0 auto 12px}
#link{font:12px ui-monospace,monospace;color:#777;word-break:break-all;margin-bottom:16px}
</style></head><body>
<header>
  <h1>Photo Station</h1><span id=mode class=badge>…</span><span class=spacer></span>
  <span id=count style="color:var(--mute)"></span>
  <button id=import>Import from SD card</button>
  <button id=all>Select all</button>
  <button id=send class=primary disabled>Select photos</button>
  <button id=clear title="Archive what's left and start fresh">New game</button>
</header>
<div id=msg></div>
<div id=drop>…or drag photos / a folder's contents onto this page</div>
<div id=grid></div>
<div id=modal><div class=card>
  <h2>Scan to get your photos</h2><p>Open your phone camera and point it here</p>
  <img id=qr alt="QR code"><div id=link></div>
  <button id=done class=primary>Done</button>
</div></div>
<script>
let state={inbox:[],mode:''};const sel=new Set();const $=s=>document.querySelector(s);
async function api(p,o){const r=await fetch(p,o);if(!r.ok)throw new Error(await r.text());return r.json()}
function flash(t){$('#msg').textContent=t}
function badge(){const b=$('#mode');b.className='badge '+state.mode;
  b.textContent=state.mode==='internet'?'Online — any phone can scan':'Wi-Fi only — phones must join this network'}
async function refresh(){state=await api('/api/state');badge();render()}
function render(){
  for(const n of [...sel]) if(!state.inbox.includes(n)) sel.delete(n);
  const g=$('#grid');g.innerHTML='';
  if(!state.inbox.length) g.innerHTML='<div class=empty>No photos yet — insert the SD card and press <b>Import from SD card</b>.</div>';
  for(const n of state.inbox){const t=document.createElement('div');t.className='tile'+(sel.has(n)?' sel':'');
    t.innerHTML='<img loading=lazy src="/inbox/'+encodeURIComponent(n)+'"><span class=tick></span>';
    t.onclick=()=>{sel.has(n)?sel.delete(n):sel.add(n);t.classList.toggle('sel');bar()};g.appendChild(t)}
  bar()}
function bar(){const s=$('#send');s.disabled=!sel.size;s.textContent=sel.size?`Send ${sel.size} photo${sel.size>1?'s':''} →`:'Select photos';
  $('#count').textContent=state.inbox.length+' photos';$('#all').textContent=sel.size&&sel.size===state.inbox.length?'Select none':'Select all'}
$('#import').onclick=async()=>{flash('Importing…');try{flash((await api('/api/import',{method:'POST'})).message)}catch(e){flash('Import failed: '+e.message)}refresh()};
$('#all').onclick=()=>{if(sel.size===state.inbox.length)sel.clear();else state.inbox.forEach(n=>sel.add(n));render()};
$('#send').onclick=async()=>{const r=await api('/api/send',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({names:[...sel]})});
  sel.clear();$('#qr').src='/qr/'+r.id+'.svg';$('#link').textContent=r.url;$('#modal').classList.add('show');refresh()};
$('#done').onclick=()=>$('#modal').classList.remove('show');
$('#clear').onclick=async()=>{if(!confirm('Archive all remaining photos and start a new game?'))return;await api('/api/clear',{method:'POST'});sel.clear();flash('Ready for the next game.');refresh()};
const drop=$('#drop');
document.addEventListener('dragover',e=>{e.preventDefault();drop.classList.add('over')});
document.addEventListener('dragleave',e=>{if(!e.relatedTarget)drop.classList.remove('over')});
document.addEventListener('drop',async e=>{e.preventDefault();drop.classList.remove('over');
  const fd=new FormData();for(const f of e.dataTransfer.files)fd.append('files',f);
  flash('Uploading…');flash((await api('/api/upload',{method:'POST',body:fd})).message);refresh()});
setInterval(async()=>{if(state.mode==='internet')return;const s=await api('/api/state');if(s.mode!==state.mode){state.mode=s.mode;badge()}},3000);
refresh();
</script></body></html>"""

GUEST_HTML = r"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Your game photos</title>
<style>
:root{--bg:#111;--fg:#eee;--mute:#999;--acc:#ff3b5c}
*{box-sizing:border-box}body{margin:0;font:16px/1.45 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
.wrap{max-width:640px;margin:auto;padding:20px 16px 40px}
h1{margin:0 0 4px;font-size:24px}p{color:var(--mute);margin:0 0 16px}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:22px}
.btn{font:inherit;display:inline-block;padding:12px 16px;border-radius:10px;border:1px solid #333;background:#262626;color:var(--fg);text-decoration:none;cursor:pointer}
.btn.primary{background:var(--acc);border-color:var(--acc);font-weight:600}
.ph{margin:0 0 20px}.ph img{width:100%;border-radius:12px;display:block;background:#000}
.ph a{display:inline-block;margin-top:8px;color:var(--mute);font-size:14px}
</style></head><body><div class=wrap>
<h1>Your photos</h1>
<p>{{ files|length }} photo{{ '' if files|length == 1 else 's' }} from your game. Tap <b>Save all</b>, or press and hold any photo to save it.</p>
<div class=actions>
  <button id=share class="btn primary" hidden>Save all to Photos</button>
  <a class=btn href="/g/{{ sid }}.zip" download>Download all (.zip)</a>
</div>
{% for f in files %}<div class=ph>
  <img src="/g/{{ sid }}/{{ f|urlencode }}" alt="Game photo {{ loop.index }}">
  <a href="/g/{{ sid }}/{{ f|urlencode }}" download="{{ f }}">Save this one</a>
</div>{% endfor %}
</div>
<script>
const names={{ files|tojson }}, sid={{ sid|tojson }}, btn=document.getElementById('share');
let files=null;
if(navigator.canShare){
  // Fetch up front: iOS only allows share() straight after a tap, not after an await.
  Promise.all(names.map(async n=>{const b=await (await fetch('/g/'+sid+'/'+encodeURIComponent(n))).blob();
    return new File([b],n,{type:b.type||'image/jpeg'})}))
    .then(fs=>{if(navigator.canShare({files:fs})){files=fs;btn.hidden=false}}).catch(()=>{});
  btn.onclick=()=>{if(files)navigator.share({files}).catch(()=>{})};
}
</script></body></html>"""


if __name__ == "__main__":
    PUBLIC["url"] = f"http://{lan_ip()}:{PORT}"
    threading.Thread(target=start_tunnel, daemon=True).start()
    print(f"\n  Photo Station running — open http://localhost:{PORT} on this laptop\n")
    if not os.environ.get("NO_BROWSER"):
        threading.Timer(1.2, lambda: webbrowser.open(f"http://localhost:{PORT}")).start()
    app.run(host="0.0.0.0", port=PORT, threaded=True)
