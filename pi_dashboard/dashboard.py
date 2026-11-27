#!/usr/bin/env python3
"""
dashboard.py  -  Web-Dashboard fuer die Motorsteuerung (Raspberry Pi 4)
=======================================================================
Starten:   python3 dashboard.py
Oeffnen:   http://<IP-des-Pi>:8080   (im Browser, auch vom Handy)

Was das Programm macht:
  1) EMPFANGEN:  Hoert per UDP auf zwei IEEE-1722-Strome
        - MESSWERT von ESP1  (Ist-Drehzahl und Ist-Winkel)
        - REGLER   von ESP2  (aktiver Sollwert, Motor-Ansteuerung, Status)
  2) SENDEN:     Schickt den vom Nutzer eingestellten Sollwert 10x pro
     Sekunde als SOLLWERT-Strom an ESP2. Das regelmaessige Senden ist
     Absicht: bleibt es aus (Pi/WLAN faellt aus), bremst ESP2 den Motor
     nach 1 Sekunde selbststaendig.
  3) WEBSEITE:   Kleiner Webserver (nur Python-Standardbibliothek, nichts
     zu installieren) mit Eingabe, Anzeige und Verlaufsdiagramm.

Die Zahlen unten muessen zu common/projekt_config.h der ESPs passen.
"""
import argparse
import json
import select
import socket
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from avtp_motor import Empfaenger, Sender

# ---------------------------------------------------------------------------
# Einstellungen  (gleiche Werte wie in common/projekt_config.h und
# common/motor_protocol.h der ESPs)
# ---------------------------------------------------------------------------
IP_ESP2 = "192.168.4.52"
PORT_SOLLWERT = 17221      # Pi -> ESP2
PORT_MESSWERT = 17222      # ESP1 -> Pi
PORT_REGLER = 17223        # ESP2 -> Pi

STREAM_ID_SOLLWERT = 0xAABBCCDD01000001
STREAM_ID_MESSWERT = 0xAABBCCDD02000001
STREAM_ID_REGLER = 0xAABBCCDD03000001

MSG_ID_SOLLWERT = 0x100
MSG_ID_MESSWERT = 0x101
MSG_ID_REGLER = 0x102

MODE_AUS, MODE_DREHZAHL, MODE_WINKEL = 0, 1, 2
MODE_NAMEN = {MODE_AUS: "AUS", MODE_DREHZAHL: "DREHZAHL", MODE_WINKEL: "WINKEL"}

MAX_RPM = 3000.0           # Eingabegrenze fuer die Drehzahl (U/min)
MAX_WINKEL = 36000.0       # Eingabegrenze fuer den Winkel (Grad)
SENDE_PERIODE_S = 0.1      # Sollwert 10x pro Sekunde senden
VERLAUF_PUNKTE = 300       # Anzahl Punkte im Diagramm (300 x 0,2 s = 60 s)
VERLAUF_PERIODE_S = 0.2

# ---------------------------------------------------------------------------
# Gemeinsamer Zustand (wird von mehreren Threads benutzt -> Lock)
# ---------------------------------------------------------------------------
lock = threading.Lock()
zustand = {
    # was der Nutzer eingestellt hat (wird an ESP2 gesendet)
    "modus": MODE_AUS,
    "sollwert": 0.0,            # U/min oder Grad, je nach Modus
    # was von ESP1 kommt
    "ist_rpm": 0.0,
    "ist_winkel": 0.0,
    "messwert_zeit": 0.0,       # wann zuletzt empfangen (time.time())
    # was von ESP2 kommt
    "regler_modus": MODE_AUS,
    "regler_soll": 0.0,
    "ausgang_prozent": 0.0,
    "sicherheit": False,
    "regler_zeit": 0.0,
}
verlauf = []                    # Liste von Messpunkten fuer das Diagramm


def sollwert_paket_daten(modus, sollwert):
    """Baut die 8 Datenbytes der SOLLWERT-Nachricht (Format: motor_protocol.h)."""
    centi = int(round(sollwert * 100))
    return struct.pack("<Bi3x", modus, centi)       # 1 + 4 + 3 Fuellbytes = 8


# ---------------------------------------------------------------------------
# Thread 1: Strome von ESP1 und ESP2 empfangen
# ---------------------------------------------------------------------------
def empfangs_thread():
    sock_mess = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_mess.bind(("", PORT_MESSWERT))
    sock_regl = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_regl.bind(("", PORT_REGLER))
    rx_mess = Empfaenger(STREAM_ID_MESSWERT)
    rx_regl = Empfaenger(STREAM_ID_REGLER)

    while True:
        bereit, _, _ = select.select([sock_mess, sock_regl], [], [], 1.0)
        for s in bereit:
            paket, _adresse = s.recvfrom(256)
            if s is sock_mess:
                erg = rx_mess.lesen(paket)
                if erg and erg[0] == MSG_ID_MESSWERT:
                    rpm_centi, winkel_centi = struct.unpack("<ii", erg[1])
                    with lock:
                        zustand["ist_rpm"] = rpm_centi / 100.0
                        zustand["ist_winkel"] = winkel_centi / 100.0
                        zustand["messwert_zeit"] = time.time()
            else:
                erg = rx_regl.lesen(paket)
                if erg and erg[0] == MSG_ID_REGLER:
                    modus, soll_centi, promille, status = struct.unpack("<BihB", erg[1])
                    with lock:
                        zustand["regler_modus"] = modus
                        zustand["regler_soll"] = soll_centi / 100.0
                        zustand["ausgang_prozent"] = promille / 10.0
                        zustand["sicherheit"] = bool(status & 0x01)
                        zustand["regler_zeit"] = time.time()


# ---------------------------------------------------------------------------
# Thread 2: Sollwert regelmaessig an ESP2 senden
# ---------------------------------------------------------------------------
def sende_thread():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tx = Sender(STREAM_ID_SOLLWERT)
    while True:
        with lock:
            modus, sollwert = zustand["modus"], zustand["sollwert"]
        paket = tx.paket(MSG_ID_SOLLWERT, sollwert_paket_daten(modus, sollwert))
        sock.sendto(paket, (IP_ESP2, PORT_SOLLWERT))
        time.sleep(SENDE_PERIODE_S)


# ---------------------------------------------------------------------------
# Thread 3: Verlauf fuer das Diagramm aufzeichnen (alle 0,2 s ein Punkt)
# ---------------------------------------------------------------------------
def verlauf_thread():
    t0 = time.time()
    while True:
        with lock:
            punkt = {
                "t": round(time.time() - t0, 1),
                "rpm": zustand["ist_rpm"],
                "winkel": zustand["ist_winkel"],
                "soll": zustand["regler_soll"],
                "modus": zustand["regler_modus"],
            }
            verlauf.append(punkt)
            del verlauf[:-VERLAUF_PUNKTE]
        time.sleep(VERLAUF_PERIODE_S)


# ---------------------------------------------------------------------------
# Webserver
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    def _json(self, daten, code=200):
        body = json.dumps(daten).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/":
            body = SEITE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/api/status":
            jetzt = time.time()
            with lock:
                antwort = dict(zustand)
                antwort["verlauf"] = list(verlauf)
            # "Verbunden", wenn in den letzten 0,5 s etwas ankam
            antwort["esp1_ok"] = jetzt - antwort["messwert_zeit"] < 0.5
            antwort["esp2_ok"] = jetzt - antwort["regler_zeit"] < 0.5
            self._json(antwort)
        else:
            self.send_error(404)

    def do_POST(self):
        """Nutzer hat im Browser einen Sollwert eingegeben: {"modus": 1, "sollwert": 1500}"""
        if self.path != "/api/sollwert":
            self.send_error(404)
            return
        try:
            laenge = int(self.headers.get("Content-Length", 0))
            daten = json.loads(self.rfile.read(laenge))
            modus = int(daten["modus"])
            sollwert = float(daten["sollwert"])
        except (ValueError, KeyError, TypeError):
            self._json({"fehler": "Ungueltige Eingabe"}, 400)
            return
        if modus not in MODE_NAMEN:
            self._json({"fehler": "Unbekannte Betriebsart"}, 400)
            return
        # Eingabe auf den erlaubten Bereich begrenzen
        grenze = MAX_RPM if modus == MODE_DREHZAHL else MAX_WINKEL
        sollwert = max(-grenze, min(grenze, sollwert))
        if modus == MODE_AUS:
            sollwert = 0.0
        with lock:
            zustand["modus"] = modus
            zustand["sollwert"] = sollwert
        self._json({"ok": True, "modus": modus, "sollwert": sollwert})

    def log_message(self, *args):
        pass        # keine Zeile pro Seitenaufruf in der Konsole


SEITE = r"""<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Motorsteuerung</title>
<style>
  body{font-family:system-ui,sans-serif;margin:0;background:#101418;color:#e8edf2}
  main{max-width:820px;margin:0 auto;padding:16px}
  h1{font-size:20px;margin:0 0 12px}
  .karte{background:#1a2128;border-radius:10px;padding:14px;margin-bottom:12px}
  .zeile{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
  .gross{font-size:30px;font-weight:600}
  .klein{font-size:13px;color:#8fa0b0}
  .kachel{flex:1;min-width:140px}
  button{font-size:16px;padding:10px 14px;border:0;border-radius:8px;background:#2f6fed;color:#fff;cursor:pointer}
  button.aus{background:#d23c3c}
  button.sek{background:#37424e}
  button.aktiv{outline:3px solid #8fb4ff}
  input{font-size:18px;padding:9px;border-radius:8px;border:1px solid #37424e;background:#0e1216;color:#fff;width:140px}
  .punkt{display:inline-block;width:10px;height:10px;border-radius:50%;background:#d23c3c;margin-right:5px}
  .punkt.ok{background:#3cc46a}
  .warn{color:#ffb454;font-weight:600}
  canvas{width:100%;height:220px;display:block}
  .rund{flex:1;min-width:240px;max-width:360px;text-align:center}
  .rund canvas{height:auto;aspect-ratio:1}
</style></head><body><main>
<h1>Motorsteuerung</h1>

<div class="karte">
  <div class="zeile">
    <button id="b1" class="sek" onclick="modus(1)">Drehzahl</button>
    <button id="b2" class="sek" onclick="modus(2)">Winkel</button>
    <input id="wert" type="number" step="any" value="0">
    <span id="einheit" class="klein">U/min</span>
    <button onclick="senden()">Setzen</button>
    <button class="aus" onclick="stopp()">STOPP</button>
  </div>
  <div class="klein" style="margin-top:8px">
    Winkel: 360 = eine Umdrehung, 0 = Position beim Einschalten von ESP1.
  </div>
</div>

<div class="karte zeile" style="justify-content:center">
  <div class="rund"><div class="klein">Drehzahl (Skala in 1000 U/min)</div><canvas id="rpmdial" width="400" height="400"></canvas></div>
  <div class="rund"><div class="klein">Winkel</div><canvas id="winkeldial" width="400" height="400"></canvas></div>
</div>

<div class="karte zeile">
  <div class="kachel"><div class="klein">Sollwert im Regler</div><div class="gross" id="soll">-</div><div class="klein" id="sollinfo">-</div></div>
  <div class="kachel"><div class="klein">Motor-Ansteuerung</div><div class="gross" id="aus">-</div><div class="klein">% PWM</div></div>
</div>

<div class="karte">
  <span class="punkt" id="p1"></span><span class="klein">ESP1 (Encoder)</span>&nbsp;&nbsp;
  <span class="punkt" id="p2"></span><span class="klein">ESP2 (Regler)</span>
  <span id="warn" class="warn"></span>
</div>

<div class="karte">
  <div class="klein">Verlauf der letzten 60 s (blau = Ist, orange = Soll)</div>
  <canvas id="plot" width="800" height="220"></canvas>
</div>

<script>
let aktModus = 1;   // im Eingabefeld gewaehlte Betriebsart

function modus(m){
  aktModus = m;
  document.getElementById('einheit').textContent = (m==1) ? 'U/min' : 'Grad';
  document.getElementById('b1').classList.toggle('aktiv', m==1);
  document.getElementById('b2').classList.toggle('aktiv', m==2);
}
async function post(m, w){
  await fetch('/api/sollwert', {method:'POST', body: JSON.stringify({modus:m, sollwert:w})});
}
function senden(){ post(aktModus, parseFloat(document.getElementById('wert').value) || 0); }
function stopp(){ document.getElementById('wert').value = 0; post(0, 0); }
modus(1);


// ---- Rundinstrument (Tacho) -------------------------------------------
// c: Canvas, ist: Istwert, soll: Sollwert (oder null), cfg: Einstellungen
//   cfg.grad(v)  rechnet einen Wert in Grad um (0 = oben, im Uhrzeigersinn)
//   cfg.ticks    Liste der Skalenstriche [{v, text}]   text = Beschriftung
//   cfg.text     Text in der Mitte,  cfg.einheit  kleine Zeile darunter
function rundinstrument(c, ist, soll, ok, cfg){
  const g = c.getContext('2d'), W = c.width, m = W/2, r = W*0.42;
  const rad = v => (cfg.grad(v) - 90) * Math.PI / 180;   // Canvas: 0 rad = rechts
  g.clearRect(0, 0, W, W);
  // Skalenring
  g.lineWidth = 14; g.strokeStyle = '#26303a'; g.beginPath();
  if(cfg.vollkreis) g.arc(m, m, r, 0, 2*Math.PI);
  else g.arc(m, m, r, (cfg.von-90)*Math.PI/180, (cfg.bis-90)*Math.PI/180);
  g.stroke();
  // Striche und Zahlen
  g.fillStyle = '#8fa0b0'; g.font = '22px sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
  cfg.ticks.forEach(t => {
    const a = rad(t.v), lang = t.text !== undefined;
    g.lineWidth = lang ? 3 : 1.5; g.strokeStyle = lang ? '#c5d0da' : '#5d6b78';
    g.beginPath();
    g.moveTo(m + Math.cos(a)*(r-(lang?30:20)), m + Math.sin(a)*(r-(lang?30:20)));
    g.lineTo(m + Math.cos(a)*(r-8), m + Math.sin(a)*(r-8));
    g.stroke();
    if(lang) g.fillText(t.text, m + Math.cos(a)*(r-52), m + Math.sin(a)*(r-52));
  });
  // Sollwert-Marke (orange Dreieck am Aussenrand)
  if(soll !== null){
    const a = rad(soll), px = m + Math.cos(a)*(r+16), py = m + Math.sin(a)*(r+16);
    g.fillStyle = '#ffb454'; g.beginPath();
    g.moveTo(px, py);
    g.lineTo(m + Math.cos(a)*(r+2) - Math.sin(a)*9, m + Math.sin(a)*(r+2) + Math.cos(a)*9);
    g.lineTo(m + Math.cos(a)*(r+2) + Math.sin(a)*9, m + Math.sin(a)*(r+2) - Math.cos(a)*9);
    g.closePath(); g.fill();
  }
  // Zeiger (Istwert)
  const a = rad(ist), farbe = ok ? '#5b9bff' : '#55606b';
  g.strokeStyle = farbe; g.lineWidth = 6; g.lineCap = 'round';
  g.beginPath(); g.moveTo(m, m); g.lineTo(m + Math.cos(a)*(r-36), m + Math.sin(a)*(r-36)); g.stroke();
  g.fillStyle = farbe; g.beginPath(); g.arc(m, m, 12, 0, 2*Math.PI); g.fill();
  // Zahl in der Mitte
  g.fillStyle = '#e8edf2'; g.font = '600 46px system-ui,sans-serif';
  g.fillText(ok ? cfg.text : '-', m, m + r*0.30);
  g.fillStyle = '#8fa0b0'; g.font = '20px sans-serif';
  g.fillText(cfg.einheit, m, m + r*0.30 + 34);
}

// Drehzahl: -3000 ... +3000 U/min, Null steht oben (Bogen von -135 bis +135 Grad)
const RPM_MAX = 3000;
const rpmTicks = [];
for(let v = -RPM_MAX; v <= RPM_MAX; v += 250)
  rpmTicks.push(v % 1000 == 0 ? {v: v, text: String(v/1000)} : {v: v});
function zeigeRpm(ist, soll, ok, sollAktiv){
  const k = v => Math.max(-RPM_MAX, Math.min(RPM_MAX, v));
  rundinstrument(document.getElementById('rpmdial'), k(ist), sollAktiv ? k(soll) : null, ok, {
    grad: v => k(v) / RPM_MAX * 135, von: -135, bis: 135, ticks: rpmTicks,
    text: ist.toFixed(0), einheit: 'U/min'
  });
}

// Winkel: voller Kreis, 0 Grad oben, positiv im Uhrzeigersinn
const winkelTicks = [];
for(let v = 0; v < 360; v += 15)
  winkelTicks.push(v % 90 == 0 ? {v: v, text: String(v)} : {v: v});
const mod360 = v => ((v % 360) + 360) % 360;
function zeigeWinkel(ist, soll, ok, sollAktiv){
  const umdr = Math.trunc(ist / 360);     // volle Umdrehungen seit Start
  rundinstrument(document.getElementById('winkeldial'), ist, sollAktiv ? soll : null, ok, {
    grad: v => mod360(v), vollkreis: true, ticks: winkelTicks,
    text: ist.toFixed(1) + '°', einheit: 'Umdrehungen: ' + umdr
  });
}
zeigeRpm(0, 0, false, false); zeigeWinkel(0, 0, false, false);

function zeichne(v){
  const c = document.getElementById('plot'), g = c.getContext('2d');
  g.clearRect(0,0,c.width,c.height);
  if(v.length < 2) return;
  // Es wird die Groesse gezeichnet, die zur aktuellen Betriebsart passt
  const w = v[v.length-1].modus == 2;
  const ist = v.map(p => w ? p.winkel : p.rpm), soll = v.map(p => p.soll);
  const alle = ist.concat(soll);
  let lo = Math.min(...alle, 0), hi = Math.max(...alle, 1);
  if(hi - lo < 1) hi = lo + 1;
  const y = x => c.height - 10 - (x - lo) / (hi - lo) * (c.height - 20);
  const X = i => i / (v.length - 1) * c.width;
  g.strokeStyle = '#37424e'; g.beginPath(); g.moveTo(0,y(0)); g.lineTo(c.width,y(0)); g.stroke();
  [[soll,'#ffb454'],[ist,'#5b9bff']].forEach(([d,f]) => {
    g.strokeStyle = f; g.lineWidth = 2; g.beginPath();
    d.forEach((val,i) => i ? g.lineTo(X(i), y(val)) : g.moveTo(X(i), y(val)));
    g.stroke();
  });
  g.fillStyle = '#8fa0b0'; g.font = '12px sans-serif';
  g.fillText(hi.toFixed(0), 4, 14); g.fillText(lo.toFixed(0), 4, c.height-14);
}

async function aktualisieren(){
  try{
    const s = await (await fetch('/api/status')).json();
    // Sollmarke nur im jeweils aktiven Modus zeigen
    zeigeRpm(s.ist_rpm, s.regler_soll, s.esp1_ok, s.esp2_ok && s.regler_modus == 1);
    zeigeWinkel(s.ist_winkel, s.regler_soll, s.esp1_ok, s.esp2_ok && s.regler_modus == 2);
    document.getElementById('soll').textContent = s.esp2_ok ? s.regler_soll.toFixed(1) : '-';
    document.getElementById('sollinfo').textContent = s.esp2_ok ? ['AUS','U/min','Grad'][s.regler_modus] : '-';
    document.getElementById('aus').textContent = s.esp2_ok ? s.ausgang_prozent.toFixed(1) : '-';
    document.getElementById('p1').className = 'punkt' + (s.esp1_ok ? ' ok' : '');
    document.getElementById('p2').className = 'punkt' + (s.esp2_ok ? ' ok' : '');
    document.getElementById('warn').textContent = (s.esp2_ok && s.sicherheit) ?
      '   Sicherheitsabschaltung aktiv (Messwert oder Sollwert fehlt)' : '';
    zeichne(s.verlauf);
  }catch(e){}
}
setInterval(aktualisieren, 250);
</script></main></body></html>
"""


def main():
    global IP_ESP2, PORT_SOLLWERT, PORT_MESSWERT, PORT_REGLER
    ap = argparse.ArgumentParser(description="Web-Dashboard fuer die Motorsteuerung")
    ap.add_argument("--esp2-ip", default=IP_ESP2, help="IP-Adresse von ESP2")
    ap.add_argument("--http-port", type=int, default=8080, help="Port der Webseite")
    ap.add_argument("--port-sollwert", type=int, default=PORT_SOLLWERT)
    ap.add_argument("--port-messwert", type=int, default=PORT_MESSWERT)
    ap.add_argument("--port-regler", type=int, default=PORT_REGLER)
    a = ap.parse_args()
    IP_ESP2, PORT_SOLLWERT = a.esp2_ip, a.port_sollwert
    PORT_MESSWERT, PORT_REGLER = a.port_messwert, a.port_regler

    for ziel in (empfangs_thread, sende_thread, verlauf_thread):
        threading.Thread(target=ziel, daemon=True).start()

    server = ThreadingHTTPServer(("", a.http_port), Handler)
    print(f"Dashboard laeuft: http://<IP-des-Pi>:{a.http_port}   (Beenden mit Strg+C)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nBeendet.")


if __name__ == "__main__":
    main()
