"""Prueft Python <-> echte Open1722-Bibliothek (ueber das C-Testprogramm test_avtp)."""
import subprocess, sys
sys.path.insert(0, "pi_dashboard")
from avtp_motor import Sender, Empfaenger

EXE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/test_avtp"

# 1) C baut Paket -> Python liest es
hexpaket = subprocess.check_output([EXE, "build"]).decode().strip()
rx = Empfaenger(0xAABBCCDD02000001)
res = rx.lesen(bytes.fromhex(hexpaket))
assert res == (0x101, bytes([1,2,3,4,5,6,7,8])), res
print("C -> Python: OK", hex(res[0]), res[1].hex())

# 2) Python baut Paket -> C liest es (andere Stream-ID: 0xAABBCCDD01000001)
tx = Sender(0xAABBCCDD01000001)
p = tx.paket(0x100, bytes([1, 0x10, 0x27, 0, 0, 0, 0, 0]))
out = subprocess.check_output([EXE, "parse", p.hex()]).decode().split()
assert out == ["100", "0110270000000000"], out
print("Python -> C: OK", out)

# 3) Python-Paket ist bytegleich zum C-Paket (gleiche Parameter)
tx2 = Sender(0xAABBCCDD02000001); tx2.zaehler = 258
assert tx2.paket(0x101, bytes([1,2,3,4,5,6,7,8])).hex() == hexpaket
print("Byte-identisch: OK")

# 4) falsche Stream-ID wird abgelehnt (in C und Python)
assert Empfaenger(0xDEAD).lesen(bytes.fromhex(hexpaket)) is None
assert subprocess.run([EXE, "parse", hexpaket]).returncode == 1  # C erwartet ...01000001
print("Fremder Strom wird verworfen: OK")
