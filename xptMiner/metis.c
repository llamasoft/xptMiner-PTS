/* $Id: fugue.c 216 2010-06-08 09:46:57Z tp $ */
/*
 * Fugue implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2007-2010  Projet RNRT SAPHIR
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @author   Thomas Pornin <thomas.pornin@cryptolog.com>
 */

#include <stddef.h>
#include <string.h>

#include "sph_metis.h"

#ifdef __cplusplus
extern "C"{
#endif

#ifdef _MSC_VER
#pragma warning (disable: 4146)
#endif

static const sph_u32 IV224[] = {
	SPH_C32(0xf4c9120d), SPH_C32(0x6286f757), SPH_C32(0xee39e01c),
	SPH_C32(0xe074e3cb), SPH_C32(0xa1127c62), SPH_C32(0x9a43d215),
	SPH_C32(0xbd8d679a)
};

static const sph_u32 IV256[] = {
	SPH_C32(0xe952bdde), SPH_C32(0x6671135f), SPH_C32(0xe0d4f668),
	SPH_C32(0xd2b0b594), SPH_C32(0xf96c621d), SPH_C32(0xfbf929de),
	SPH_C32(0x9149e899), SPH_C32(0x34f8c248)
};

static const sph_u32 IV384[] = {
	SPH_C32(0xaa61ec0d), SPH_C32(0x31252e1f), SPH_C32(0xa01db4c7),
	SPH_C32(0x00600985), SPH_C32(0x215ef44a), SPH_C32(0x741b5e9c),
	SPH_C32(0xfa693e9a), SPH_C32(0x473eb040), SPH_C32(0xe502ae8a),
	SPH_C32(0xa99c25e0), SPH_C32(0xbc95517c), SPH_C32(0x5c1095a1)
};

static const sph_u32 IV512[] = {
	SPH_C32(0x8807a57e), SPH_C32(0xe616af75), SPH_C32(0xc5d3e4db),
	SPH_C32(0xac9ab027), SPH_C32(0xd915f117), SPH_C32(0xb6eecc54),
	SPH_C32(0x06e8020b), SPH_C32(0x4a92efd1), SPH_C32(0xaac6e2c9),
	SPH_C32(0xddb21398), SPH_C32(0xcae65838), SPH_C32(0x437f203f),
	SPH_C32(0x25ea78e7), SPH_C32(0x951fddd6), SPH_C32(0xda6ed11d),
	SPH_C32(0xe13e3567)
};


#define TIX2(q, x00, x01, x08, x10, x24)   do { \
		x10 ^= x00; \
		x00 = (q); \
		x08 ^= x00; \
		x01 ^= x24; \
	} while (0)

#define TIX3(q, x00, x01, x04, x08, x16, x27, x30)   do { \
		x16 ^= x00; \
		x00 = (q); \
		x08 ^= x00; \
		x01 ^= x27; \
		x04 ^= x30; \
	} while (0)

#define TIX4(q, x00, x01, x04, x07, x08, x22, x24, x27, x30)   do { \
		x22 ^= x00; \
		x00 = (q); \
		x08 ^= x00; \
		x01 ^= x24; \
		x04 ^= x27; \
		x07 ^= x30; \
	} while (0)

#define CMIX30(x00, x01, x02, x04, x05, x06, x15, x16, x17)   do { \
		x00 ^= x04; \
		x01 ^= x05; \
		x02 ^= x06; \
		x15 ^= x04; \
		x16 ^= x05; \
		x17 ^= x06; \
	} while (0)

#define CMIX36(x00, x01, x02, x04, x05, x06, x18, x19, x20)   do { \
		x00 ^= x04; \
		x01 ^= x05; \
		x02 ^= x06; \
		x18 ^= x04; \
		x19 ^= x05; \
		x20 ^= x06; \
	} while (0)

#define SMIX(x0, x1, x2, x3)   do { \
		sph_u32 c0 = 0; \
		sph_u32 c1 = 0; \
		sph_u32 c2 = 0; \
		sph_u32 c3 = 0; \
		sph_u32 r0 = 0; \
		sph_u32 r1 = 0; \
		sph_u32 r2 = 0; \
		sph_u32 r3 = 0; \
		sph_u32 tmp; \
		tmp = mixtab0[x0 >> 24]; \
		c0 ^= tmp; \
		tmp = mixtab1[(x0 >> 16) & 0xFF]; \
		c0 ^= tmp; \
		r1 ^= tmp; \
		tmp = mixtab2[(x0 >>  8) & 0xFF]; \
		c0 ^= tmp; \
		r2 ^= tmp; \
		tmp = mixtab3[x0 & 0xFF]; \
		c0 ^= tmp; \
		r3 ^= tmp; \
		tmp = mixtab0[x1 >> 24]; \
		c1 ^= tmp; \
		r0 ^= tmp; \
		tmp = mixtab1[(x1 >> 16) & 0xFF]; \
		c1 ^= tmp; \
		tmp = mixtab2[(x1 >>  8) & 0xFF]; \
		c1 ^= tmp; \
		r2 ^= tmp; \
		tmp = mixtab3[x1 & 0xFF]; \
		c1 ^= tmp; \
		r3 ^= tmp; \
		tmp = mixtab0[x2 >> 24]; \
		c2 ^= tmp; \
		r0 ^= tmp; \
		tmp = mixtab1[(x2 >> 16) & 0xFF]; \
		c2 ^= tmp; \
		r1 ^= tmp; \
		tmp = mixtab2[(x2 >>  8) & 0xFF]; \
		c2 ^= tmp; \
		tmp = mixtab3[x2 & 0xFF]; \
		c2 ^= tmp; \
		r3 ^= tmp; \
		tmp = mixtab0[x3 >> 24]; \
		c3 ^= tmp; \
		r0 ^= tmp; \
		tmp = mixtab1[(x3 >> 16) & 0xFF]; \
		c3 ^= tmp; \
		r1 ^= tmp; \
		tmp = mixtab2[(x3 >>  8) & 0xFF]; \
		c3 ^= tmp; \
		r2 ^= tmp; \
		tmp = mixtab3[x3 & 0xFF]; \
		c3 ^= tmp; \
		x0 = ((c0 ^ r0) & SPH_C32(0xFF000000)) \
			| ((c1 ^ r1) & SPH_C32(0x00FF0000)) \
			| ((c2 ^ r2) & SPH_C32(0x0000FF00)) \
			| ((c3 ^ r3) & SPH_C32(0x000000FF)); \
		x1 = ((c1 ^ (r0 << 8)) & SPH_C32(0xFF000000)) \
			| ((c2 ^ (r1 << 8)) & SPH_C32(0x00FF0000)) \
			| ((c3 ^ (r2 << 8)) & SPH_C32(0x0000FF00)) \
			| ((c0 ^ (r3 >> 24)) & SPH_C32(0x000000FF)); \
		x2 = ((c2 ^ (r0 << 16)) & SPH_C32(0xFF000000)) \
			| ((c3 ^ (r1 << 16)) & SPH_C32(0x00FF0000)) \
			| ((c0 ^ (r2 >> 16)) & SPH_C32(0x0000FF00)) \
			| ((c1 ^ (r3 >> 16)) & SPH_C32(0x000000FF)); \
		x3 = ((c3 ^ (r0 << 24)) & SPH_C32(0xFF000000)) \
			| ((c0 ^ (r1 >> 8)) & SPH_C32(0x00FF0000)) \
			| ((c1 ^ (r2 >> 8)) & SPH_C32(0x0000FF00)) \
			| ((c2 ^ (r3 >> 8)) & SPH_C32(0x000000FF)); \
		/* */ \
	} while (0)

#if SPH_METIS_NOCOPY

#define DECL_STATE_SMALL
#define READ_STATE_SMALL(state)
#define WRITE_STATE_SMALL(state)
#define DECL_STATE_BIG
#define READ_STATE_BIG(state)
#define WRITE_STATE_BIG(state)

#define S00   ((sc)->S[ 0])
#define S01   ((sc)->S[ 1])
#define S02   ((sc)->S[ 2])
#define S03   ((sc)->S[ 3])
#define S04   ((sc)->S[ 4])
#define S05   ((sc)->S[ 5])
#define S06   ((sc)->S[ 6])
#define S07   ((sc)->S[ 7])
#define S08   ((sc)->S[ 8])
#define S09   ((sc)->S[ 9])
#define S10   ((sc)->S[10])
#define S11   ((sc)->S[11])
#define S12   ((sc)->S[12])
#define S13   ((sc)->S[13])
#define S14   ((sc)->S[14])
#define S15   ((sc)->S[15])
#define S16   ((sc)->S[16])
#define S17   ((sc)->S[17])
#define S18   ((sc)->S[18])
#define S19   ((sc)->S[19])
#define S20   ((sc)->S[20])
#define S21   ((sc)->S[21])
#define S22   ((sc)->S[22])
#define S23   ((sc)->S[23])
#define S24   ((sc)->S[24])
#define S25   ((sc)->S[25])
#define S26   ((sc)->S[26])
#define S27   ((sc)->S[27])
#define S28   ((sc)->S[28])
#define S29   ((sc)->S[29])
#define S30   ((sc)->S[30])
#define S31   ((sc)->S[31])
#define S32   ((sc)->S[32])
#define S33   ((sc)->S[33])
#define S34   ((sc)->S[34])
#define S35   ((sc)->S[35])

#else

#define DECL_STATE_SMALL \
	sph_u32 S00, S01, S02, S03, S04, S05, S06, S07, S08, S09; \
	sph_u32 S10, S11, S12, S13, S14, S15, S16, S17, S18, S19; \
	sph_u32 S20, S21, S22, S23, S24, S25, S26, S27, S28, S29;

#define DECL_STATE_BIG \
	DECL_STATE_SMALL \
	sph_u32 S30, S31, S32, S33, S34, S35;

#define READ_STATE_SMALL(state)   do { \
		S00 = (state)->S[ 0]; \
		S01 = (state)->S[ 1]; \
		S02 = (state)->S[ 2]; \
		S03 = (state)->S[ 3]; \
		S04 = (state)->S[ 4]; \
		S05 = (state)->S[ 5]; \
		S06 = (state)->S[ 6]; \
		S07 = (state)->S[ 7]; \
		S08 = (state)->S[ 8]; \
		S09 = (state)->S[ 9]; \
		S10 = (state)->S[10]; \
		S11 = (state)->S[11]; \
		S12 = (state)->S[12]; \
		S13 = (state)->S[13]; \
		S14 = (state)->S[14]; \
		S15 = (state)->S[15]; \
		S16 = (state)->S[16]; \
		S17 = (state)->S[17]; \
		S18 = (state)->S[18]; \
		S19 = (state)->S[19]; \
		S20 = (state)->S[20]; \
		S21 = (state)->S[21]; \
		S22 = (state)->S[22]; \
		S23 = (state)->S[23]; \
		S24 = (state)->S[24]; \
		S25 = (state)->S[25]; \
		S26 = (state)->S[26]; \
		S27 = (state)->S[27]; \
		S28 = (state)->S[28]; \
		S29 = (state)->S[29]; \
	} while (0)

#define READ_STATE_BIG(state)   do { \
		READ_STATE_SMALL(state); \
		S30 = (state)->S[30]; \
		S31 = (state)->S[31]; \
		S32 = (state)->S[32]; \
		S33 = (state)->S[33]; \
		S34 = (state)->S[34]; \
		S35 = (state)->S[35]; \
	} while (0)

#define WRITE_STATE_SMALL(state)   do { \
		(state)->S[ 0] = S00; \
		(state)->S[ 1] = S01; \
		(state)->S[ 2] = S02; \
		(state)->S[ 3] = S03; \
		(state)->S[ 4] = S04; \
		(state)->S[ 5] = S05; \
		(state)->S[ 6] = S06; \
		(state)->S[ 7] = S07; \
		(state)->S[ 8] = S08; \
		(state)->S[ 9] = S09; \
		(state)->S[10] = S10; \
		(state)->S[11] = S11; \
		(state)->S[12] = S12; \
		(state)->S[13] = S13; \
		(state)->S[14] = S14; \
		(state)->S[15] = S15; \
		(state)->S[16] = S16; \
		(state)->S[17] = S17; \
		(state)->S[18] = S18; \
		(state)->S[19] = S19; \
		(state)->S[20] = S20; \
		(state)->S[21] = S21; \
		(state)->S[22] = S22; \
		(state)->S[23] = S23; \
		(state)->S[24] = S24; \
		(state)->S[25] = S25; \
		(state)->S[26] = S26; \
		(state)->S[27] = S27; \
		(state)->S[28] = S28; \
		(state)->S[29] = S29; \
	} while (0)

#define WRITE_STATE_BIG(state)   do { \
		WRITE_STATE_SMALL(state); \
		(state)->S[30] = S30; \
		(state)->S[31] = S31; \
		(state)->S[32] = S32; \
		(state)->S[33] = S33; \
		(state)->S[34] = S34; \
		(state)->S[35] = S35; \
	} while (0)

#endif

static void
metis_init(sph_metis_context *sc, size_t z_len,
	const sph_u32 *iv, size_t iv_len)
{
	size_t u;

	for (u = 0; u < z_len; u ++)
		sc->S[u] = 0;
	memcpy(&sc->S[z_len], iv, iv_len * sizeof *iv);
	sc->partial = 0;
	sc->partial_len = 0;
	sc->round_shift = 0;
#if SPH_64
	sc->bit_count = 0;
#else
	sc->bit_count_high = 0;
	sc->bit_count_low = 0;
#endif
}

#if SPH_64

#define INCR_COUNTER   do { \
		sc->bit_count += (sph_u64)len << 3; \
	} while (0)

#else

#define INCR_COUNTER   do { \
		sph_u32 tmp = SPH_T32((sph_u32)len << 3); \
		sc->bit_count_low = SPH_T32(sc->bit_count_low + tmp); \
		if (sc->bit_count_low < tmp) \
			sc->bit_count_high ++; \
		sc->bit_count_high = SPH_T32(sc->bit_count_high \
			+ ((sph_u32)len >> 29)); \
	} while (0)

#endif

#define CORE_ENTRY \
	sph_u32 p; \
	unsigned plen, rshift; \
	INCR_COUNTER; \
	p = sc->partial; \
	plen = sc->partial_len; \
	if (plen < 4) { \
		unsigned count = 4 - plen; \
		if (len < count) \
			count = len; \
		plen += count; \
		while (count -- > 0) { \
			p = (p << 8) | *(const unsigned char *)data; \
			data = (const unsigned char *)data + 1; \
			len --; \
		} \
		if (len == 0) { \
			sc->partial = p; \
			sc->partial_len = plen; \
			return; \
		} \
	}

#define CORE_EXIT \
	p = 0; \
	sc->partial_len = (unsigned)len; \
	while (len -- > 0) { \
		p = (p << 8) | *(const unsigned char *)data; \
		data = (const unsigned char *)data + 1; \
	} \
	sc->partial = p; \
	sc->round_shift = rshift;

/*
 * Not in a do..while: the 'break' must exit the outer loop.
 */
#define NEXT(rc) \
	if (len <= 4) { \
		rshift = (rc); \
		break; \
	} \
	p = sph_dec32be(data); \
	data = (const unsigned char *)data + 4; \
	len -= 4

static void
metis2_core(sph_metis_context *sc, const void *data, size_t len)
{
	DECL_STATE_SMALL
	CORE_ENTRY
	READ_STATE_SMALL(sc);
	rshift = sc->round_shift;
	switch (rshift) {
		for (;;) {
			sph_u32 q;

		case 0:
			q = p;
			TIX2(q, S00, S01, S08, S10, S24);
			CMIX30(S27, S28, S29, S01, S02, S03, S12, S13, S14);
			SMIX(S27, S28, S29, S00);
			CMIX30(S24, S25, S26, S28, S29, S00, S09, S10, S11);
			SMIX(S24, S25, S26, S27);
			NEXT(1);
			/* fall through */
		case 1:
			q = p;
			TIX2(q, S24, S25, S02, S04, S18);
			CMIX30(S21, S22, S23, S25, S26, S27, S06, S07, S08);
			SMIX(S21, S22, S23, S24);
			CMIX30(S18, S19, S20, S22, S23, S24, S03, S04, S05);
			SMIX(S18, S19, S20, S21);
			NEXT(2);
			/* fall through */
		case 2:
			q = p;
			TIX2(q, S18, S19, S26, S28, S12);
			CMIX30(S15, S16, S17, S19, S20, S21, S00, S01, S02);
			SMIX(S15, S16, S17, S18);
			CMIX30(S12, S13, S14, S16, S17, S18, S27, S28, S29);
			SMIX(S12, S13, S14, S15);
			NEXT(3);
			/* fall through */
		case 3:
			q = p;
			TIX2(q, S12, S13, S20, S22, S06);
			CMIX30(S09, S10, S11, S13, S14, S15, S24, S25, S26);
			SMIX(S09, S10, S11, S12);
			CMIX30(S06, S07, S08, S10, S11, S12, S21, S22, S23);
			SMIX(S06, S07, S08, S09);
			NEXT(4);
			/* fall through */
		case 4:
			q = p;
			TIX2(q, S06, S07, S14, S16, S00);
			CMIX30(S03, S04, S05, S07, S08, S09, S18, S19, S20);
			SMIX(S03, S04, S05, S06);
			CMIX30(S00, S01, S02, S04, S05, S06, S15, S16, S17);
			SMIX(S00, S01, S02, S03);
			NEXT(0);
		}
	}
	CORE_EXIT
	WRITE_STATE_SMALL(sc);
}

static void
metis3_core(sph_metis_context *sc, const void *data, size_t len)
{
	DECL_STATE_BIG
	CORE_ENTRY
	READ_STATE_BIG(sc);
	rshift = sc->round_shift;
	switch (rshift) {
		for (;;) {
			sph_u32 q;

		case 0:
			q = p;
			TIX3(q, S00, S01, S04, S08, S16, S27, S30);
			CMIX36(S33, S34, S35, S01, S02, S03, S15, S16, S17);
			SMIX(S33, S34, S35, S00);
			CMIX36(S30, S31, S32, S34, S35, S00, S12, S13, S14);
			SMIX(S30, S31, S32, S33);
			CMIX36(S27, S28, S29, S31, S32, S33, S09, S10, S11);
			SMIX(S27, S28, S29, S30);
			NEXT(1);
			/* fall through */
		case 1:
			q = p;
			TIX3(q, S27, S28, S31, S35, S07, S18, S21);
			CMIX36(S24, S25, S26, S28, S29, S30, S06, S07, S08);
			SMIX(S24, S25, S26, S27);
			CMIX36(S21, S22, S23, S25, S26, S27, S03, S04, S05);
			SMIX(S21, S22, S23, S24);
			CMIX36(S18, S19, S20, S22, S23, S24, S00, S01, S02);
			SMIX(S18, S19, S20, S21);
			NEXT(2);
			/* fall through */
		case 2:
			q = p;
			TIX3(q, S18, S19, S22, S26, S34, S09, S12);
			CMIX36(S15, S16, S17, S19, S20, S21, S33, S34, S35);
			SMIX(S15, S16, S17, S18);
			CMIX36(S12, S13, S14, S16, S17, S18, S30, S31, S32);
			SMIX(S12, S13, S14, S15);
			CMIX36(S09, S10, S11, S13, S14, S15, S27, S28, S29);
			SMIX(S09, S10, S11, S12);
			NEXT(3);
			/* fall through */
		case 3:
			q = p;
			TIX3(q, S09, S10, S13, S17, S25, S00, S03);
			CMIX36(S06, S07, S08, S10, S11, S12, S24, S25, S26);
			SMIX(S06, S07, S08, S09);
			CMIX36(S03, S04, S05, S07, S08, S09, S21, S22, S23);
			SMIX(S03, S04, S05, S06);
			CMIX36(S00, S01, S02, S04, S05, S06, S18, S19, S20);
			SMIX(S00, S01, S02, S03);
			NEXT(0);
		}
	}
	CORE_EXIT
	WRITE_STATE_BIG(sc);
}

static void
metis4_core(sph_metis_context *sc, const void *data, size_t len)
{
	DECL_STATE_BIG
	CORE_ENTRY
	READ_STATE_BIG(sc);
	rshift = sc->round_shift;
	switch (rshift) {
		for (;;) {
			sph_u32 q;

		case 0:
			q = p;
			TIX4(q, S00, S01, S04, S07, S08, S22, S24, S27, S30);
			CMIX36(S33, S34, S35, S01, S02, S03, S15, S16, S17);
			SMIX(S33, S34, S35, S00);
			CMIX36(S30, S31, S32, S34, S35, S00, S12, S13, S14);
			SMIX(S30, S31, S32, S33);
			CMIX36(S27, S28, S29, S31, S32, S33, S09, S10, S11);
			SMIX(S27, S28, S29, S30);
			CMIX36(S24, S25, S26, S28, S29, S30, S06, S07, S08);
			SMIX(S24, S25, S26, S27);
			NEXT(1);
			/* fall through */
		case 1:
			q = p;
			TIX4(q, S24, S25, S28, S31, S32, S10, S12, S15, S18);
			CMIX36(S21, S22, S23, S25, S26, S27, S03, S04, S05);
			SMIX(S21, S22, S23, S24);
			CMIX36(S18, S19, S20, S22, S23, S24, S00, S01, S02);
			SMIX(S18, S19, S20, S21);
			CMIX36(S15, S16, S17, S19, S20, S21, S33, S34, S35);
			SMIX(S15, S16, S17, S18);
			CMIX36(S12, S13, S14, S16, S17, S18, S30, S31, S32);
			SMIX(S12, S13, S14, S15);
			NEXT(2);
			/* fall through */
		case 2:
			q = p;
			TIX4(q, S12, S13, S16, S19, S20, S34, S00, S03, S06);
			CMIX36(S09, S10, S11, S13, S14, S15, S27, S28, S29);
			SMIX(S09, S10, S11, S12);
			CMIX36(S06, S07, S08, S10, S11, S12, S24, S25, S26);
			SMIX(S06, S07, S08, S09);
			CMIX36(S03, S04, S05, S07, S08, S09, S21, S22, S23);
			SMIX(S03, S04, S05, S06);
			CMIX36(S00, S01, S02, S04, S05, S06, S18, S19, S20);
			SMIX(S00, S01, S02, S03);
			NEXT(0);
		}
	}
	CORE_EXIT
	WRITE_STATE_BIG(sc);
}

#if SPH_64

#define WRITE_COUNTER   do { \
		sph_enc64be(buf + 4, sc->bit_count + n); \
	} while (0)

#else

#define WRITE_COUNTER   do { \
		sph_enc32be(buf + 4, sc->bit_count_high); \
		sph_enc32be(buf + 8, sc->bit_count_low + n); \
	} while (0)

#endif

#define CLOSE_ENTRY(s, rcm, core) \
	unsigned char buf[16]; \
	unsigned plen, rms; \
	unsigned char *out; \
	sph_u32 S[s]; \
	plen = sc->partial_len; \
	WRITE_COUNTER; \
	if (plen == 0 && n == 0) { \
		plen = 4; \
	} else if (plen < 4 || n != 0) { \
		unsigned u; \
 \
		if (plen == 4) \
			plen = 0; \
		buf[plen] = ub & ~(0xFFU >> n); \
		for (u = plen + 1; u < 4; u ++) \
			buf[u] = 0; \
	} \
	core(sc, buf + plen, (sizeof buf) - plen); \
	rms = sc->round_shift * (rcm); \
	memcpy(S, sc->S + (s) - rms, rms * sizeof(sph_u32)); \
	memcpy(S + rms, sc->S, ((s) - rms) * sizeof(sph_u32));

#define ROR(n, s)   do { \
		sph_u32 tmp[n]; \
		memcpy(tmp, S + ((s) - (n)), (n) * sizeof(sph_u32)); \
		memmove(S + (n), S, ((s) - (n)) * sizeof(sph_u32)); \
		memcpy(S, tmp, (n) * sizeof(sph_u32)); \
	} while (0)

static void
metis2_close(sph_metis_context *sc, unsigned ub, unsigned n,
	void *dst, size_t out_size_w32)
{
	int i;

	CLOSE_ENTRY(30, 6, metis2_core)
	for (i = 0; i < 10; i ++) {
		ROR(3, 30);
		CMIX30(S[0], S[1], S[2], S[4], S[5], S[6], S[15], S[16], S[17]);
		SMIX(S[0], S[1], S[2], S[3]);
	}
	for (i = 0; i < 13; i ++) {
		S[4] ^= S[0];
		S[15] ^= S[0];
		ROR(15, 30);
		SMIX(S[0], S[1], S[2], S[3]);
		S[4] ^= S[0];
		S[16] ^= S[0];
		ROR(14, 30);
		SMIX(S[0], S[1], S[2], S[3]);
	}
	S[4] ^= S[0];
	S[15] ^= S[0];
	out = dst;
	sph_enc32be(out +  0, S[ 1]);
	sph_enc32be(out +  4, S[ 2]);
	sph_enc32be(out +  8, S[ 3]);
	sph_enc32be(out + 12, S[ 4]);
	sph_enc32be(out + 16, S[15]);
	sph_enc32be(out + 20, S[16]);
	sph_enc32be(out + 24, S[17]);
	if (out_size_w32 == 8) {
		sph_enc32be(out + 28, S[18]);
		sph_metis256_init(sc);
	} else {
		sph_metis224_init(sc);
	}
}

static void
metis3_close(sph_metis_context *sc, unsigned ub, unsigned n, void *dst)
{
	int i;

	CLOSE_ENTRY(36, 9, metis3_core)
	for (i = 0; i < 18; i ++) {
		ROR(3, 36);
		CMIX36(S[0], S[1], S[2], S[4], S[5], S[6], S[18], S[19], S[20]);
		SMIX(S[0], S[1], S[2], S[3]);
	}
	for (i = 0; i < 13; i ++) {
		S[4] ^= S[0];
		S[12] ^= S[0];
		S[24] ^= S[0];
		ROR(12, 36);
		SMIX(S[0], S[1], S[2], S[3]);
		S[4] ^= S[0];
		S[13] ^= S[0];
		S[24] ^= S[0];
		ROR(12, 36);
		SMIX(S[0], S[1], S[2], S[3]);
		S[4] ^= S[0];
		S[13] ^= S[0];
		S[25] ^= S[0];
		ROR(11, 36);
		SMIX(S[0], S[1], S[2], S[3]);
	}
	S[4] ^= S[0];
	S[12] ^= S[0];
	S[24] ^= S[0];
	out = dst;
	sph_enc32be(out +  0, S[ 1]);
	sph_enc32be(out +  4, S[ 2]);
	sph_enc32be(out +  8, S[ 3]);
	sph_enc32be(out + 12, S[ 4]);
	sph_enc32be(out + 16, S[12]);
	sph_enc32be(out + 20, S[13]);
	sph_enc32be(out + 24, S[14]);
	sph_enc32be(out + 28, S[15]);
	sph_enc32be(out + 32, S[24]);
	sph_enc32be(out + 36, S[25]);
	sph_enc32be(out + 40, S[26]);
	sph_enc32be(out + 44, S[27]);
	sph_metis384_init(sc);
}

static void
metis4_close(sph_metis_context *sc, unsigned ub, unsigned n, void *dst)
{
	int i;

	CLOSE_ENTRY(36, 12, metis4_core)
	for (i = 0; i < 32; i ++) {
		ROR(3, 36);
		CMIX36(S[0], S[1], S[2], S[4], S[5], S[6], S[18], S[19], S[20]);
		SMIX(S[0], S[1], S[2], S[3]);
	}
	for (i = 0; i < 13; i ++) {
		S[4] ^= S[0];
		S[9] ^= S[0];
		S[18] ^= S[0];
		S[27] ^= S[0];
		ROR(9, 36);
		SMIX(S[0], S[1], S[2], S[3]);
		S[4] ^= S[0];
		S[10] ^= S[0];
		S[18] ^= S[0];
		S[27] ^= S[0];
		ROR(9, 36);
		SMIX(S[0], S[1], S[2], S[3]);
		S[4] ^= S[0];
		S[10] ^= S[0];
		S[19] ^= S[0];
		S[27] ^= S[0];
		ROR(9, 36);
		SMIX(S[0], S[1], S[2], S[3]);
		S[4] ^= S[0];
		S[10] ^= S[0];
		S[19] ^= S[0];
		S[28] ^= S[0];
		ROR(8, 36);
		SMIX(S[0], S[1], S[2], S[3]);
	}
	S[4] ^= S[0];
	S[9] ^= S[0];
	S[18] ^= S[0];
	S[27] ^= S[0];
	out = dst;
	sph_enc32be(out +  0, S[ 1]);
	sph_enc32be(out +  4, S[ 2]);
	sph_enc32be(out +  8, S[ 3]);
	sph_enc32be(out + 12, S[ 4]);
	sph_enc32be(out + 16, S[ 9]);
	sph_enc32be(out + 20, S[10]);
	sph_enc32be(out + 24, S[11]);
	sph_enc32be(out + 28, S[12]);
	sph_enc32be(out + 32, S[18]);
	sph_enc32be(out + 36, S[19]);
	sph_enc32be(out + 40, S[20]);
	sph_enc32be(out + 44, S[21]);
	sph_enc32be(out + 48, S[27]);
	sph_enc32be(out + 52, S[28]);
	sph_enc32be(out + 56, S[29]);
	sph_enc32be(out + 60, S[30]);
//	sph_metis512_init(sc);
}

void
sph_metis224_init(void *cc)
{
	metis_init(cc, 23, IV224, 7);
}

void
sph_metis224(void *cc, const void *data, size_t len)
{
	metis2_core(cc, data, len);
}

void
sph_metis224_close(void *cc, void *dst)
{
	metis2_close(cc, 0, 0, dst, 7);
}

void
sph_metis224_addbits_and_close(void *cc, unsigned ub, unsigned n, void *dst)
{
	metis2_close(cc, ub, n, dst, 7);
}

void
sph_metis256_init(void *cc)
{
	metis_init(cc, 22, IV256, 8);
}

void
sph_metis256(void *cc, const void *data, size_t len)
{
	metis2_core(cc, data, len);
}

void
sph_metis256_close(void *cc, void *dst)
{
	metis2_close(cc, 0, 0, dst, 8);
}

void
sph_metis256_addbits_and_close(void *cc, unsigned ub, unsigned n, void *dst)
{
	metis2_close(cc, ub, n, dst, 8);
}

void
sph_metis384_init(void *cc)
{
	metis_init(cc, 24, IV384, 12);
}

void
sph_metis384(void *cc, const void *data, size_t len)
{
	metis3_core(cc, data, len);
}

void
sph_metis384_close(void *cc, void *dst)
{
	metis3_close(cc, 0, 0, dst);
}

void
sph_metis384_addbits_and_close(void *cc, unsigned ub, unsigned n, void *dst)
{
	metis3_close(cc, ub, n, dst);
}

void
sph_metis512_init(void *cc)
{
	metis_init(cc, 20, IV512, 16);
}

void
sph_metis512(void *cc, const void *data, size_t len)
{
	metis4_core(cc, data, len);
}

void
sph_metis512_close(void *cc, void *dst)
{
	metis4_close(cc, 0, 0, dst);
}

void
sph_metis512_addbits_and_close(void *cc, unsigned ub, unsigned n, void *dst)
{
	metis4_close(cc, ub, n, dst);
}
#ifdef __cplusplus
}
#endif
