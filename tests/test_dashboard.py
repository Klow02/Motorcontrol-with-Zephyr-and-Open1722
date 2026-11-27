"""
Ende-zu-Ende-Test des Dashboards ohne Hardware.
Wir spielen ESP1, ESP2 und den Browser mit Python nach:
  - "ESP1" sendet MESSWERT-Pakete, "ESP2" sendet REGLER-Pakete an das Dashboard
  - "Browser" ruft /api/status ab und schickt einen Sollwert
  - "ESP2" empfaengt den SOLLWERT-Strom und prueft ihn
"""
import json
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.request

HIER = os.path.dirname(os.path.abspath(__file__))
DASH = os.path.join(HIER, "..", "pi_dashboard")
sys.path.insert(0, DASH)
from avtp_motor import Empfaenger, Sender  # noqa: E402

HTTP, P_SOLL, P_MESS, P_REGL = 18080, 27221, 27222, 27223
SID_SOLL, SID_MESS, SID_REGL = 0xAABBCCDD01000001, 0xAABBCCDD02000001, 0xAABBCCDD03000001

# "ESP2" hoert auf den Sollwert-Port (das Dashboard sendet an 127.0.0.1)
esp2_rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
esp2_rx.bind(("127.0.0.1", P_SOLL))
esp2_rx.settimeout(2.0)

proc = subprocess.Popen([sys.executable, os.path.join(DASH, "dashboard.py"),
                         "--esp2-ip", "127.0.0.1", "--http-port", str(HTTP),
                         "--port-sollwert", str(P_SOLL), "--port-messwert", str(P_MESS),
                         "--port-regler", str(P_REGL)], cwd=DASH,
                        env={**os.environ, "PYTHONPATH": DASH})
try:
    time.sleep(1.0)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    esp1, esp2 = Sender(SID_MESS), Sender(SID_REGL)

    # 1) Mess- und Reglerdaten einspeisen (mehrfach, damit "verbunden" gilt)
    for _ in range(5):
        udp.sendto(esp1.paket(0x101, struct.pack("<ii", 123456, -9050)), ("127.0.0.1", P_MESS))
        udp.sendto(esp2.paket(0x102, struct.pack("<BihB", 1, 150000, -375, 1)), ("127.0.0.1", P_REGL))
        time.sleep(0.05)
    s = json.load(urllib.request.urlopen(f"http://127.0.0.1:{HTTP}/api/status"))
    assert abs(s["ist_rpm"] - 1234.56) < 1e-6 and abs(s["ist_winkel"] + 90.5) < 1e-6, s
    assert s["regler_modus"] == 1 and s["regler_soll"] == 1500.0, s
    assert abs(s["ausgang_prozent"] + 37.5) < 1e-6 and s["sicherheit"] is True, s
    assert s["esp1_ok"] and s["esp2_ok"]
    print("Status-Anzeige:  OK  (Ist-Drehzahl 1234.56, Winkel -90.5, Ausgang -37.5 %, Sicherheit)")

    # 2) Browser setzt Sollwert Winkel 720 -> "ESP2" muss ihn empfangen
    req = urllib.request.Request(f"http://127.0.0.1:{HTTP}/api/sollwert", method="POST",
                                 data=json.dumps({"modus": 2, "sollwert": 720}).encode())
    assert json.load(urllib.request.urlopen(req))["ok"]
    rx = Empfaenger(SID_SOLL)
    gefunden = None
    ende = time.time() + 2
    while time.time() < ende and not gefunden:
        paket, _ = esp2_rx.recvfrom(256)
        erg = rx.lesen(paket)
        if erg and erg[1][0] == 2:
            gefunden = erg
    assert gefunden, "kein SOLLWERT mit Modus 2 empfangen"
    modus, centi = struct.unpack("<Bi", gefunden[1][:5])
    assert gefunden[0] == 0x100 and modus == 2 and centi == 72000, gefunden
    print("Sollwert senden: OK  (Modus WINKEL, 720.00 Grad, ID 0x100)")

    # 3) Begrenzung: 99999 U/min wird auf 3000 begrenzt, ungueltiger Modus abgelehnt
    req = urllib.request.Request(f"http://127.0.0.1:{HTTP}/api/sollwert", method="POST",
                                 data=json.dumps({"modus": 1, "sollwert": 99999}).encode())
    assert json.load(urllib.request.urlopen(req))["sollwert"] == 3000.0
    req = urllib.request.Request(f"http://127.0.0.1:{HTTP}/api/sollwert", method="POST",
                                 data=json.dumps({"modus": 7, "sollwert": 1}).encode())
    try:
        urllib.request.urlopen(req)
        raise AssertionError("ungueltiger Modus wurde akzeptiert")
    except urllib.error.HTTPError as e:
        assert e.code == 400
    print("Eingabegrenzen:  OK  (3000 U/min Limit, ungueltige Betriebsart -> 400)")

    # 4) Seite wird ausgeliefert
    html = urllib.request.urlopen(f"http://127.0.0.1:{HTTP}/").read().decode()
    assert "Motorsteuerung" in html
    print("Webseite:        OK")

    # 5) Ohne neue Pakete gilt ESP1/ESP2 nach 0,5 s als getrennt
    time.sleep(0.8)
    s = json.load(urllib.request.urlopen(f"http://127.0.0.1:{HTTP}/api/status"))
    assert not s["esp1_ok"] and not s["esp2_ok"]
    print("Verbindungsanzeige: OK (ohne Pakete -> getrennt)")
    print("\nDashboard-Test OK")
finally:
    proc.terminate()
