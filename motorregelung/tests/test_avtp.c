/* Host-Test: Paket bauen -> Bytes ausgeben / Paket lesen. Aufruf:
 *   test_avtp build  -> schreibt ein Paket als Hex auf stdout
 *   test_avtp parse <hex> -> liest ein Hex-Paket (z.B. aus Python) und gibt ID+Daten aus
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avtp_link.h"
int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "build")) {
        avtp_sender_t s; avtp_sender_init(&s, 0xAABBCCDD02000001ULL);
        s.zaehler = 258;
        uint8_t d[8] = {1,2,3,4,5,6,7,8}, buf[64];
        size_t n = avtp_paket_bauen(&s, 0x101, d, buf);
        for (size_t i = 0; i < n; i++) printf("%02x", buf[i]);
        printf("\n");
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "parse")) {
        uint8_t buf[64]; size_t n = strlen(argv[2]) / 2;
        for (size_t i = 0; i < n; i++) { unsigned v; sscanf(argv[2] + 2*i, "%2x", &v); buf[i] = (uint8_t)v; }
        avtp_empfaenger_t e; avtp_empfaenger_init(&e, 0xAABBCCDD01000001ULL);
        uint32_t id; uint8_t d[8];
        if (!avtp_paket_lesen(&e, buf, n, &id, d)) { printf("INVALID\n"); return 1; }
        printf("%x %02x%02x%02x%02x%02x%02x%02x%02x\n", id, d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7]);
        return 0;
    }
    return 2;
}
