/* Nuked OPL3
 * Copyright (C) 2013-2020 Nuke.YKT
 * Copyright (C) 2026 Tony Gies (Nuked-OPL3-fast modifications)
 *
 * This file is part of Nuked OPL3.
 *
 * Nuked OPL3 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 2.1
 * of the License, or (at your option) any later version.
 *
 * Nuked OPL3 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Nuked OPL3. If not, see <https://www.gnu.org/licenses/>.
 *
 *  Nuked OPL3 emulator.
 *  Thanks:
 *      MAME Development Team(Jarek Burczynski, Tatsuyuki Satoh):
 *          Feedback and Rhythm part calculation information.
 *      forums.submarine.org.uk(carbon14, opl3):
 *          Tremolo and phase generator calculation information.
 *      OPLx decapsulated(Matthew Gambrell, Olli Niemitalo):
 *          OPL2 ROMs.
 *      siliconpr0n.org(John McMaster, digshadow):
 *          YMF262 and VRC VII decaps and die shots.
 *
 * Upstream version: 1.8 (commit cfedb09)
 * Fork version:    1.8-fast.2
 * Fork home:       https://github.com/tgies/Nuked-OPL3-fast
 *
 * Nuked-OPL3-fast is a bit-exact performance-optimized fork of Nuked-OPL3.
 * Audio output is identical to upstream for the same register stream.
 *
 * Ported into DOSBox-Pure from the standalone Nuked-OPL3-fast library.
 */

#ifndef OPL_OPL3_H
#define OPL_OPL3_H
#define OPL_WRITEBUF_SIZE   1024
#define OPL_WRITEBUF_DELAY  2

#include "dosbox.h"

typedef struct _opl3_slot opl3_slot;
typedef struct _opl3_channel opl3_channel;
typedef struct _opl3_chip opl3_chip;

struct _opl3_slot {
    opl3_channel *channel;
    opl3_chip *chip;
    Bit16s *mod;
    Bit8u *trem;
    Bit32u pg_reset;
    Bit32u pg_phase;
    Bit32u pg_inc;
    Bit16s out;
    Bit16s fbmod;
    Bit16s prout;
    Bit16u eg_rout;
    Bit16u eg_out;
    /* Cached (reg_tl << 2) + (eg_ksl >> kslshift[reg_ksl]); maintained by
     * OPL3_EnvelopeUpdateKSL whenever any of those inputs change. Hoists
     * a load + lookup + shift out of the per-sample envelope hot path. */
    Bit16u eg_tl_ksl;
    Bit16u pg_phase_out;
    Bit8u key;
    Bit8u eg_gen;
    Bit8u reg_vib;
    Bit8u reg_mult;
    Bit8u reg_wf;
    Bit8u slot_num;
    Bit8u eg_ksl;
    Bit8u eg_ks;
    Bit8u reg_type;
    Bit8u reg_ksr;
    Bit8u reg_ksl;
    Bit8u reg_tl;
    Bit8u reg_ar;
    Bit8u reg_dr;
    Bit8u reg_sl;
    Bit8u reg_rr;
    Bit8u eg_rates[4];
    Bit8u eg_rate_hi[4];
    Bit8u eg_rate_lo[4];
    /* Phase increment per vibrato position, maintained by
     * OPL3_PhaseUpdateInc (and rebuilt on vibshift changes); pg_inc_vib[pos]
     * equals the upstream per-sample vibrato f_num adjustment for that pos. */
    Bit32u pg_inc_vib[8];
};

struct _opl3_channel {
    opl3_slot *slots[2];
    opl3_channel *pair;
    opl3_chip *chip;
    Bit16s *out[4];
    /* Mix-pass pointer lists: identical to out[] except entries pointing at
     * a delayed slot's out are redirected to its prout, which holds the
     * previous sample's out once all 36 slots are processed. out_left delays
     * slots 15-35 and out_right delays 33-35, reproducing the
     * CHANNELSAMPLEDELAY snapshots without staging slot processing around
     * the mixes. */
    Bit16s *out_left[4];
    Bit16s *out_right[4];
    Bit8u out_cnt;

    Bit8u chtype;
    Bit16u f_num;
    Bit8u block;
    Bit8u fb;
    Bit8u con;
    Bit8u alg;
    Bit8u ksv;
    Bit16u cha, chb;
    Bit16u chc, chd;
    Bit8u ch_num;
};

typedef struct _opl3_writebuf {
    Bit64u time;
    Bit16u reg;
    Bit8u data;
} opl3_writebuf;

struct _opl3_chip {
    opl3_channel channel[18];
    opl3_slot slot[36];
    Bit16u timer;
    Bit64u eg_timer;
    Bit8u eg_timerrem;
    Bit8u eg_state;
    Bit8u eg_add;
    Bit8u eg_timer_lo;
    Bit8u newm;
    Bit8u nts;
    Bit8u rhy;
    Bit8u vibpos;
    Bit8u vibshift;
    Bit8u tremolo;
    Bit8u tremolopos;
    Bit8u tremoloshift;
    Bit8u tremolo_dirty;
    Bit32u noise;
    /* Bit 0 of the noise LFSR state as seen by the hh (slot 13) and sd
     * (slot 16) rhythm operators, precomputed per sample */
    Bit32u noise_hh;
    Bit32u noise_sd;
    Bit16s zeromod;
    Bit32s mixbuff[4];
    Bit8u rm_hh_bit2;
    Bit8u rm_hh_bit3;
    Bit8u rm_hh_bit7;
    Bit8u rm_hh_bit8;
    Bit8u rm_tc_bit3;
    Bit8u rm_tc_bit5;

    /* OPL3L */
    Bit32s rateratio;
    Bit32s samplecnt;
    Bit16s oldsamples[4];
    Bit16s samples[4];

    Bit64u writebuf_samplecnt;
    Bit32u writebuf_cur;
    Bit32u writebuf_last;
    Bit64u writebuf_lasttime;
    opl3_writebuf writebuf[OPL_WRITEBUF_SIZE];
};

#include <math.h>
#include "adlib.h"

namespace NukedOPL
{
    struct Handler : public Adlib::Handler
    {
        opl3_chip chip;
        Bit8u newm;
        virtual void WriteReg(Bit32u reg, Bit8u val);
        virtual Bit32u WriteAddr(Bit32u port, Bit8u val);
        virtual void Generate(MixerChannel* chan, Bitu samples);
        virtual void Init(Bitu rate);
    };
}

#endif
