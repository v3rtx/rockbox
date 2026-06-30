/***************************************************************************
 * Colorfly C4 Pro -- firmware shim that reuses the upstream libbch.c
 * (Parrot S.A., GPLv2) decoder verbatim.
 *
 * libbch.c is compiled here essentially unmodified (only its malloc glue and
 * its <arpa/inet.h>/htonl use are guarded out with C4PRO_LIBBCH_GLUE). This
 * file supplies the firmware-side replacements:
 *
 *   - a portable, endian-independent byteswap for cpu_to_be32.
 *
 * On top of the library it adds the two entry points used by the FTL read
 * path: c4pro_bch_init() and c4pro_bch_correct_sector().
 *
 * IMPORTANT: init_bch() is NOT called on the target. The GF table builders in
 * libbch.c (build_gf_tables / compute_generator_polynomial / build_mod8_tables
 * / build_deg2_base) produce WRONG tables on the ARM7 target (the encoder's
 * embedded mod8 table works, but on-device init_bch yields a decoder that
 * flags phantom errors on clean codewords and cannot correct real ones).
 * Instead c4pro_bch_init() assembles a static struct bch_control whose GF
 * tables point at host-precomputed const arrays (c4pro_bch_dectab.c +
 * c4pro_bch_tab.c) and whose scratch points at static buffers. decode_bch()
 * and encode_bch() only READ those tables, so the const arrays are shared
 * read-only; the writable scratch is firmware-owned. The init_bch()/arena
 * code below is left compiled but unused.
 ****************************************************************************/
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "c4pro_bch.h"

/* ---- firmware glue: replaces libbch.c's kmalloc/kfree/kzalloc + htonl ---- */
#define C4PRO_LIBBCH_GLUE

/*
 * Static bump arena for libbch's kmalloc glue. This now only backs the unused
 * init_bch() path (see the file header): the target assembles bch_control from
 * embedded const tables and never allocates at runtime. Sized to init_bch's
 * peak (~83 KiB) so the compiler's interprocedural bounds analysis stays happy
 * (a shrunken arena trips -Warray-bounds on the still-compiled init_bch code).
 */
#define C4_BCH_ARENA_SIZE  (112u * 1024u)
static unsigned char c4_bch_arena[C4_BCH_ARENA_SIZE];
static size_t c4_bch_arena_off;

static void *c4_bch_alloc(size_t size)
{
    size_t off = (c4_bch_arena_off + 7u) & ~(size_t)7u; /* 8-byte align */
    if (off + size > sizeof(c4_bch_arena))
        return NULL;
    c4_bch_arena_off = off + size;
    return &c4_bch_arena[off];
}

#define kmalloc(sz, flags)  c4_bch_alloc(sz)
#define kfree(p)            ((void)(p))

static void *kzalloc(size_t size, int flags)
{
    void *p;
    (void)flags;
    p = c4_bch_alloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

/* endian-independent value byteswap (RK27xx is little-endian; this matches
 * htonl there and the be32v() used by the validated software encoder). */
static inline uint32_t c4_bswap32(uint32_t x)
{
    return ((x & 0x000000ffu) << 24) | ((x & 0x0000ff00u) << 8) |
           ((x & 0x00ff0000u) >> 8)  | ((x & 0xff000000u) >> 24);
}
#define cpu_to_be32(x) c4_bswap32(x)

/* Some libcs (e.g. newlib's <strings.h>) declare a non-static int fls(int),
 * which clashes with libbch.c's own "static inline int fls". Rename libbch's
 * internal helper so the upstream source stays untouched. */
#define fls c4_bch_fls

/* Pull in the upstream library verbatim (glue blocks skipped). */
#include "../../../../../utils/rk27utils/nandextract/libbch.c"

/* ---- C4 spare transform (must match c4pro_bch.c make_spare exactly) ---- */
#define C4_ECC_BYTES 13u

static const unsigned char c4_bch_ecc_mask[C4_ECC_BYTES] = {
    0x4e, 0x8c, 0x9d, 0x52, 0x2d, 0x6c, 0x7c,
    0xcb, 0xc3, 0x12, 0x14, 0x19, 0x37,
};

static unsigned char c4_rev8(unsigned char b)
{
    return (unsigned char)(
        ((b & 0x80) >> 7) | ((b & 0x40) >> 5) |
        ((b & 0x20) >> 3) | ((b & 0x10) >> 1) |
        ((b & 0x08) << 1) | ((b & 0x04) << 3) |
        ((b & 0x02) << 5) | ((b & 0x01) << 7));
}

/* ---- host-precomputed GF tables (c4pro_bch_dectab.c + c4pro_bch_tab.c) ---- */
extern const uint16_t c4pro_bch_a_pow_tab[8192];   /* 1 + n entries */
extern const uint16_t c4pro_bch_a_log_tab[8192];   /* 1 + n entries */
extern const unsigned int c4pro_bch_xi_tab[13];    /* m entries */
extern const uint32_t c4pro_bch_mod8_tab[4096];    /* 4*256*words encoder tab */

/* BCH(13,8,0x25af) constants, as computed by host init_bch(). */
#define C4_BCH_M         13
#define C4_BCH_N         8191
#define C4_BCH_T         8
#define C4_BCH_ECC_BITS  104u
#define C4_BCH_WORDS     4u   /* DIV_ROUND_UP(m*t, 32) */

/* Firmware-owned writable scratch (decode_bch/encode_bch read GF tables, but
 * write into these). Static, not on the stack: the FTL read path runs on the
 * storage thread and is serialized by the storage mutex. */
static uint32_t     c4_ecc_buf[C4_BCH_WORDS];
static uint32_t     c4_ecc_buf2[C4_BCH_WORDS];
static unsigned int c4_syn[2 * C4_BCH_T];
static int          c4_cache[2 * C4_BCH_T];

/* gf_poly scratch. uint32_t backing guarantees 4-byte alignment for the
 * struct gf_poly accesses. Sizes mirror init_bch(): elp gets
 * (t+1)*sizeof(struct gf_poly_deg1) (factor_polynomial indexes it as an array
 * of gf_poly_deg1), and each poly_2t[i] gets GF_POLY_SZ(2*t). */
static uint32_t c4_elp_buf[((C4_BCH_T + 1) * sizeof(struct gf_poly_deg1) + 3u) / 4u];
static uint32_t c4_poly_2t_buf[4][(GF_POLY_SZ(2 * C4_BCH_T) + 3u) / 4u];

static struct bch_control  c4_bch_ctl;
static struct bch_control *c4_bch;

void c4pro_bch_init(void)
{
    unsigned int i;

    if (c4_bch != NULL)
        return;

    c4_bch_ctl.m         = C4_BCH_M;
    c4_bch_ctl.n         = C4_BCH_N;
    c4_bch_ctl.t         = C4_BCH_T;
    c4_bch_ctl.ecc_bits  = C4_BCH_ECC_BITS;
    c4_bch_ctl.ecc_bytes = C4_ECC_BYTES;

    /* decode_bch/encode_bch only READ these; cast away const into the
     * bch_control's non-const pointer fields. */
    c4_bch_ctl.a_pow_tab = (uint16_t *)c4pro_bch_a_pow_tab;
    c4_bch_ctl.a_log_tab = (uint16_t *)c4pro_bch_a_log_tab;
    c4_bch_ctl.mod8_tab  = (uint32_t *)c4pro_bch_mod8_tab;
    c4_bch_ctl.xi_tab    = (unsigned int *)c4pro_bch_xi_tab;

    c4_bch_ctl.ecc_buf   = c4_ecc_buf;
    c4_bch_ctl.ecc_buf2  = c4_ecc_buf2;
    c4_bch_ctl.syn       = c4_syn;
    c4_bch_ctl.cache     = c4_cache;
    c4_bch_ctl.elp       = (struct gf_poly *)c4_elp_buf;
    for (i = 0; i < 4u; i++)
        c4_bch_ctl.poly_2t[i] = (struct gf_poly *)c4_poly_2t_buf[i];

    c4_bch = &c4_bch_ctl;
}

int c4pro_bch_correct_sector(unsigned char *data, const unsigned char *spare)
{
    /* Static, not on the stack: this runs on the FTL read path (storage
     * thread) where large frames have overflowed the thread stack before.
     * The owned-volume path is serialized by the storage mutex. */
    static unsigned char bch_data[C4PRO_BCH_SECTOR_DATA + 3u];
    unsigned char recv_ecc[C4_ECC_BYTES];
    unsigned int errloc[8];  /* t = 8 max correctable errors */
    unsigned int i;
    int n;

    if (c4_bch == NULL)
        c4pro_bch_init();
    if (c4_bch == NULL)
        return -1;

    /* Reverse the spare transform to recover the libbch-format parity:
     *   spare[3+i] = rev8(ecc[i]) ^ mask[i]  =>  ecc[i] = rev8(spare[3+i] ^ mask) */
    for (i = 0; i < C4_ECC_BYTES; i++)
        recv_ecc[i] = c4_rev8((unsigned char)(spare[3u + i] ^ c4_bch_ecc_mask[i]));

    /* Rebuild the codeword the encoder ran over: rev8(data) ++ 3x rev8(0xFF). */
    for (i = 0; i < C4PRO_BCH_SECTOR_DATA; i++)
        bch_data[i] = c4_rev8(data[i]);
    bch_data[C4PRO_BCH_SECTOR_DATA + 0u] = 0xff;
    bch_data[C4PRO_BCH_SECTOR_DATA + 1u] = 0xff;
    bch_data[C4PRO_BCH_SECTOR_DATA + 2u] = 0xff;

    n = decode_bch(c4_bch, bch_data, C4PRO_BCH_SECTOR_DATA + 3u,
                   recv_ecc, NULL, NULL, errloc);
    if (n < 0)
        return -1;  /* uncorrectable: leave data unchanged */

    /* decode_bch returns positions in the bch_data (bit-reversed) space, where
     * the correction is bch_data[loc/8] ^= 1 << (loc%8). Only positions inside
     * the 512 data bytes need to be reflected back to the caller's buffer; the
     * rev8 of byte b maps bit p -> bit (7-p), so flip data[b] bit (7 - loc%8). */
    for (i = 0; i < (unsigned int)n; i++) {
        unsigned int loc = errloc[i];
        if (loc < C4PRO_BCH_SECTOR_DATA * 8u) {
            unsigned int byte = loc >> 3;
            unsigned int bit  = loc & 7u;
            data[byte] ^= (unsigned char)(1u << (7u - bit));
        }
    }
    return n;
}
