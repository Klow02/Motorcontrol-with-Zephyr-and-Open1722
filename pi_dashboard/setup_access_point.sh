#!/bin/sh
# setup_access_point.sh  -  macht den Raspberry Pi zum WLAN-Access-Point
# =====================================================================
# Einmalig auf dem Pi ausfuehren:   sudo sh setup_access_point.sh
#
# Voraussetzung: Raspberry Pi OS "Bookworm" oder neuer (NetworkManager).
# Danach gilt:
#   - Der Pi spannt das WLAN "MotorRegelung" auf (IP des Pi: 192.168.4.1)
#   - ESP1 und ESP2 verbinden sich damit (feste IPs .51 und .52)
#   - Handy/Laptop verbinden sich ebenfalls mit diesem WLAN und oeffnen
#     im Browser:  http://192.168.4.1:8080
#   - Das WLAN startet bei jedem Boot automatisch.
#
# SSID und Passwort muessen zu common/projekt_config.h passen!
# Achtung: Das WLAN des Pi (wlan0) ist danach NICHT mehr mit deinem
# Heimnetz verbunden. Zugriff auf den Pi geht dann ueber das Kabel (eth0)
# oder ueber das neue WLAN.
set -e

SSID="MotorRegelung"
PASSWORT="motor12345"        # mindestens 8 Zeichen
KANAL=6                      # 2,4 GHz Kanal (1, 6 oder 11 sind meist am besten)
PI_IP="192.168.4.1/24"

if [ "$(id -u)" -ne 0 ]; then
    echo "Bitte mit sudo ausfuehren: sudo sh $0"
    exit 1
fi

# WLAN-Funk freigeben (falls per Land-Einstellung gesperrt)
rfkill unblock wifi || true

# Alte Version dieser Verbindung entfernen, falls vorhanden
nmcli connection delete MotorAP >/dev/null 2>&1 || true

# Access-Point anlegen. "ipv4.method shared" startet automatisch einen
# DHCP-Server (fuer Handy/Laptop). Die ESPs nutzen feste IPs.
nmcli connection add type wifi ifname wlan0 con-name MotorAP ssid "$SSID" \
    802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel "$KANAL" \
    wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PASSWORT" \
    wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp \
    ipv4.method shared ipv4.addresses "$PI_IP" \
    ipv6.method disabled \
    connection.autoconnect yes connection.autoconnect-priority 100

nmcli connection up MotorAP

echo
echo "Fertig. WLAN '$SSID' ist aktiv, Pi-Adresse 192.168.4.1"
echo "Dashboard starten:  python3 dashboard.py   ->  http://192.168.4.1:8080"
echo "Rueckgaengig machen: sudo nmcli connection delete MotorAP"
