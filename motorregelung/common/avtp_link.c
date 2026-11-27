/*
 * avtp_link.c  -  siehe avtp_link.h
 */
#include "avtp_link.h"

#include <string.h>

#include "avtp/AlternativeHeader.h"
#include "avtp/CommonHeader.h"
#include "avtp/Udp.h"
#include "avtp/acf/AcfCommon.h"
#include "avtp/acf/CanBrief.h"
#include "avtp/acf/Ntscf.h"

void avtp_sender_init(avtp_sender_t *s, uint64_t stream_id)
{
    s->stream_id = stream_id;
    s->zaehler = 0;
}

void avtp_empfaenger_init(avtp_empfaenger_t *e, uint64_t stream_id)
{
    e->stream_id = stream_id;
    e->erwarteter_zaehler = 0;
    e->luecken = 0;
    e->erstes_paket = true;
}

size_t avtp_paket_bauen(avtp_sender_t *s, uint32_t can_id,
                        const uint8_t daten[8], uint8_t *puffer)
{
    uint8_t *p = puffer;

    /* 1) UDP-Kapselung: nur ein 32-Bit Paketzaehler (4 Byte) */
    Avtp_Udp_t *udp = (Avtp_Udp_t *)p;
    Avtp_Udp_Init(udp);
    Avtp_Udp_SetEncapsulationSeqNo(udp, s->zaehler);
    p += AVTP_UDP_HEADER_LEN;

    /* 2) NTSCF-Kopf (12 Byte): Stream-ID + 8-Bit Zaehler.
     *    Die Datenlaenge tragen wir erst ein, wenn wir sie kennen (unten). */
    Avtp_Ntscf_t *ntscf = (Avtp_Ntscf_t *)p;
    Avtp_Ntscf_InitV0(ntscf);
    Avtp_Ntscf_SetSequenceNum_V0(ntscf, s->zaehler & 0xFFu);
    Avtp_Ntscf_SetStreamId_V0(ntscf, s->stream_id);
    p += AVTP_NTSCF_HEADER_LEN_V0;

    /* 3) ACF-CAN-Brief: CAN-ID + 8 Datenbytes (die Bibliothek traegt
     *    Laenge und Auffuell-Bytes selbst ein) */
    uint8_t kopie[8];
    memcpy(kopie, daten, 8);
    Avtp_CanBrief_t *can = (Avtp_CanBrief_t *)p;
    Avtp_CanBrief_CreateAcfMessage(can, can_id, kopie, 8, false);
    size_t acf_laenge = Avtp_AcfCommon_GetAcfMsgLengthInBytes((Avtp_AcfCommon_t *)can);

    /* 4) Jetzt die Gesamtlaenge der Nutzlast im NTSCF-Kopf nachtragen */
    Avtp_Ntscf_SetNtscfDataLength_V0(ntscf, (uint16_t)acf_laenge);

    s->zaehler++;
    return AVTP_UDP_HEADER_LEN + AVTP_NTSCF_HEADER_LEN_V0 + acf_laenge;
}

bool avtp_paket_lesen(avtp_empfaenger_t *e, const uint8_t *puffer, size_t laenge,
                      uint32_t *can_id, uint8_t daten[8])
{
    /* Mindestlaenge: UDP-Kapselung + NTSCF-Kopf + ACF-Kopf */
    if (laenge < AVTP_UDP_HEADER_LEN + AVTP_NTSCF_HEADER_LEN_V0 + AVTP_CAN_BRIEF_HEADER_LEN) {
        return false;
    }

    /* 1) UDP-Kapselung: Paketzaehler lesen und Luecken zaehlen */
    uint32_t zaehler = Avtp_Udp_GetEncapsulationSeqNo((const Avtp_Udp_t *)puffer);

    /* 2) NTSCF-Kopf pruefen (Typ, Version, passt die Laenge ins Paket?) */
    const uint8_t *p = puffer + AVTP_UDP_HEADER_LEN;
    size_t rest = laenge - AVTP_UDP_HEADER_LEN;
    if (!Avtp_Ntscf_IsValid((const Avtp_Ntscf_t *)p, rest)) {
        return false;
    }
    if (Avtp_AlternativeHeader_GetVersion((const Avtp_AlternativeHeader_t *)p) != 0) {
        return false;                       /* wir kennen nur Version 0 */
    }
    const Avtp_Ntscf_t *ntscf = (const Avtp_Ntscf_t *)p;
    if (Avtp_Ntscf_GetStreamId_V0(ntscf) != e->stream_id) {
        return false;                       /* Paket eines anderen Stroms */
    }
    size_t nutzlast = Avtp_Ntscf_GetNtscfDataLength_V0(ntscf);
    if (AVTP_NTSCF_HEADER_LEN_V0 + nutzlast > rest) {
        return false;
    }

    /* 3) ACF-CAN-Brief pruefen und auspacken */
    const Avtp_CanBrief_t *can = (const Avtp_CanBrief_t *)(p + AVTP_NTSCF_HEADER_LEN_V0);
    if (!Avtp_CanBrief_IsValid(can, nutzlast)) {
        return false;
    }
    size_t n = Avtp_CanBrief_GetPayloadLength(can);
    if (n > 8) {
        return false;
    }
    memset(daten, 0, 8);
    memcpy(daten, Avtp_CanBrief_GetPayload(can), n);
    *can_id = Avtp_CanBrief_GetCanIdentifier(can);

    /* 4) Paket ist gut -> Zaehler nachfuehren */
    if (!e->erstes_paket && zaehler != e->erwarteter_zaehler) {
        e->luecken++;
    }
    e->erstes_paket = false;
    e->erwarteter_zaehler = zaehler + 1;
    return true;
}
