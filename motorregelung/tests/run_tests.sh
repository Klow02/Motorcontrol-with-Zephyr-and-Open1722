#!/bin/sh
# Fuehrt alle Tests auf dem PC aus (ohne Hardware).
# Voraussetzung: gcc, python3 und das Open1722-Repository (OPEN1722_DIR).
set -e
cd "$(dirname "$0")/.."
: "${OPEN1722_DIR:?Bitte OPEN1722_DIR auf den Open1722-Ordner setzen}"
T=$(mktemp -d)
INC="-Icommon -I$OPEN1722_DIR/include"
gcc -Wall -o $T/test_avtp tests/test_avtp.c common/avtp_link.c $INC 
gcc -Wall -o $T/test_pid tests/test_pid.c common/pid.c common/regelung.c $INC -lm && $T/test_pid
python3 tests/test_python_vs_c.py $T/test_avtp
python3 tests/test_dashboard.py
echo "=== Alle Tests bestanden ==="
