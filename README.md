# Motorregelung mit Zephyr + Open1722

Drehzahl- **oder** Winkelregelung eines Motors mit Hall-Encoder.
Zwei ESP32-C6 (Zephyr) und ein Raspberry Pi 4 sprechen per **IEEE 1722 (Open1722) über WLAN/UDP**.
Der **Pi ist selbst der WLAN-Access-Point** (kein Router nötig); das Dashboard erreichst du mit Handy/Laptop im Pi-WLAN.
Zusätzlich gibt es zwei **CAN-Ausgänge** (ESP1: Ist-Werte, ESP2: Regler-Status).

> **Ehrlicher Stand:** Die Zephyr-Firmware und das Pi-Access-Point-Skript konnten hier nicht gebaut/ausgeführt werden (kein Zephyr-SDK/keine Hardware).
> Getestet auf dem PC sind: Open1722-Anbindung (gegen die echte Bibliothek), Python↔C-Pakete,
> Regelalgorithmus (Simulation) und das Dashboard (Ende-zu-Ende). Die ESP-Quellen wurden gegen
> Zephyr ~4.4-Sourcen geprüft, ein erster Build kann trotzdem kleine Anpassungen verlangen.

## Aufbau

```
 Browser ──HTTP──► Pi 4 (dashboard.py)
                     │  ▲
        SOLLWERT 0x100  │  │ MESSWERT 0x101 (ESP1), REGLER 0x102 (ESP2)
                     ▼  │
  Encoder ─► ESP1 ──MESSWERT 0x101──► ESP2 ─► H-Brücke ─► Motor
             │  └─────────── CAN ───────┐  │
             └─► CAN: Ist-Drehzahl/-Winkel   └─► CAN: Soll + Stellgröße
```

| Nachricht | CAN-ID | Richtung | Inhalt (little-endian, Werte ×100) |
|---|---|---|---|
| SOLLWERT | 0x100 | Pi → ESP2 | Byte0 Modus (0 aus, 1 rpm, 2 Winkel), Byte1-4 Sollwert int32 |
| MESSWERT | 0x101 | ESP1 → ESP2, Pi, CAN | Byte0-3 Drehzahl int32, Byte4-7 Winkel int32 |
| REGLER | 0x102 | ESP2 → Pi, CAN | Byte0 Modus, Byte1-4 aktiver Soll, Byte5-6 Ausgang int16 (‰), Byte7 Status (Bit0 = Sicherheitsabschaltung) |

Die 8 Datenbytes sind identisch auf WLAN (IEEE 1722 ACF-CAN-Brief in NTSCF in UDP) und auf dem CAN-Bus.
UDP-Ports: 17221 Sollwert, 17222 Messwert, 17223 Regler.

## Verzeichnisse

- `common/` – gemeinsamer Code (Einstellungen, Protokoll, Open1722-Wrapper, Netzwerk, CAN, PID, Regelung)
- `esp1_encoder/` – Zephyr-App ESP1 (Quadratur-Decoder, Drehzahl/Winkel, Senden)
- `esp2_regler/` – Zephyr-App ESP2 (Empfang, Regelung, Motor-PWM, Sicherheit)
- `pi_dashboard/` – `dashboard.py` (nur Python-Standardbibliothek), `avtp_motor.py`, `setup_access_point.sh`, `motor-dashboard.service`
- `tests/` – Tests auf dem PC (`run_tests.sh`)

## Einstellen

Alles Wichtige steht oben in `common/projekt_config.h`: WLAN-Name/Passwort, IP-Adressen
(Pi .1 = Access Point, ESP1 .51, ESP2 .52 im Netz 192.168.4.0/24), Encoder-Pulse (`ENCODER_PULSES_PER_REV`, Standard 64 – **an deinen Motor anpassen**),
Drehrichtung (`ENCODER_RICHTUNG`), Timeouts. Geänderte IPs/Ports auch oben in `pi_dashboard/dashboard.py`
(oder per `--esp2-ip` usw.). Reglerwerte: `common/regelung.h` (`MOTOR_MAX_RPM`, `DREHZAHL_KP/KI`, `WINKEL_KP/KI/KD`).

## Verdrahtung (ESP32-C6-DevKitC)

**ESP1**: GPIO0 = Encoder A, GPIO1 = Encoder B (Pull-up intern), GPIO2 = CAN TXD, GPIO3 = CAN RXD
**ESP2**: GPIO0 = PWM, GPIO1 = INA, GPIO6 = INB, GPIO7 = Enable (optional), GPIO2/3 = CAN TXD/RXD

- **Pegel:** ESP32 arbeitet mit **3,3 V**. Das Monster Moto Shield und viele Hall-Encoder haben 5-V-Logik. Encoder-Signale
  (5 V) über Spannungsteiler/Pegelwandler auf 3,3 V; Shield-Eingänge reagieren meist auf 3,3 V, im Zweifel Pegelwandler.
- Monster Moto Shield: Kanal 1 (INA1/INB1/PWM1) nutzen, Kanal 2 bleibt frei. Normale H-Brücke: PWM→ENA, GPIO1→IN1, GPIO6→IN2.
- CAN braucht je einen **externen Transceiver** (z. B. SN65HVD230 für 3,3 V) und 120 Ω Abschluss an beiden Busenden.
- **Alle Massen verbinden** (ESP1, ESP2, Shield, Encoder, CAN).
- Motorversorgung getrennt vom ESP-USB; Strom-/Spannungsgrenzen der Brücke beachten.

## Bauen und Flashen

```sh
git clone https://github.com/COVESA/Open1722.git ~/Open1722
export OPEN1722_DIR=~/Open1722
west build -b esp32c6_devkitc/esp32c6/hpcore esp1_encoder -p
west flash
west build -b esp32c6_devkitc/esp32c6/hpcore esp2_regler -p
west flash
```
Open1722 wird nur als Header eingebunden (`OPEN1722_INLINE` ist `static inline`), es muss nichts separat gebaut werden.

## Pi als Access Point einrichten

Einmalig auf dem Pi (Raspberry Pi OS Bookworm oder neuer, NetworkManager):

```sh
cd pi_dashboard
sudo sh setup_access_point.sh      # spannt WLAN "MotorRegelung" auf, Pi = 192.168.4.1
```
- SSID/Passwort stehen oben im Skript **und** in `common/projekt_config.h` – beide müssen übereinstimmen (Passwort ≥ 8 Zeichen).
- Das WLAN startet bei jedem Boot von selbst. Der Pi-WLAN-Chip ist danach nicht mehr mit deinem Heimnetz verbunden;
  für SSH/Internet nimm das LAN-Kabel (eth0) oder das neue WLAN.
- Die ESPs haben feste IPs (.51/.52). Handys/Laptops bekommen ihre Adresse automatisch vom Pi (DHCP).
- Rückgängig: `sudo nmcli connection delete MotorAP`

## Dashboard auf dem Pi

```sh
cd pi_dashboard
python3 dashboard.py            # Browser (im Pi-WLAN): http://192.168.4.1:8080
```
Oben Betriebsart wählen, Wert eingeben, „Setzen“; „STOPP“ bremst sofort.
Die **Rund-Anzeigen** zeigen Drehzahl (−3000…+3000 U/min, Null oben) und Winkel (Vollkreis, 0° oben, mit Umdrehungszähler);
der orange Pfeil am Rand ist der Sollwert des aktiven Modus.
Der Pi sendet den Sollwert 10×/s; bleibt er länger als 1 s aus, bremst ESP2 selbst
(ebenso, wenn 200 ms keine Messwerte von ESP1 kommen).

Automatisch beim Booten starten (Pfad in `motor-dashboard.service` ggf. anpassen):
```sh
sudo cp motor-dashboard.service /etc/systemd/system/
sudo systemctl enable --now motor-dashboard
```

## Inbetriebnahme (empfohlene Reihenfolge)

1. ESP1 allein: Konsole zeigen/Dashboard: Motor von Hand drehen, Winkel prüfen (eine Umdrehung = 360°). Vorzeichen falsch → `ENCODER_RICHTUNG`.
2. ESP2 mit **kleinem** Sollwert (z. B. 300 U/min), Motor frei laufend. Drehrichtung passend? Sonst INA/INB tauschen.
3. `MOTOR_MAX_RPM` auf die gemessene Leerlaufdrehzahl bei 100 % setzen (Vorsteuerung).
4. Drehzahl-PI: erst `DREHZAHL_KI` klein, `KP` erhöhen bis schnell aber ohne Schwingen, dann `KI`.
5. Winkel-PID: `WINKEL_KP` erhöhen bis leichtes Überschwingen, dann `KD` für Dämpfung, `KI` nur gegen Restfehler.

## Tests auf dem PC

```sh
OPEN1722_DIR=~/Open1722 sh tests/run_tests.sh
```

## Bekannte Grenzen

- Nicht auf Hardware getestet (siehe oben); PID-Werte sind Startwerte für einen kleinen Gleichstrommotor.
- Winkel 0 = Position beim Start von ESP1; ein Neustart von ESP1 setzt ihn zurück.
- Keine Authentifizierung im Dashboard – jeder im Pi-WLAN kann den Motor steuern; setze ein eigenes, starkes WLAN-Passwort.
