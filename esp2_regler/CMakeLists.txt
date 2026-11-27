# CMake fuer ESP2 (Regler-Knoten)
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp2_regler)

# Wo liegt die Open1722-Bibliothek? (Ordner, der "include/avtp/..." enthaelt)
# Entweder Umgebungsvariable OPEN1722_DIR setzen oder beim Bauen mitgeben:
#   west build ... -- -DOPEN1722_DIR=/pfad/zu/Open1722
set(OPEN1722_DIR "G:/Schule_und_Formula/Programmierordner/VS_Code/Open1722")
if(NOT OPEN1722_DIR)
  message(FATAL_ERROR "OPEN1722_DIR nicht gesetzt (Pfad zum geklonten Open1722-Repository)")
endif()

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../common)

target_sources(app PRIVATE
  src/main.c
  src/motor.c
  ${COMMON}/avtp_link.c
  ${COMMON}/netzwerk.c
  ${COMMON}/can_bus.c
  ${COMMON}/pid.c
  ${COMMON}/regelung.c
)

# Open1722 besteht fuer unsere Zwecke nur aus Header-Dateien -> nur Include-Pfad noetig
target_include_directories(app PRIVATE ${COMMON} ${OPEN1722_DIR}/include)
