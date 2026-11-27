"""
avtp_motor.py  -  IEEE-1722-Pakete (ACF-CAN-Brief ueber UDP) bauen und lesen
=============================================================================
Das ist das Python-Gegenstueck zu common/avtp_link.c der ESP32.
Aufbau eines Pakets (32 Byte, alle Zahlen "big endian"):

   Byte  0.. 3  UDP-Kapselung : 32-Bit Paketzaehler
   Byte  4..15  NTSCF-Kopf    : 0x82, Version/SV, Datenlaenge, 8-Bit Zaehler, Stream-ID
   Byte 16..23  ACF-CAN-Brief : Typ + Laenge, CAN-ID (29 Bit)
   Byte 24..31  Nutzdaten     : 8 CAN-Datenbytes

Diese Datei wurde gegen die echte Open1722-Bibliothek getestet
(tests/test_python_vs_c.py): ein von C gebautes Paket wird hier richtig
gelesen und umgekehrt.
"""
import struct

PAKET_LAENGE = 32


class Sender:
    """Baut Pakete eines Stroms (eigene Stream-ID + eigener Zaehler)."""

    def __init__(self, stream_id):
        self.stream_id = stream_id
        self.zaehler = 0

    def paket(self, can_id, daten):
        """can_id: int (<= 0x7FF), daten: genau 8 Bytes -> fertiges Paket (bytes)"""
        assert len(daten) == 8
        udp_kopf = struct.pack(">I", self.zaehler & 0xFFFFFFFF)

        # NTSCF-Kopf, 1. Wort: Typ 0x82 | SV=1 | Version=0 | R=0 | Laenge (11 Bit) | Zaehler
        acf_laenge = 16  # 8 Byte ACF-Kopf + 8 Byte Daten
        wort0 = (0x82 << 24) | (1 << 23) | (acf_laenge << 8) | (self.zaehler & 0xFF)
        ntscf = struct.pack(">IQ", wort0, self.stream_id)

        # ACF-CAN-Brief, 1. Wort: Typ=2 (CAN Brief) | Laenge in 32-Bit-Worten (=4)
        # alle Flag-Bits (RTR, EFF, ...) und Bus-ID bleiben 0
        acf0 = (0x02 << 25) | ((acf_laenge // 4) << 16)
        acf = struct.pack(">II", acf0, can_id & 0x1FFFFFFF) + bytes(daten)

        self.zaehler += 1
        return udp_kopf + ntscf + acf


class Empfaenger:
    """Liest Pakete eines Stroms; Pakete anderer Strome werden verworfen."""

    def __init__(self, stream_id):
        self.stream_id = stream_id
        self.erwartet = None   # naechster erwarteter Zaehler
        self.luecken = 0       # wie oft fehlte ein Paket

    def lesen(self, paket):
        """Gibt (can_id, daten) zurueck, oder None wenn das Paket nicht passt."""
        if len(paket) < PAKET_LAENGE:
            return None
        zaehler, wort0, stream_id, acf0, ident = struct.unpack(">IIQII", paket[:24])

        if (wort0 >> 24) != 0x82:            # kein NTSCF
            return None
        if (wort0 >> 20) & 0x7 != 0:         # nur Version 0
            return None
        if stream_id != self.stream_id:      # anderer Strom
            return None
        if ((wort0 >> 8) & 0x7FF) != 16:     # Nutzlast muss ACF(8)+Daten(8) sein
            return None
        if (acf0 >> 25) != 0x02:             # kein ACF-CAN-Brief
            return None

        if self.erwartet is not None and zaehler != self.erwartet:
            self.luecken += 1
        self.erwartet = (zaehler + 1) & 0xFFFFFFFF
        return ident & 0x1FFFFFFF, paket[24:32]
