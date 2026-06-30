/***************************************************************************
 * Colorfly C4 Pro -- software BCH(13,8,0x25af) parity generator.
 *
 * Encode-only port of the libbch (Parrot S.A., GPLv2) encoder. The decode
 * machinery is not needed: reads are corrected by the RK27xx hardware BCH
 * engine (FLCTL=0xA24). The mod8 remainder table (c4pro_bch_mod8_tab) is
 * precomputed by init_bch(13,8,0x25af) on the host (see
 * .../tools/dump_mod8.c) and embedded as a const, so no runtime allocation
 * or table build is required. The standalone encoder below was verified
 * byte-for-byte against libbch encode_bch over 4000 random/unaligned vectors.
 ****************************************************************************/
#include <stdint.h>
#include <string.h>
#include "c4pro_bch.h"

/* DIV_ROUND_UP(13*8, 32) = 4 ; ecc is 13 bytes (m*t/8). */
#define ECC_WORDS 4u
#define ECC_BYTES 13u
#define META_SIZE 3u

extern const uint32_t c4pro_bch_mod8_tab[4u * 256u * ECC_WORDS];

/* libbch emits 13 parity bytes; the C4 spare stores them reverse-bit'd and
 * XORed with this mask (identical to tools/c4_payload_ecc.c). The mask makes
 * an erased (all-0xFF) sector encode to an all-0xFF spare. */
static const unsigned char c4pro_bch_ecc_mask[ECC_BYTES] = {
    0x4e, 0x8c, 0x9d, 0x52, 0x2d, 0x6c, 0x7c,
    0xcb, 0xc3, 0x12, 0x14, 0x19, 0x37,
};

static unsigned char rev8(unsigned char b)
{
    return (unsigned char)(
        ((b & 0x80) >> 7) | ((b & 0x40) >> 5) |
        ((b & 0x20) >> 3) | ((b & 0x10) >> 1) |
        ((b & 0x08) << 1) | ((b & 0x04) << 3) |
        ((b & 0x02) << 5) | ((b & 0x01) << 7));
}

static uint32_t be32v(uint32_t x)
{
    return ((x & 0xffu) << 24) | ((x & 0xff00u) << 8) |
           ((x >> 8) & 0xff00u) | ((x >> 24) & 0xffu);
}

static void enc_unaligned(const unsigned char *data, unsigned int len,
                          uint32_t *ecc)
{
    const int l = (int)ECC_WORDS - 1;
    int i;
    const uint32_t *p;

    while (len--)
    {
        p = c4pro_bch_mod8_tab + (l + 1) * (((ecc[0] >> 24) ^ (*data++)) & 0xff);
        for (i = 0; i < l; i++)
            ecc[i] = ((ecc[i] << 8) | (ecc[i + 1] >> 24)) ^ (*p++);
        ecc[l] = (ecc[l] << 8) ^ (*p);
    }
}

static void load_ecc8(uint32_t *dst, const unsigned char *src)
{
    unsigned char pad[4] = { 0, 0, 0, 0 };
    unsigned int i, nwords = ECC_WORDS - 1;

    for (i = 0; i < nwords; i++, src += 4)
        dst[i] = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                 ((uint32_t)src[2] << 8) | src[3];

    memcpy(pad, src, ECC_BYTES - 4 * nwords);
    dst[nwords] = ((uint32_t)pad[0] << 24) | ((uint32_t)pad[1] << 16) |
                  ((uint32_t)pad[2] << 8) | pad[3];
}

static void store_ecc8(unsigned char *dst, const uint32_t *src)
{
    unsigned char pad[4];
    unsigned int i, nwords = ECC_WORDS - 1;

    for (i = 0; i < nwords; i++)
    {
        *dst++ = (unsigned char)(src[i] >> 24);
        *dst++ = (unsigned char)(src[i] >> 16);
        *dst++ = (unsigned char)(src[i] >> 8);
        *dst++ = (unsigned char)(src[i]);
    }
    pad[0] = (unsigned char)(src[nwords] >> 24);
    pad[1] = (unsigned char)(src[nwords] >> 16);
    pad[2] = (unsigned char)(src[nwords] >> 8);
    pad[3] = (unsigned char)(src[nwords]);
    memcpy(dst, pad, ECC_BYTES - 4 * nwords);
}

/* Compute BCH parity (ecc initialised to 0) over len data bytes. */
static void encode_bch(const unsigned char *data, unsigned int len,
                       unsigned char *ecc)
{
    const unsigned int l = ECC_WORDS - 1;
    unsigned int i, mlen;
    unsigned long m;
    uint32_t w, r[ECC_WORDS], buf[ECC_WORDS];
    const uint32_t *const tab0 = c4pro_bch_mod8_tab;
    const uint32_t *const tab1 = tab0 + 256 * (l + 1);
    const uint32_t *const tab2 = tab1 + 256 * (l + 1);
    const uint32_t *const tab3 = tab2 + 256 * (l + 1);
    const uint32_t *pdata, *p0, *p1, *p2, *p3;

    load_ecc8(buf, ecc);

    m = ((unsigned long)data) & 3;
    if (m)
    {
        mlen = (len < (4 - m)) ? len : (unsigned int)(4 - m);
        enc_unaligned(data, mlen, buf);
        data += mlen;
        len -= mlen;
    }

    pdata = (const uint32_t *)data;
    mlen = len / 4;
    data += 4 * mlen;
    len -= 4 * mlen;
    memcpy(r, buf, sizeof(r));

    while (mlen--)
    {
        w = r[0] ^ be32v(*pdata++);
        p0 = tab0 + (l + 1) * ((w >> 0) & 0xff);
        p1 = tab1 + (l + 1) * ((w >> 8) & 0xff);
        p2 = tab2 + (l + 1) * ((w >> 16) & 0xff);
        p3 = tab3 + (l + 1) * ((w >> 24) & 0xff);

        for (i = 0; i < l; i++)
            r[i] = r[i + 1] ^ p0[i] ^ p1[i] ^ p2[i] ^ p3[i];

        r[l] = p0[l] ^ p1[l] ^ p2[l] ^ p3[l];
    }
    memcpy(buf, r, sizeof(r));

    if (len)
        enc_unaligned(data, len, buf);

    store_ecc8(ecc, buf);
}

void c4pro_bch_make_spare(const unsigned char *data, unsigned char *spare)
{
    /* Static, not on the stack: this runs on the deep FTL cache-eviction/COW
     * write path (storage thread), where a 515-byte stack frame overflowed the
     * thread stack and crashed under sustained writes. The owned-volume write
     * path is serialized by the storage mutex, so a shared scratch is safe. */
    static unsigned char bch_input[C4PRO_BCH_SECTOR_DATA + META_SIZE];
    unsigned char ecc[ECC_BYTES];
    unsigned int i;

    for (i = 0; i < C4PRO_BCH_SECTOR_DATA; i++)
        bch_input[i] = rev8(data[i]);
    /* meta is 3x 0xFF -> rev8(0xFF) = 0xFF */
    bch_input[C4PRO_BCH_SECTOR_DATA + 0] = 0xff;
    bch_input[C4PRO_BCH_SECTOR_DATA + 1] = 0xff;
    bch_input[C4PRO_BCH_SECTOR_DATA + 2] = 0xff;

    memset(ecc, 0, sizeof(ecc));
    encode_bch(bch_input, sizeof(bch_input), ecc);

    spare[0] = 0xff;
    spare[1] = 0xff;
    spare[2] = 0xff;
    for (i = 0; i < ECC_BYTES; i++)
        spare[META_SIZE + i] = rev8(ecc[i]) ^ c4pro_bch_ecc_mask[i];
}

void c4pro_bch_fill_page_spare(const unsigned char *page_data,
                               unsigned char *page_spare)
{
    unsigned int sec;

    for (sec = 0; sec < C4PRO_BCH_PAGE_SECTORS; sec++)
        c4pro_bch_make_spare(page_data + sec * C4PRO_BCH_SECTOR_DATA,
                             page_spare + sec * C4PRO_BCH_SECTOR_SPARE);
}
