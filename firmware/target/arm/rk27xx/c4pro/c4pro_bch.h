/***************************************************************************
 * Colorfly C4 Pro -- software BCH(13,8,0x25af) parity generator for the
 * owned-volume FTL data path.
 *
 * The RK27xx NAND controller decodes/corrects on read with the hardware BCH
 * engine (FLCTL=0xA24), but exposes no parity-generating write engine in this
 * driver, so the 13 parity bytes per 512-byte sector are computed in software
 * here and written into the page spare. The layout (3x 0xFF meta + 13 BCH
 * bytes, with bit-reversal and an ecc_mask so a blank page encodes to all
 * 0xFF) matches the proven host tool tools/c4_payload_ecc.c and round-trips
 * through the 0xA24 read decoder.
 ****************************************************************************/
#ifndef C4PRO_BCH_H
#define C4PRO_BCH_H

#define C4PRO_BCH_SECTOR_DATA   512u
#define C4PRO_BCH_SECTOR_SPARE  16u
#define C4PRO_BCH_PAGE_SECTORS  8u
#define C4PRO_BCH_PAGE_DATA     4096u
#define C4PRO_BCH_PAGE_SPARE    128u

/* Compute the 16-byte spare (3 meta + 13 parity) for one 512-byte sector. */
void c4pro_bch_make_spare(const unsigned char *data, unsigned char *spare);

/* Fill a full 128-byte page spare from 4096 bytes of page data (8 sectors). */
void c4pro_bch_fill_page_spare(const unsigned char *page_data,
                               unsigned char *page_spare);

/* One-time init of the libbch decoder (idempotent). Call once at mount. */
void c4pro_bch_init(void);

/* Correct up to 8 bit errors in one 512-byte sector using its 16-byte spare.
 * data[512] is corrected in place. Returns the number of corrected bits
 * (>= 0), or -1 if the sector is uncorrectable (data left unchanged). */
int c4pro_bch_correct_sector(unsigned char *data, const unsigned char *spare);

#endif /* C4PRO_BCH_H */
