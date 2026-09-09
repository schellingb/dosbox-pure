/*
 * Copyright (C) 2021, 2024 nukeykt
 *
 * This file is part of Nuked-SC55.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  Thanks:
 *      John McMaster (https://siliconprawn.org):
 *          PCM chip decap
 *
 * Upstream version: 0.31 (commit 9c98ab9)
 *
 * This version was optimized for use in DOSBox Pure with the following changes:
 *  - Tied up code into this one file (excluded lcd.cpp LCD rendering and callbacks)
 *  - Change large static arrays to be dynamically allocated (rom, ram, etc.) with a single allocation
 *  - Simple API with NUKEDSC55_Init, NUKEDSC55_Render and NUKEDSC55_Shutdown based on original main of mcu.cpp
 *  - Removed own PCM output audio buffer to write directly into buffer passed to NUKEDSC55_Render
 *  - Fully zero global state when re-initializing to allow switching ROMs, shutting down and restarting
 *  - Small cleanups and optimizations which don't affect the processed output
 */

#include <stdint.h> // int types
#include <string.h> // memset, memcpy

#ifdef NDEBUG
#define SC55_ASSERT(cond)
#else
#define SC55_ASSERT(cond) (void)((cond) ? ((int)0) : *(volatile int*)0 = 0xbad,0)
#endif

// Removed functions
//#include <stdio.h> // to enable printf
#define printf(...) do {} while (0)
#define LCD_Enable(...) do {} while (0)
#define LCD_Write(...) do {} while (0)

#ifndef NUKEDSC55_MCU_H_DATA
#define NUKEDSC55_MCU_H_DATA
enum {
    INTERRUPT_SOURCE_NMI = 0,
    INTERRUPT_SOURCE_IRQ0, // GPINT
    INTERRUPT_SOURCE_IRQ1,
    INTERRUPT_SOURCE_FRT0_ICI,
    INTERRUPT_SOURCE_FRT0_OCIA,
    INTERRUPT_SOURCE_FRT0_OCIB,
    INTERRUPT_SOURCE_FRT0_FOVI,
    INTERRUPT_SOURCE_FRT1_ICI,
    INTERRUPT_SOURCE_FRT1_OCIA,
    INTERRUPT_SOURCE_FRT1_OCIB,
    INTERRUPT_SOURCE_FRT1_FOVI,
    INTERRUPT_SOURCE_FRT2_ICI,
    INTERRUPT_SOURCE_FRT2_OCIA,
    INTERRUPT_SOURCE_FRT2_OCIB,
    INTERRUPT_SOURCE_FRT2_FOVI,
    INTERRUPT_SOURCE_TIMER_CMIA,
    INTERRUPT_SOURCE_TIMER_CMIB,
    INTERRUPT_SOURCE_TIMER_OVI,
    INTERRUPT_SOURCE_ANALOG,
    INTERRUPT_SOURCE_UART_RX,
    INTERRUPT_SOURCE_UART_TX,
    INTERRUPT_SOURCE_MAX
};
struct mcu_t {
    uint16_t r[8];
    uint16_t pc;
    uint16_t sr;
    uint8_t cp, dp, ep, tp, br;
    uint8_t sleep;
    uint8_t ex_ignore;
    int32_t exception_pending;
    uint32_t interrupt_pending;
    uint16_t trapa_pending;
    uint64_t cycles;
};
#endif // NUKEDSC55_MCU_H_DATA

#ifndef NUKEDSC55_PCM_H_DATA
#define NUKEDSC55_PCM_H_DATA
struct pcm_t {
    uint32_t ram1[32][8];
    uint16_t ram2[32][16];
    uint32_t select_channel;
    uint32_t voice_mask;
    uint32_t voice_mask_pending;
    uint32_t voice_mask_updating;
    uint32_t write_latch;
    uint32_t wave_read_address;
    uint8_t wave_byte_latch;
    uint32_t read_latch;
    uint8_t config_reg_3c; // SC55:c3 JV880:c0
    uint8_t config_reg_3d;
    uint8_t bank_shift;
    uint32_t irq_channel;
    uint32_t irq_assert;

    uint32_t nfs;

    uint32_t tv_counter;

    uint64_t cycles;

    uint16_t* eram; //[0x4000];

    int accum_l;
    int accum_r;
    int rcsum[2];

    const uint8_t* banks[8];
    uint32_t bank_masks[8];
};
#endif // NUKEDSC55_PCM_H_DATA

#ifndef NUKEDSC55_SUBMCU_H_DATA
#define NUKEDSC55_SUBMCU_H_DATA
struct submcu_t {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t s;
    uint8_t sr;
    uint64_t cycles;
    uint8_t sleep;
};
#endif // NUKEDSC55_SUBMCU_H_DATA

#ifndef NUKEDSC55_MCU_OPCODES_CPP_DATA
#define NUKEDSC55_MCU_OPCODES_CPP_DATA
static uint32_t operand_type;
static uint16_t operand_ea;
static uint8_t operand_ep;
static uint8_t operand_size;
static uint8_t operand_reg;
static uint16_t operand_data;
static uint8_t opcode_extended;
#endif // NUKEDSC55_MCU_OPCODES_CPP_DATA

#ifndef NUKEDSC55_MCU_TIMER_CPP_DATA
#define NUKEDSC55_MCU_TIMER_CPP_DATA
static uint64_t timer_cycles;
static uint8_t timer_tempreg;

struct frt_t {
    uint8_t tcr;
    uint8_t tcsr;
    uint16_t frc;
    uint16_t ocra;
    uint16_t ocrb;
    uint16_t icr;
    uint8_t status_rd;
    uint8_t tcr_mask;
};
static frt_t frt[3];

struct mcu_timer_t {
    uint8_t tcr;
    uint8_t tcsr;
    uint8_t tcora;
    uint8_t tcorb;
    uint8_t tcnt;
    uint8_t status_rd;
    uint16_t tcr_mask;
};
static mcu_timer_t timer;
#endif // NUKEDSC55_MCU_TIMER_CPP_DATA

#ifndef NUKEDSC55_PCM_CPP_DATA
#define NUKEDSC55_PCM_CPP_DATA
static pcm_t pcm;
static uint8_t* sc55buffers;
#endif // NUKEDSC55_PCM_CPP_DATA

#ifndef NUKEDSC55_SUBMCU_CPP_DATA
#define NUKEDSC55_SUBMCU_CPP_DATA
static uint8_t* sm_rom; //[4096];
static uint8_t* sm_ram; //[128];
static uint8_t* sm_shared_ram; //[192];
static uint8_t sm_access[0x18];

static uint8_t sm_p0_dir;
static uint8_t sm_p1_dir;

static uint8_t sm_device_mode[32];
static uint8_t sm_cts;

static uint8_t sm_timer_prescaler;
static uint8_t sm_timer_counter;

static submcu_t sm;

static uint8_t sm_uart_rx_gotbyte;
static uint8_t sm_uart_rx_byte;
static uint64_t sm_uart_rx_delay;
#endif // NUKEDSC55_SUBMCU_CPP_DATA

#ifndef NUKEDSC55_MCU_CPP_DATA
#define NUKEDSC55_MCU_CPP_DATA
enum : int { ROM1_SIZE = 0x8000 };
enum : int { ROM2_SIZE = 0x80000 };
enum : int { RAM_SIZE = 0x400 };
enum : int { SRAM_SIZE = 0x8000 };
enum : int { NVRAM_SIZE = 0x8000 }; // JV880 only
enum : int { CARDRAM_SIZE = 0x8000 }; // JV880 only
enum : int { ROMSM_SIZE = 0x1000 };

static short* render_output;

static bool mcu_mk1; // 0 - SC-55mkII, SC-55ST. 1 - SC-55, CM-300/SCC-1
static bool mcu_cm300; // 0 - SC-55, 1 - CM-300/SCC-1
static bool mcu_jv880; // 0 - SC-55, 1 - JV880
static bool mcu_scb55; // 0 - sub mcu (e.g SC-55mk2), 1 - no sub mcu (e.g SCB-55)
static bool mcu_sc155; // 0 - SC-55(MK2), 1 - SC-155(MK2)

static uint8_t ga_int;
static uint8_t ga_int_enable = 0;
static uint8_t ga_int_trigger = 0;
static int ga_lcd_counter = 0;

static uint8_t dev_register[0x80];

static uint8_t io_sd = 0x00;

static int adf_rd = 0;

static uint64_t analog_end_time;

static int ssr_rd = 0;

enum : uint32_t { uart_buffer_size = 8192 };
static uint32_t uart_write_ptr;
static uint32_t uart_read_ptr;
static uint8_t* uart_buffer; //[uart_buffer_size];

static uint8_t mcu_uart_rx_byte;
static uint64_t mcu_uart_rx_delay;
static uint64_t mcu_uart_tx_delay;

static mcu_t mcu;

static uint8_t* rom1; //[ROM1_SIZE];
static uint8_t* rom2; //[ROM2_SIZE];
static uint8_t* ram; //[RAM_SIZE];
static uint8_t* sram; //[SRAM_SIZE];
static uint8_t* nvram; //[NVRAM_SIZE];
static uint8_t* cardram; //[CARDRAM_SIZE];

static int rom2_mask;

static uint8_t mcu_p0_data = 0x00;
#endif // NUKEDSC55_MCU_CPP_DATA

#ifndef NUKEDSC55_MCU_OPCODES_H
#define NUKEDSC55_MCU_OPCODES_H
extern void (*MCU_Operand_Table[256])(uint8_t operand);
extern void (*MCU_Opcode_Table[32])(uint8_t opcode, uint8_t opcode_reg);
#endif // NUKEDSC55_MCU_OPCODES_H

#ifndef NUKEDSC55_MCU_INTERRUPT_H
#define NUKEDSC55_MCU_INTERRUPT_H

static void MCU_Interrupt_Exception(uint32_t exception);

enum {
    EXCEPTION_SOURCE_ADDRESS_ERROR = 0,
    EXCEPTION_SOURCE_INVALID_INSTRUCTION,
    EXCEPTION_SOURCE_TRACE,
};
#endif // NUKEDSC55_MCU_INTERRUPT_H

#ifndef NUKEDSC55_MCU_H
#define NUKEDSC55_MCU_H
enum : uint16_t { sr_mask = 0x870f };

enum {
    DEV_P1DDR = 0x00,
    DEV_P5DDR = 0x08,
    DEV_P6DDR = 0x09,
    DEV_P7DDR = 0x0c,
    DEV_P7DR = 0x0e,
    DEV_FRT1_TCR = 0x10,
    DEV_FRT1_TCSR = 0x11,
    DEV_FRT1_FRCH = 0x12,
    DEV_FRT1_FRCL = 0x13,
    DEV_FRT1_OCRAH = 0x14,
    DEV_FRT1_OCRAL = 0x15,
    DEV_FRT2_TCR = 0x20,
    DEV_FRT2_TCSR = 0x21,
    DEV_FRT2_FRCH = 0x22,
    DEV_FRT2_FRCL = 0x23,
    DEV_FRT2_OCRAH = 0x24,
    DEV_FRT2_OCRAL = 0x25,
    DEV_FRT3_TCR = 0x30,
    DEV_FRT3_TCSR = 0x31,
    DEV_FRT3_FRCH = 0x32,
    DEV_FRT3_FRCL = 0x33,
    DEV_FRT3_OCRAH = 0x34,
    DEV_FRT3_OCRAL = 0x35,
    DEV_PWM1_TCR = 0x40,
    DEV_PWM1_DTR = 0x41,
    DEV_PWM2_TCR = 0x44,
    DEV_PWM2_DTR = 0x45,
    DEV_PWM3_TCR = 0x48,
    DEV_PWM3_DTR = 0x49,
    DEV_TMR_TCR = 0x50,
    DEV_TMR_TCSR = 0x51,
    DEV_TMR_TCORA = 0x52,
    DEV_TMR_TCORB = 0x53,
    DEV_TMR_TCNT = 0x54,
    DEV_SMR = 0x58,
    DEV_BRR = 0x59,
    DEV_SCR = 0x5a,
    DEV_TDR = 0x5b,
    DEV_SSR = 0x5c,
    DEV_RDR = 0x5d,
    DEV_ADDRAH = 0x60,
    DEV_ADDRAL = 0x61,
    DEV_ADDRBH = 0x62,
    DEV_ADDRBL = 0x63,
    DEV_ADDRCH = 0x64,
    DEV_ADDRCL = 0x65,
    DEV_ADDRDH = 0x66,
    DEV_ADDRDL = 0x67,
    DEV_ADCSR = 0x68,
    DEV_IPRA = 0x70,
    DEV_IPRB = 0x71,
    DEV_IPRC = 0x72,
    DEV_IPRD = 0x73,
    DEV_DTEA = 0x74,
    DEV_DTEB = 0x75,
    DEV_DTEC = 0x76,
    DEV_DTED = 0x77,
    DEV_WCR = 0x78,
    DEV_RAME = 0x79,
    DEV_P1CR = 0x7c,
    DEV_P9DDR = 0x7e,
    DEV_P9DR = 0x7f,
};

enum {
    STATUS_T = 0x8000,
    STATUS_N = 0x08,
    STATUS_Z = 0x04,
    STATUS_V = 0x02,
    STATUS_C = 0x01,
    STATUS_INT_MASK = 0x700
};

enum {
    VECTOR_RESET = 0,
    VECTOR_RESERVED1, // UNUSED
    VECTOR_INVALID_INSTRUCTION,
    VECTOR_DIVZERO,
    VECTOR_TRAP,
    VECTOR_RESERVED2, // UNUSED
    VECTOR_RESERVED3, // UNUSED
    VECTOR_RESERVED4, // UNUSED
    VECTOR_ADDRESS_ERROR,
    VECTOR_TRACE,
    VECTOR_RESERVED5, // UNUSED
    VECTOR_NMI,
    VECTOR_RESERVED6, // UNUSED
    VECTOR_RESERVED7, // UNUSED
    VECTOR_RESERVED8, // UNUSED
    VECTOR_RESERVED9, // UNUSED
    VECTOR_TRAPA_0,
    VECTOR_TRAPA_1,
    VECTOR_TRAPA_2,
    VECTOR_TRAPA_3,
    VECTOR_TRAPA_4,
    VECTOR_TRAPA_5,
    VECTOR_TRAPA_6,
    VECTOR_TRAPA_7,
    VECTOR_TRAPA_8,
    VECTOR_TRAPA_9,
    VECTOR_TRAPA_A,
    VECTOR_TRAPA_B,
    VECTOR_TRAPA_C,
    VECTOR_TRAPA_D,
    VECTOR_TRAPA_E,
    VECTOR_TRAPA_F,
    VECTOR_IRQ0,
    VECTOR_IRQ1,
    VECTOR_INTERNAL_INTERRUPT_88, // UNUSED
    VECTOR_INTERNAL_INTERRUPT_8C, // UNUSED
    VECTOR_INTERNAL_INTERRUPT_90, // FRT1 ICI
    VECTOR_INTERNAL_INTERRUPT_94, // FRT1 OCIA
    VECTOR_INTERNAL_INTERRUPT_98, // FRT1 OCIB
    VECTOR_INTERNAL_INTERRUPT_9C, // FRT1 FOVI
    VECTOR_INTERNAL_INTERRUPT_A0, // FRT2 ICI
    VECTOR_INTERNAL_INTERRUPT_A4, // FRT2 OCIA
    VECTOR_INTERNAL_INTERRUPT_A8, // FRT2 OCIB
    VECTOR_INTERNAL_INTERRUPT_AC, // FRT2 FOVI
    VECTOR_INTERNAL_INTERRUPT_B0, // FRT3 ICI
    VECTOR_INTERNAL_INTERRUPT_B4, // FRT3 OCIA
    VECTOR_INTERNAL_INTERRUPT_B8, // FRT3 OCIB
    VECTOR_INTERNAL_INTERRUPT_BC, // FRT3 FOVI
    VECTOR_INTERNAL_INTERRUPT_C0, // CMIA
    VECTOR_INTERNAL_INTERRUPT_C4, // CMIB
    VECTOR_INTERNAL_INTERRUPT_C8, // OVI
    VECTOR_INTERNAL_INTERRUPT_CC, // UNUSED
    VECTOR_INTERNAL_INTERRUPT_D0, // ERI
    VECTOR_INTERNAL_INTERRUPT_D4, // RXI
    VECTOR_INTERNAL_INTERRUPT_D8, // TXI
    VECTOR_INTERNAL_INTERRUPT_DC, // UNUSED
    VECTOR_INTERNAL_INTERRUPT_E0, // ADI
};

static uint8_t MCU_Read(uint32_t address);
static uint16_t MCU_Read16(uint32_t address);
static uint32_t MCU_Read32(uint32_t address);
static void MCU_Write(uint32_t address, uint8_t value);
static void MCU_Write16(uint32_t address, uint16_t value);
static uint8_t MCU_ReadP0(void);
static uint8_t MCU_ReadP1(void);
static void MCU_GA_SetGAInt(uint8_t line, int value);
static void MCU_PostSample(int *sample);

static inline void MCU_ErrorTrap(void) {
    //printf("%.2x %.4x\n", mcu.cp, mcu.pc);
}

static inline void MCU_WriteP0(uint8_t data) {
    mcu_p0_data = data;
}

static inline void MCU_WriteP1(uint8_t data) {
    //mcu_p1_data = data;
}

static inline uint32_t MCU_GetAddress(uint8_t page, uint16_t address) {
    return (page << 16) + address;
}

static inline uint8_t MCU_ReadCode(void) {
    return MCU_Read(MCU_GetAddress(mcu.cp, mcu.pc));
}

static inline uint8_t MCU_ReadCodeAdvance(void) {
    uint8_t ret = MCU_ReadCode();
    mcu.pc++;
    return ret;
}

static inline uint32_t MCU_GetVectorAddress(uint32_t vector)
{
    return MCU_Read32(vector * 4);
}

static inline uint32_t MCU_GetPageForRegister(uint32_t reg)
{
    if (reg >= 6)
        return mcu.tp;
    else if (reg >= 4)
        return mcu.ep;
    return mcu.dp;
}

static inline void MCU_ControlRegisterWrite(uint32_t reg, uint32_t siz, uint32_t data)
{
    if (siz)
    {
        if (reg == 0)
        {
            mcu.sr = data;
            mcu.sr &= sr_mask;
        }
        else if (reg == 5) // FIXME: undocumented
        {
            mcu.dp = data & 0xff;
        }
        else if (reg == 4) // FIXME: undocumented
        {
            mcu.ep = data & 0xff;
        }
        else if (reg == 3) // FIXME: undocumented
        {
            mcu.br = data & 0xff;
        }
        else
        {
            MCU_ErrorTrap();
        }
    }
    else
    {
        if (reg == 1)
        {
            mcu.sr &= ~0xff;
            mcu.sr |= data & 0xff;
            mcu.sr &= sr_mask;
        }
        else if (reg == 3)
        {
            mcu.br = data;
        }
        else if (reg == 4)
        {
            mcu.ep = data;
        }
        else if (reg == 5)
        {
            mcu.dp = data;
        }
        else if (reg == 7)
        {
            mcu.tp = data;
        }
        else
        {
            MCU_ErrorTrap();
        }
    }
}

static inline uint32_t MCU_ControlRegisterRead(uint32_t reg, uint32_t siz)
{
    uint32_t ret = 0;
    if (siz)
    {
        if (reg == 0)
        {
            ret = mcu.sr & sr_mask;
        }
        else if (reg == 5) // FIXME: undocumented
        {
            ret = mcu.dp | (mcu.dp << 8);
        }
        else if (reg == 4) // FIXME: undocumented
        {
            ret = mcu.ep | (mcu.ep << 8);
        }
        else if (reg == 3) // FIXME: undocumented
        {
            ret = mcu.br | (mcu.br << 8);;
        }
        else
        {
            MCU_ErrorTrap();
        }
        ret &= 0xffff;
    }
    else
    {
        if (reg == 1)
        {
            ret = mcu.sr & sr_mask;
        }
        else if (reg == 3)
        {
            ret = mcu.br;
        }
        else if (reg == 4)
        {
            ret = mcu.ep;
        }
        else if (reg == 5)
        {
            ret = mcu.dp;
        }
        else if (reg == 7)
        {
            ret = mcu.tp;
        }
        else
        {
            MCU_ErrorTrap();
        }
        ret &= 0xff;
    }
    return ret;
}

static inline void MCU_SetStatus(uint32_t condition, uint32_t mask)
{
    if (condition)
        mcu.sr |= mask;
    else
        mcu.sr &= ~mask;
}

static inline void MCU_PushStack(uint16_t data)
{
    if (mcu.r[7] & 1)
        MCU_Interrupt_Exception(EXCEPTION_SOURCE_ADDRESS_ERROR);
    mcu.r[7] -= 2;
    MCU_Write16(mcu.r[7], data);
}

static inline uint16_t MCU_PopStack(void)
{
    uint16_t ret;
    if (mcu.r[7] & 1)
        MCU_Interrupt_Exception(EXCEPTION_SOURCE_ADDRESS_ERROR);
    ret = MCU_Read16(mcu.r[7]);
    mcu.r[7] += 2;
    return ret;
}
#endif // NUKEDSC55_MCU_H

#ifndef NUKEDSC55_MCU_INTERRUPT_CPP
#define NUKEDSC55_MCU_INTERRUPT_CPP
static void MCU_Interrupt_Start(int32_t mask)
{
    MCU_PushStack(mcu.pc);
    MCU_PushStack(mcu.cp);
    MCU_PushStack(mcu.sr);
    mcu.sr &= ~STATUS_T;
    if (mask >= 0)
    {
        mcu.sr &= ~STATUS_INT_MASK;
        mcu.sr |= mask << 8;
    }
    mcu.sleep = 0;
}

static inline void MCU_Interrupt_SetRequest(uint32_t interrupt, uint32_t value)
{
    mcu.interrupt_pending = (value ? (mcu.interrupt_pending | (1<<interrupt)) : (mcu.interrupt_pending & (~(1<<interrupt))));
}

static inline void MCU_Interrupt_Exception(uint32_t exception)
{
#if 0
    if (interrupt == INTERRUPT_SOURCE_IRQ0 && (dev_register[DEV_P1CR] & 0x20) == 0)
        return;
    if (interrupt == INTERRUPT_SOURCE_IRQ1 && (dev_register[DEV_P1CR] & 0x40) == 0)
        return;
#endif
    mcu.exception_pending = exception;
}

static inline void MCU_Interrupt_TRAPA(uint32_t vector)
{
    mcu.trapa_pending |= (1<<vector);
}

static inline void MCU_Interrupt_StartVector(uint32_t vector, int32_t mask)
{
    uint32_t address = MCU_GetVectorAddress(vector);
    MCU_Interrupt_Start(mask);
    mcu.cp = address >> 16;
    mcu.pc = address;
}

static void MCU_Interrupt_Handle(void)
{
#if 0
    if (mcu.cycles % 2000 == 0 && mcu.sleep)
    {
        MCU_Interrupt_StartVector(VECTOR_INTERNAL_INTERRUPT_94);
        return;
    }
    if (mcu.cycles % 2000 == 1000 && mcu.sleep)
    {
        MCU_Interrupt_StartVector(VECTOR_INTERNAL_INTERRUPT_A4);
        return;
    }
    if (mcu.cycles % 2000 == 1500 && mcu.sleep)
    {
        MCU_Interrupt_StartVector(VECTOR_INTERNAL_INTERRUPT_B4);
        return;
    }
#endif
    uint32_t i;
    // Change mcu interrupt_pending and trapa_pending to be bitmasks to make this faster
    if (mcu.trapa_pending) for (i = 0; i < 16; i++)
    {
        if (mcu.trapa_pending & (1<<i))
        {
            mcu.trapa_pending &= ~(1<<i);
            MCU_Interrupt_StartVector(VECTOR_TRAPA_0 + i, -1);
            return;
        }
    }
    if (mcu.exception_pending >= 0)
    {
        switch (mcu.exception_pending)
        {
            case EXCEPTION_SOURCE_ADDRESS_ERROR:
                MCU_Interrupt_StartVector(VECTOR_ADDRESS_ERROR, -1);
                break;
            case EXCEPTION_SOURCE_INVALID_INSTRUCTION:
                MCU_Interrupt_StartVector(VECTOR_INVALID_INSTRUCTION, -1);
                break;
            case EXCEPTION_SOURCE_TRACE:
                MCU_Interrupt_StartVector(VECTOR_TRACE, -1);
                break;

        }
        mcu.exception_pending = -1;
        return;
    }
    if (!mcu.interrupt_pending)
        return;
    // Commented out because INTERRUPT_SOURCE_NMI is never set to be pending
    //if (mcu.interrupt_pending & (1<<INTERRUPT_SOURCE_NMI))
    //{
    //    // mcu.interrupt_pending[INTERRUPT_SOURCE_NMI] = 0;
    //    MCU_Interrupt_StartVector(VECTOR_NMI, 7);
    //    return;
    //}
    uint32_t mask = (mcu.sr >> 8) & 7;
    for (i = INTERRUPT_SOURCE_NMI + 1; i < INTERRUPT_SOURCE_MAX; i++)
    {
        int32_t vector = -1;
        int32_t level = 0;
        if (!(mcu.interrupt_pending & (1<<i)))
            continue;
        switch (i)
        {
            case INTERRUPT_SOURCE_IRQ0:
                if ((dev_register[DEV_P1CR] & 0x20) == 0)
                    continue;
                vector = VECTOR_IRQ0;
                level = (dev_register[DEV_IPRA] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_IRQ1:
                if ((dev_register[DEV_P1CR] & 0x40) == 0)
                    continue;
                vector = VECTOR_IRQ1;
                level = (dev_register[DEV_IPRA] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_FRT0_OCIA:
                vector = VECTOR_INTERNAL_INTERRUPT_94;
                level = (dev_register[DEV_IPRB] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_FRT0_OCIB:
                vector = VECTOR_INTERNAL_INTERRUPT_98;
                level = (dev_register[DEV_IPRB] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_FRT0_FOVI:
                vector = VECTOR_INTERNAL_INTERRUPT_9C;
                level = (dev_register[DEV_IPRB] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_FRT1_OCIA:
                vector = VECTOR_INTERNAL_INTERRUPT_A4;
                level = (dev_register[DEV_IPRB] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_FRT1_OCIB:
                vector = VECTOR_INTERNAL_INTERRUPT_A8;
                level = (dev_register[DEV_IPRB] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_FRT1_FOVI:
                vector = VECTOR_INTERNAL_INTERRUPT_AC;
                level = (dev_register[DEV_IPRB] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_FRT2_OCIA:
                vector = VECTOR_INTERNAL_INTERRUPT_B4;
                level = (dev_register[DEV_IPRC] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_FRT2_OCIB:
                vector = VECTOR_INTERNAL_INTERRUPT_B8;
                level = (dev_register[DEV_IPRC] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_FRT2_FOVI:
                vector = VECTOR_INTERNAL_INTERRUPT_BC;
                level = (dev_register[DEV_IPRC] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_TIMER_CMIA:
                vector = VECTOR_INTERNAL_INTERRUPT_C0;
                level = (dev_register[DEV_IPRC] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_TIMER_CMIB:
                vector = VECTOR_INTERNAL_INTERRUPT_C4;
                level = (dev_register[DEV_IPRC] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_TIMER_OVI:
                vector = VECTOR_INTERNAL_INTERRUPT_C8;
                level = (dev_register[DEV_IPRC] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_ANALOG:
                vector = VECTOR_INTERNAL_INTERRUPT_E0;
                level = (dev_register[DEV_IPRD] >> 0) & 7;
                break;
            case INTERRUPT_SOURCE_UART_RX:
                vector = VECTOR_INTERNAL_INTERRUPT_D4;
                level = (dev_register[DEV_IPRD] >> 4) & 7;
                break;
            case INTERRUPT_SOURCE_UART_TX:
                vector = VECTOR_INTERNAL_INTERRUPT_D8;
                level = (dev_register[DEV_IPRD] >> 4) & 7;
                break;
            default:
                break;
        }

        if ((int32_t)mask < level)
        {
            // mcu.interrupt_pending[INTERRUPT_SOURCE_NMI] = 0;
            MCU_Interrupt_StartVector(vector, level);
            return;
        }
    }
}
#endif // NUKEDSC55_MCU_INTERRUPT_CPP

#ifndef NUKEDSC55_MCU_OPCODES_CPP
#define NUKEDSC55_MCU_OPCODES_CPP
static int32_t MCU_SUB_Common(int32_t t1, int32_t t2, int32_t c_bit, uint32_t siz)
{
    int32_t st1, st2;
    int32_t N, Z, C, V = 0;
    if (siz)
    {
        st1 = (int16_t)t1;
        st2 = (int16_t)t2;
        t1 = (uint16_t)t1;
        t2 = (uint16_t)t2;
        t1 -= t2;
        t1 -= c_bit;
        C = (t1 >> 16) & 1;

        t1 &= 0xffff;
        N = (t1 & 0x8000) != 0;
        Z = t1 == 0;

        st1 -= st2;
        st1 -= c_bit;
        if (st1 < INT16_MIN || st1 > INT16_MAX)
            V = 1;
    }
    else
    {
        st1 = (int8_t)t1;
        st2 = (int8_t)t2;
        t1 = (uint8_t)t1;
        t2 = (uint8_t)t2;
        t1 -= t2;
        t1 -= c_bit;
        C = (t1 >> 8) & 1;

        t1 &= 0xff;
        N = (t1 & 0x80) != 0;
        Z = t1 == 0;

        st1 -= st2;
        st1 -= c_bit;
        if (st1 < INT8_MIN || st1 > INT8_MAX)
            V = 1;
    }
    MCU_SetStatus(N, STATUS_N);
    MCU_SetStatus(Z, STATUS_Z);
    MCU_SetStatus(C, STATUS_C);
    MCU_SetStatus(V, STATUS_V);

    return t1;
}

static int32_t MCU_ADD_Common(int32_t t1, int32_t t2, int32_t c_bit, uint32_t siz)
{
    int32_t st1, st2;
    int32_t N, Z, C, V = 0;
    if (siz)
    {
        st1 = (int16_t)t1;
        st2 = (int16_t)t2;
        t1 = (uint16_t)t1;
        t2 = (uint16_t)t2;
        t1 += t2;
        t1 += c_bit;
        C = (t1 >> 16) & 1;

        t1 &= 0xffff;
        N = (t1 & 0x8000) != 0;
        Z = t1 == 0;

        st1 += st2;
        st1 += c_bit;
        if (st1 < INT16_MIN || st1 > INT16_MAX)
            V = 1;
    }
    else
    {
        st1 = (int8_t)t1;
        st2 = (int8_t)t2;
        t1 = (uint8_t)t1;
        t2 = (uint8_t)t2;
        t1 += t2;
        t1 += c_bit;
        C = (t1 >> 8) & 1;

        t1 &= 0xff;
        N = (t1 & 0x80) != 0;
        Z = t1 == 0;

        st1 += st2;
        st1 += c_bit;
        if (st1 < INT8_MIN || st1 > INT8_MAX)
            V = 1;
    }
    MCU_SetStatus(N, STATUS_N);
    MCU_SetStatus(Z, STATUS_Z);
    MCU_SetStatus(C, STATUS_C);
    MCU_SetStatus(V, STATUS_V);

    return t1;
}

static void MCU_Operand_Nop(uint8_t operand)
{
}

static void MCU_Operand_Sleep(uint8_t operand)
{
    mcu.sleep = 1;
}

static void MCU_Operand_NotImplemented(uint8_t operand)
{
    MCU_ErrorTrap();
}

enum {
    GENERAL_DIRECT = 0,
    GENERAL_INDIRECT,
    GENERAL_ABSOLUTE,
    GENERAL_IMMEDIATE
};

enum {
    OPERAND_BYTE = 0,
    OPERAND_WORD
};

enum {
    INCREASE_NONE = 0,
    INCREASE_DECREASE,
    INCREASE_INCREASE
};

static void MCU_LDM(uint8_t operand)
{
    uint8_t rlist = MCU_ReadCodeAdvance();
    int32_t i;
    for (i = 0; i < 8; i++)
    {
        if (rlist & (1 << i))
        {
            uint16_t data = MCU_PopStack();
            if (i != 7)
                mcu.r[i] = data;
        }
    }
}

static void MCU_STM(uint8_t operand)
{
    uint8_t rlist = MCU_ReadCodeAdvance();
    int32_t i;
    for (i = 7; i >= 0; i--)
    {
        if (rlist & (1 << i))
        {
            uint16_t data = mcu.r[i];
            if (i == 7)
                data -= 2;
            MCU_PushStack(data);
        }
    }
}

static void MCU_TRAPA(uint8_t operand)
{
    uint32_t opcode = MCU_ReadCodeAdvance();
    if ((opcode & 0xf0) == 0x10)
    {
        MCU_Interrupt_TRAPA(opcode & 0x0f);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Jump_PJSR(uint8_t operand)
{
    //uint32_t ocp = mcu.cp;
    //uint32_t opc = mcu.pc;
    uint8_t page = MCU_ReadCodeAdvance();
    uint16_t address;
    address = MCU_ReadCodeAdvance() << 8;
    address |= MCU_ReadCodeAdvance();
    MCU_PushStack(mcu.pc);
    MCU_PushStack(mcu.cp);
    mcu.cp = page;
    if (mcu.cp == 0x27)
        mcu.cp += 0;
    mcu.pc = address;
}

static void MCU_Jump_JSR(uint8_t operand)
{
    uint16_t address;
    address = MCU_ReadCodeAdvance() << 8;
    address |= MCU_ReadCodeAdvance();
    MCU_PushStack(mcu.pc);
    mcu.pc = address;
}

static void MCU_Jump_RTE(uint8_t operand)
{
    mcu.sr = MCU_PopStack();
    mcu.cp = (uint8_t)MCU_PopStack();
    mcu.pc = MCU_PopStack();
    mcu.ex_ignore = 1;
}

static void MCU_Jump_Bcc(uint8_t operand)
{
    uint16_t disp;
    uint32_t cond;
    uint32_t branch = 0;
    uint32_t N, C, Z, V;
    if (operand & 0x10)
    {
        disp = MCU_ReadCodeAdvance() << 8;
        disp |= MCU_ReadCodeAdvance();
    }
    else
    {
        disp = (int8_t)MCU_ReadCodeAdvance();
    }
    cond = operand & 0x0f;

    N = (mcu.sr & STATUS_N) != 0;
    C = (mcu.sr & STATUS_C) != 0;
    Z = (mcu.sr & STATUS_Z) != 0;
    V = (mcu.sr & STATUS_V) != 0;

    switch (cond)
    {
    case 0x0: // BRA/BT
        branch = 1;
        break;
    case 0x1: // BRN/BF
        branch = 0;
        break;
    case 0x2: // BHI
        branch = (C | Z) == 0;
        break;
    case 0x3: // BLS
        branch = (C | Z) == 1;
        break;
    case 0x4: // BCC/BHS
        branch = C == 0;
        break;
    case 0x5: // BCS/BLO
        branch = C == 1;
        break;
    case 0x6: // BNE
        branch = Z == 0;
        break;
    case 0x7: // BEQ
        branch = Z == 1;
        break;
    case 0x8: // BVC
        branch = V == 0;
        break;
    case 0x9: // BVS
        branch = V == 1;
        break;
    case 0xa: // BPL
        branch = N == 0;
        break;
    case 0xb: // BMI
        branch = N == 1;
        break;
    case 0xc: // BGE
        branch = (N ^ V) == 0;
        break;
    case 0xd: // BLT
        branch = (N ^ V) == 1;
        break;
    case 0xe: // BGT
        branch = (Z | (N ^ V)) == 0;
        break;
    case 0xf: // BLE
        branch = (Z | (N ^ V)) == 1;
        break;
    }

    if (branch)
    {
        mcu.pc += disp;
    }
}

static void MCU_Jump_RTS(uint8_t operand)
{
    mcu.pc = MCU_PopStack();
}

static void MCU_Jump_RTD(uint8_t operand)
{
    int16_t imm = (int8_t)MCU_ReadCodeAdvance();
    mcu.pc = MCU_PopStack();

    if (operand == 0x14)
    {
        mcu.r[7] += imm;
        if (mcu.r[7] & 1)
            MCU_ErrorTrap();
    }
    else if (operand == 0x1c)
    {
        // TODO
        MCU_ErrorTrap();
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Jump_JMP(uint8_t operand)
{
    if (operand == 0x11)
    {
        uint8_t opcode = MCU_ReadCodeAdvance();
        uint8_t opcode_h = opcode >> 3;
        uint8_t opcode_l = opcode & 0x07;
        if (opcode == 0x19)
        {
            mcu.cp = (uint8_t)MCU_PopStack();
            mcu.pc = MCU_PopStack();
        }
        else if (opcode_h == 0x19)
        {
            MCU_PushStack(mcu.pc);
            MCU_PushStack(mcu.cp);
            opcode_l &= ~1;
            mcu.cp = mcu.r[opcode_l] & 0xff;
            mcu.pc = mcu.r[opcode_l + 1];
        }
        else if (opcode_h == 0x1a)
        {
            mcu.pc = mcu.r[opcode_l];
        }
        else if (opcode_h == 0x1b)
        {
            MCU_PushStack(mcu.pc);
            mcu.pc = mcu.r[opcode_l];
        }
        else
        {
            MCU_ErrorTrap();
        }
    }
    else if (operand == 0x01)
    {
        uint8_t opcode = MCU_ReadCodeAdvance();
        uint8_t reg = opcode & 0x07;
        opcode >>= 3;
        if (opcode == 0x17)
        {
            uint16_t disp = (int8_t)MCU_ReadCodeAdvance();
            mcu.r[reg]--;
            if (mcu.r[reg] != 0xffff)
            {
                mcu.pc += disp;
            }
        }
        else
        {
            MCU_ErrorTrap();
        }
    }
    else if (operand == 0x10)
    {
        uint32_t addr;
        addr = MCU_ReadCodeAdvance() << 8;
        addr |= MCU_ReadCodeAdvance();
        mcu.pc = addr;
    }
    else if (operand == 0x06)
    {
        uint8_t opcode = MCU_ReadCodeAdvance();
        uint8_t reg = opcode & 0x07;
        opcode >>= 3;
        if (opcode == 0x17)
        {
            uint16_t disp = (int8_t)MCU_ReadCodeAdvance();
            uint32_t Z = (mcu.sr & STATUS_Z) != 0;
            if (Z)
            {
                mcu.r[reg]--;
                if (mcu.r[reg] != 0xffff)
                {
                    mcu.pc += disp;
                }
            }
        }
        else
        {
            MCU_ErrorTrap();
        }
    }
    else if (operand == 0x07)
    {
        uint8_t opcode = MCU_ReadCodeAdvance();
        uint8_t reg = opcode & 0x07;
        opcode >>= 3;
        if (opcode == 0x17)
        {
            uint16_t disp = (int8_t)MCU_ReadCodeAdvance();
            uint32_t Z = (mcu.sr & STATUS_Z) != 0;
            if (!Z)
            {
                mcu.r[reg]--;
                if (mcu.r[reg] != 0xffff)
                {
                    mcu.pc += disp;
                }
            }
        }
        else
        {
            MCU_ErrorTrap();
        }
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Jump_BSR(uint8_t operand)
{
    uint16_t disp;
    if (operand == 0x0e)
    {
        disp = (int8_t)MCU_ReadCodeAdvance();
    }
    else
    {
        disp = MCU_ReadCodeAdvance() << 8;
        disp |= MCU_ReadCodeAdvance();
    }
    MCU_PushStack(mcu.pc);
    mcu.pc += disp;
}

static void MCU_Jump_PJMP(uint8_t operand)
{
    uint8_t page;
    uint16_t address;
    page = MCU_ReadCodeAdvance();
    address = MCU_ReadCodeAdvance() << 8;
    address |= MCU_ReadCodeAdvance();
    mcu.cp = page;
    mcu.pc = address;
}

static uint32_t MCU_Operand_Read(void)
{
    switch (operand_type)
    {
    case GENERAL_DIRECT:
        if (operand_size)
            return mcu.r[operand_reg];
        return mcu.r[operand_reg] & 0xff;
    case GENERAL_INDIRECT:
    case GENERAL_ABSOLUTE:
        if (operand_size)
        {
            if (operand_ea & 1)
            {
                MCU_Interrupt_Exception(EXCEPTION_SOURCE_ADDRESS_ERROR);
            }
            return MCU_Read16(MCU_GetAddress(operand_ep, operand_ea));
        }
        return MCU_Read(MCU_GetAddress(operand_ep, operand_ea));
    case GENERAL_IMMEDIATE:
        return operand_data;
    }
    return 0;
}

static void MCU_Operand_Write(uint32_t data)
{
    switch (operand_type)
    {
    case GENERAL_DIRECT:
        if (operand_size)
            mcu.r[operand_reg] = data;
        else
        {
            mcu.r[operand_reg] &= ~0xff;
            mcu.r[operand_reg] |= data & 0xff;
        }
        break;
    case GENERAL_INDIRECT:
    case GENERAL_ABSOLUTE:
        if (operand_size)
        {
            if (operand_ea & 1)
            {
                MCU_Interrupt_Exception(EXCEPTION_SOURCE_ADDRESS_ERROR);
            }
            MCU_Write16(MCU_GetAddress(operand_ep, operand_ea), data);
        }
        else
            MCU_Write(MCU_GetAddress(operand_ep, operand_ea), data);
        break;
    case GENERAL_IMMEDIATE:
        MCU_Interrupt_Exception(EXCEPTION_SOURCE_INVALID_INSTRUCTION);
        break;
    }
}

static void MCU_Operand_General(uint8_t operand)
{
    uint32_t type = GENERAL_DIRECT;
    uint32_t disp = 0;
    uint32_t increase = INCREASE_NONE;
    //uint32_t absolute = 0;
    uint32_t reg = 0;
    uint32_t siz = OPERAND_BYTE;
    uint32_t data = 0;
    uint32_t addr = 0;
    uint32_t addrpage = 0;
    uint32_t ea = 0;
    uint32_t ep = 0;
    uint8_t opcode;
    uint8_t opcode_reg;
    if (operand & 0x08)
        siz = OPERAND_WORD;
    else
        siz = OPERAND_BYTE;
    reg = operand & 0x07;
    switch (operand & 0xf0)
    {
    case 0xa0:
        type = GENERAL_DIRECT;
        break;
    case 0xd0:
        type = GENERAL_INDIRECT;
        break;
    case 0xe0:
        type = GENERAL_INDIRECT;
        disp = (int8_t)MCU_ReadCodeAdvance();
        break;
    case 0xf0:
        type = GENERAL_INDIRECT;
        disp = MCU_ReadCodeAdvance();
        disp <<= 8;
        disp |= MCU_ReadCodeAdvance();
        break;
    case 0xb0:
        type = GENERAL_INDIRECT;
        increase = INCREASE_DECREASE;
        break;
    case 0xc0:
        type = GENERAL_INDIRECT;
        increase = INCREASE_INCREASE;
        break;
    case 0x00:
        if (reg == 5)
        {
            type = GENERAL_ABSOLUTE;
            addr = mcu.br << 8;
            addr |= MCU_ReadCodeAdvance();
            addrpage = 0;
        }
        else if (reg == 4)
        {
            type = GENERAL_IMMEDIATE;
            data = MCU_ReadCodeAdvance();
            if (siz)
            {
                data <<= 8;
                data |= MCU_ReadCodeAdvance();
            }
        }
        break;
    case 0x10:
        if (reg == 5)
        {
            type = GENERAL_ABSOLUTE;
            addr = MCU_ReadCodeAdvance() << 8;
            addr |= MCU_ReadCodeAdvance();
            addrpage = mcu.dp;
        }
        break;
    }
    if (type == GENERAL_INDIRECT)
    {
        if (increase == INCREASE_DECREASE)
        {
            if (siz || reg == 7)
            {
                mcu.r[reg] -= 2;
            }
            else
            {
                mcu.r[reg] -= 1;
            }
        }
        ea = mcu.r[reg] + disp;
        if (increase == INCREASE_INCREASE)
        {
            if (siz || reg == 7)
            {
                mcu.r[reg] += 2;
            }
            else
            {
                mcu.r[reg] += 1;
            }
        }

        ea &= 0xffff;

        ep = MCU_GetPageForRegister(reg) & 0xff;
    }
    else if (type == GENERAL_ABSOLUTE)
    {
        ea = addr & 0xffff;

        ep = addrpage & 0xff;
    }

    opcode = MCU_ReadCodeAdvance();
    opcode_extended = opcode == 0x00;
    if (opcode_extended)
    {
        opcode = MCU_ReadCodeAdvance();
    }
    opcode_reg = opcode & 0x07;
    opcode >>= 3;

    operand_type = type;
    operand_ea = ea;
    operand_ep = ep;
    operand_size = siz;
    operand_reg = reg;
    operand_data = data;
    //operand_status = 0;

    MCU_Opcode_Table[opcode](opcode, opcode_reg);
}

static void MCU_SetStatusCommon(uint32_t val, uint32_t siz)
{
    if (siz)
        val &= 0xffff;
    else
        val &= 0xff;
    if (siz)
        MCU_SetStatus(val & 0x8000, STATUS_N);
    else
        MCU_SetStatus(val & 0x80, STATUS_N);
    MCU_SetStatus(val == 0, STATUS_Z);
    MCU_SetStatus(0, STATUS_V);
}

static void MCU_Opcode_Short_MOVE(uint8_t opcode)
{
    uint32_t reg = opcode & 0x07;
    uint8_t data = MCU_ReadCodeAdvance();
    mcu.r[reg] &= ~0xff;
    mcu.r[reg] |= data;
    MCU_SetStatusCommon(data, 0);
}

static void MCU_Opcode_Short_MOVI(uint8_t opcode)
{
    uint32_t reg = opcode & 0x07;
    uint16_t data;
    data = MCU_ReadCodeAdvance() << 8;
    data |= MCU_ReadCodeAdvance();
    mcu.r[reg] = data;
    MCU_SetStatusCommon(data, 1);
}

static void MCU_Opcode_Short_MOVF(uint8_t opcode)
{
    uint32_t reg = opcode & 0x07;
    uint32_t siz = (opcode & 0x08) != 0;
    int8_t disp = MCU_ReadCodeAdvance();
    uint32_t addr = (mcu.r[6] + disp) & 0xffff;
    addr |= mcu.tp << 16;
    if ((opcode & 0x10) == 0)
    {
        uint16_t data;
        if (siz)
        {
            data = MCU_Read16(addr);
            mcu.r[reg] &= ~0xff;
            mcu.r[reg] |= data;
            MCU_SetStatusCommon(data, 0);
        }
        else
        {
            data = MCU_Read(addr);
            mcu.r[reg] = data;
            MCU_SetStatusCommon(data, 1);
        }
    }
    else
    {
        uint16_t data;
        if (siz)
        {
            data = mcu.r[reg] & 0xff;
            MCU_Write(addr, (uint8_t)data);
            MCU_SetStatusCommon(data, 0);
        }
        else
        {
            data = mcu.r[reg];
            MCU_Write16(addr, data);
            MCU_SetStatusCommon(data, 1);
        }
    }
}

static void MCU_Opcode_Short_MOVL(uint8_t opcode)
{
    uint32_t reg = opcode & 0x07;
    uint32_t siz = (opcode & 0x08) != 0;
    uint16_t addr = mcu.br << 8;
    uint32_t data;
    addr |= MCU_ReadCodeAdvance();
    if (siz)
    {
        if (addr & 1)
            MCU_Interrupt_Exception(EXCEPTION_SOURCE_ADDRESS_ERROR);
        data = MCU_Read16(addr);
        mcu.r[reg] = data;
        MCU_SetStatusCommon(data, 1);
    }
    else
    {
        data = MCU_Read(addr);
        mcu.r[reg] &= ~0xff;
        mcu.r[reg] |= data;
        MCU_SetStatusCommon(data, 0);
    }
}

static void MCU_Opcode_Short_MOVS(uint8_t opcode)
{
    uint32_t reg = opcode & 0x07;
    uint32_t siz = (opcode & 0x08) != 0;
    uint16_t addr = mcu.br << 8;
    uint32_t data;
    addr |= MCU_ReadCodeAdvance();
    if (siz)
    {
        if (addr & 1)
            MCU_Interrupt_Exception(EXCEPTION_SOURCE_ADDRESS_ERROR);
        data = mcu.r[reg];
        MCU_Write16(addr, data);
        MCU_SetStatusCommon(data, 1);
    }
    else
    {
        data = mcu.r[reg] & 0xff;
        MCU_Write(addr, data);
        MCU_SetStatusCommon(data, 0);
    }
}

static void MCU_Opcode_Short_CMP(uint8_t opcode)
{
    uint32_t reg = opcode & 0x07;
    uint32_t siz = (opcode & 0x08) != 0;
    int32_t t1, t2;
    if (siz)
    {
        t2 = MCU_ReadCodeAdvance() << 8;
        t2 |= MCU_ReadCodeAdvance();
    }
    else
    {
        t2 = MCU_ReadCodeAdvance();
    }
    t1 = mcu.r[reg];
    MCU_SUB_Common(t1, t2, 0, siz);
}

static void MCU_Opcode_NotImplemented(uint8_t opcode, uint8_t opcode_reg)
{
    MCU_ErrorTrap();
}

static void MCU_Opcode_MOVG_Immediate(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t data;
    if (opcode_reg == 6 && (operand_type == GENERAL_INDIRECT || operand_type == GENERAL_ABSOLUTE))
    {
        data = (int8_t)MCU_ReadCodeAdvance();
        MCU_Operand_Write(data);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 7 && (operand_type == GENERAL_INDIRECT || operand_type == GENERAL_ABSOLUTE))
    {
        data = MCU_ReadCodeAdvance() << 8;
        data |= MCU_ReadCodeAdvance();
        MCU_Operand_Write(data);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 4 && (operand_type == GENERAL_INDIRECT || operand_type == GENERAL_ABSOLUTE) && operand_size == OPERAND_BYTE)
    {
        uint32_t t1 = MCU_Operand_Read();
        uint32_t t2 = MCU_ReadCodeAdvance();
        MCU_SUB_Common(t1, t2, 0, OPERAND_BYTE);
    }
    else if (opcode_reg == 4 && (operand_type == GENERAL_INDIRECT || operand_type == GENERAL_ABSOLUTE) && operand_size == OPERAND_WORD) // FIXME
    {
        uint32_t t1 = MCU_Operand_Read();
        uint32_t t2 = (uint16_t)((int8_t)MCU_ReadCodeAdvance());
        MCU_SUB_Common(t1, t2, 0, OPERAND_WORD);
    }
    else if (opcode_reg == 5 && (operand_type == GENERAL_INDIRECT || operand_type == GENERAL_ABSOLUTE) && operand_size == OPERAND_WORD)
    {
        uint32_t t1, t2;
        t1 = MCU_Operand_Read();
        t2 = MCU_ReadCodeAdvance() << 8;
        t2 |= MCU_ReadCodeAdvance();
        MCU_SUB_Common(t1, t2, 0, OPERAND_WORD);
    }
    else if (opcode_reg == 5 && (operand_type == GENERAL_INDIRECT || operand_type == GENERAL_ABSOLUTE) && operand_size == OPERAND_BYTE) // FIXME
    {
        uint32_t t1, t2;
        t1 = MCU_Operand_Read();
        t2 = MCU_ReadCodeAdvance() << 8;
        t2 |= MCU_ReadCodeAdvance();
        MCU_SUB_Common(t1, t2, 0, OPERAND_BYTE);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_BSET_ORC(uint8_t opcode, uint8_t opcode_reg)
{
    if (operand_type == GENERAL_IMMEDIATE) // ORC
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t val = MCU_ControlRegisterRead(opcode_reg, operand_size);
        val |= data;
        MCU_ControlRegisterWrite(opcode_reg, operand_size, val);
        if (opcode_reg >= 2)
        {
            MCU_SetStatusCommon(val, operand_size);
        }
        mcu.ex_ignore = 1;
    }
    else // BSET
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = mcu.r[opcode_reg] & 0x0f;
        MCU_SetStatus((data & (1 << bit)) == 0, STATUS_Z);
        data |= 1 << bit;
        MCU_Operand_Write(data);
    }
}

static void MCU_Opcode_BCLR_ANDC(uint8_t opcode, uint8_t opcode_reg)
{
    if (operand_type == GENERAL_IMMEDIATE) // ANDC
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t val = MCU_ControlRegisterRead(opcode_reg, operand_size);
        val &= data;
        MCU_ControlRegisterWrite(opcode_reg, operand_size, val);
        if (opcode_reg >= 2)
        {
            MCU_SetStatusCommon(val, operand_size);
        }
        mcu.ex_ignore = 1;
    }
    else // BCLR
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = mcu.r[opcode_reg] & 0x0f;
        MCU_SetStatus((data & (1 << bit)) == 0, STATUS_Z);
        data &= ~(1 << bit);
        MCU_Operand_Write(data);
    }
}

static void MCU_Opcode_BTST(uint8_t opcode, uint8_t opcode_reg)
{
    if (operand_type != GENERAL_IMMEDIATE)
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = mcu.r[opcode_reg] & 0x0f;
        MCU_SetStatus((data & (1 << bit)) == 0, STATUS_Z);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_CLR(uint8_t opcode, uint8_t opcode_reg)
{
    if (opcode_reg == 3 && operand_type != GENERAL_IMMEDIATE) // CLR
    {
        MCU_Operand_Write(0);
        MCU_SetStatus(0, STATUS_N);
        MCU_SetStatus(1, STATUS_Z);
        MCU_SetStatus(0, STATUS_V);
        MCU_SetStatus(0, STATUS_C);
    }
    else if (opcode_reg == 6 && operand_type != GENERAL_IMMEDIATE) // TST
    {
        uint32_t data = MCU_Operand_Read();
        MCU_SetStatusCommon(data, operand_size);
        MCU_SetStatus(0, STATUS_C);
    }
    else if (opcode_reg == 2 && operand_type == GENERAL_DIRECT && operand_size == 0) // EXTU
    {
        uint32_t data = (uint8_t)mcu.r[operand_reg];
        mcu.r[operand_reg] = data;
        MCU_SetStatus(0, STATUS_N);
        MCU_SetStatus(data == 0, STATUS_Z);
        MCU_SetStatus(0, STATUS_V);
        MCU_SetStatus(0, STATUS_C);
    }
    else if (opcode_reg == 0 && operand_type == GENERAL_DIRECT && operand_size == 0) // SWAP
    {
        uint32_t data = mcu.r[operand_reg];
        uint32_t data_h = data >> 8;
        uint32_t data_l = data & 0xff;
        data = (data_l << 8) | data_h;
        mcu.r[operand_reg] = data;
        MCU_SetStatusCommon(data, OPERAND_WORD);
    }
    else if (opcode_reg == 5 && operand_type != GENERAL_IMMEDIATE) // NOT
    {
        uint32_t data = MCU_Operand_Read();
        data = ~data;
        MCU_Operand_Write(data);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 4 && operand_type != GENERAL_IMMEDIATE) // NEG
    {
        uint32_t data = MCU_Operand_Read();
        data = MCU_SUB_Common(0, data, 0, operand_size);
        MCU_Operand_Write(data);
    }
    else if (opcode_reg == 1 && operand_type == GENERAL_DIRECT && operand_size == 0) // EXTS
    {
        uint32_t data = mcu.r[operand_reg];
        mcu.r[operand_reg] = (int8_t)data;
        MCU_SetStatusCommon(data, OPERAND_WORD);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_LDC(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t data = MCU_Operand_Read();
    MCU_ControlRegisterWrite(opcode_reg, operand_size, data);
    mcu.ex_ignore = 1;
}

static void MCU_Opcode_STC(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t data = MCU_ControlRegisterRead(opcode_reg, operand_size);
    MCU_Operand_Write(data);
}

static void MCU_Opcode_BSET(uint8_t opcode, uint8_t opcode_reg)
{
    if (operand_type != GENERAL_IMMEDIATE)
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = opcode_reg | ((opcode & 1) << 3);
        MCU_SetStatus((data & (1 << bit)) == 0, STATUS_Z);
        data |= 1 << bit;
        MCU_Operand_Write(data);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_BCLR(uint8_t opcode, uint8_t opcode_reg)
{
    if (operand_type != GENERAL_IMMEDIATE)
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = opcode_reg | ((opcode & 1) << 3);
        MCU_SetStatus((data & (1 << bit)) == 0, STATUS_Z);
        data &= ~(1 << bit);
        MCU_Operand_Write(data);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_MOVG(uint8_t opcode, uint8_t opcode_reg)
{
    if (opcode_extended)
    {
        if (opcode == 0x12)
        {
            // FIXME
            MCU_ErrorTrap();
        }
        else
        {
            MCU_ErrorTrap();
        }
    }
    else
    {
        uint8_t d = (opcode & 2) != 0;
        uint32_t data;
        if (d)
        {
            if (operand_type == GENERAL_DIRECT) // XCH
            {
                if (operand_size)
                {
                    uint32_t r1 = mcu.r[opcode_reg];
                    uint32_t r2 = mcu.r[operand_reg];
                    mcu.r[opcode_reg] = r2;
                    mcu.r[operand_reg] = r1;
                }
                else
                {
                    MCU_ErrorTrap();
                }
            }
            else
            {
                data = mcu.r[opcode_reg];
                MCU_Operand_Write(data);
                MCU_SetStatusCommon(data, operand_size);
            }
        }
        else
        {
            data = MCU_Operand_Read();
            if (operand_size)
                mcu.r[opcode_reg] = data;
            else
            {
                mcu.r[opcode_reg] &= ~0xff;
                mcu.r[opcode_reg] |= data & 0xff;
            }
            MCU_SetStatusCommon(data, operand_size);
        }
    }
}

static void MCU_Opcode_BTSTI(uint8_t opcode, uint8_t opcode_reg)
{
    if (operand_type != GENERAL_IMMEDIATE)
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = opcode_reg | ((opcode & 1) << 3);
        MCU_SetStatus((data & (1 << bit)) == 0, STATUS_Z);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_BNOTI(uint8_t opcode, uint8_t opcode_reg)
{
    if (operand_type != GENERAL_IMMEDIATE)
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = opcode_reg | ((opcode & 1) << 3);
        MCU_SetStatus((data & (1 << bit)) == 0, STATUS_Z);
        data ^= (1 << bit);
        MCU_Operand_Write(data);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_OR(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t data = MCU_Operand_Read();
    mcu.r[opcode_reg] |= data;
    MCU_SetStatusCommon(mcu.r[opcode_reg], operand_size);
}

static void MCU_Opcode_CMP(uint8_t opcode, uint8_t opcode_reg)
{
    int32_t t1 = mcu.r[opcode_reg];
    int32_t t2 = MCU_Operand_Read();
    MCU_SUB_Common(t1, t2, 0, operand_size);
}

static void MCU_Opcode_ADDQ(uint8_t opcode, uint8_t opcode_reg)
{
    int32_t t1 = MCU_Operand_Read();
    int32_t t2 = 0;
    switch (opcode_reg)
    {
    case 0:
        t2 = 1;
        break;
    case 1:
        t2 = 2;
        break;
    case 4:
        t2 = -1;
        break;
    case 5:
        t2 = -2;
        break;
    default:
        MCU_ErrorTrap();
        break;
    }
    t1 = MCU_ADD_Common(t1, t2, 0, operand_size);
    MCU_Operand_Write(t1);
}

static void MCU_Opcode_ADD(uint8_t opcode, uint8_t opcode_reg)
{
    int32_t t1 = mcu.r[opcode_reg];
    int32_t t2 = MCU_Operand_Read();
    t1 = MCU_ADD_Common(t1, t2, 0, operand_size);
    if (operand_size)
        mcu.r[opcode_reg] = t1;
    else
    {
        mcu.r[opcode_reg] &= ~0xff;
        mcu.r[opcode_reg] |= t1 & 0xff;
    }
}

static void MCU_Opcode_SUB(uint8_t opcode, uint8_t opcode_reg)
{
    int32_t t1 = mcu.r[opcode_reg];
    int32_t t2 = MCU_Operand_Read();
    t1 = MCU_SUB_Common(t1, t2, 0, operand_size);
    if (operand_size)
        mcu.r[opcode_reg] = t1;
    else
    {
        mcu.r[opcode_reg] &= ~0xff;
        mcu.r[opcode_reg] |= t1 & 0xff;
    }
}

static void MCU_Opcode_SUBS(uint8_t opcode, uint8_t opcode_reg)
{
    int32_t t1 = mcu.r[opcode_reg];
    int32_t t2 = MCU_Operand_Read();
    if (operand_size)
        mcu.r[opcode_reg] = t1 - t2;
    else
        mcu.r[opcode_reg] = t1 - (int8_t)t2;
}

static void MCU_Opcode_AND(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t data = mcu.r[opcode_reg];
    data &= MCU_Operand_Read();
    if (operand_size)
        mcu.r[opcode_reg] = data;
    else
    {
        mcu.r[opcode_reg] &= ~0xff;
        mcu.r[opcode_reg] |= data & 0xff;
    }
    MCU_SetStatusCommon(mcu.r[opcode_reg], operand_size);
}

static void MCU_Opcode_SHLR(uint8_t opcode, uint8_t opcode_reg)
{
    if (opcode_reg == 0x03 && operand_type != GENERAL_IMMEDIATE) // SHLR
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t C = data & 1;
        data >>= 1;
        MCU_Operand_Write(data);
        MCU_SetStatus(C, STATUS_C);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 0x02 && operand_type != GENERAL_IMMEDIATE) // SHLL
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t C;
        if (operand_size)
            C = (data & 0x8000) != 0;
        else
            C = (data & 0x80) != 0;
        data <<= 1;
        MCU_Operand_Write(data);
        MCU_SetStatus(C, STATUS_C);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 0x06 && operand_type != GENERAL_IMMEDIATE) // ROTXL
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t bit = (mcu.sr & STATUS_C) != 0;
        uint32_t C;
        if (operand_size)
            C = (data & 0x8000) != 0;
        else
            C = (data & 0x80) != 0;
        data <<= 1;
        data |= bit;
        MCU_Operand_Write(data);
        MCU_SetStatus(C, STATUS_C);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 0x04 && operand_type != GENERAL_IMMEDIATE) // ROTL
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t C;
        if (operand_size)
            C = (data & 0x8000) != 0;
        else
            C = (data & 0x80) != 0;
        data <<= 1;
        data |= C;
        MCU_Operand_Write(data);
        MCU_SetStatus(C, STATUS_C);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 0x00 && operand_type != GENERAL_IMMEDIATE) // SHAL
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t C;
        if (operand_size)
            C = (data & 0x8000) != 0;
        else
            C = (data & 0x80) != 0;
        data <<= 1;
        MCU_Operand_Write(data);
        MCU_SetStatus(C, STATUS_C);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 0x01 && operand_type != GENERAL_IMMEDIATE) // SHAR
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t C = data & 0x1;
        uint32_t msb;
        if (operand_size)
        {
            msb = data & 0x8000;
            data &= 0xffff;
        }
        else
        {
            msb = data & 0x80;
            data &= 0xff;
        }
        data >>= 1;
        data |= msb;
        MCU_Operand_Write(data);
        MCU_SetStatus(C, STATUS_C);
        MCU_SetStatusCommon(data, operand_size);
    }
    else if (opcode_reg == 0x05 && operand_type != GENERAL_IMMEDIATE) // ROTR
    {
        uint32_t data = MCU_Operand_Read();
        uint32_t C = (data & 0x1) != 0;
        data >>= 1;
        if (operand_size)
            data |= C << 15;
        else
            data |= C << 7;
        MCU_Operand_Write(data);
        MCU_SetStatus(C, STATUS_C);
        MCU_SetStatusCommon(data, operand_size);
    }
    else
    {
        MCU_ErrorTrap();
    }
}

static void MCU_Opcode_MULXU(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t t1 = MCU_Operand_Read();
    uint32_t t2 = mcu.r[opcode_reg];
    uint32_t N, Z;
    if (!operand_size)
        t2 &= 0xff;
    t1 *= t2;

    if (operand_size)
    {
        opcode_reg &= ~1;
        mcu.r[opcode_reg | 0] = t1 >> 16;
        mcu.r[opcode_reg | 1] = t1;
        N = (t1 & 0x80000000UL) != 0; // FIXME
    }
    else
    {
        t1 &= 0xffff;
        mcu.r[opcode_reg] = t1;
        N = (t1 & 0x8000UL) != 0; // FIXME
    }
    Z = t1 == 0;
    MCU_SetStatus(N, STATUS_N);
    MCU_SetStatus(Z, STATUS_Z);
    MCU_SetStatus(0, STATUS_V);
    MCU_SetStatus(0, STATUS_C);
}

static void MCU_Opcode_DIVXU(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t t1 = MCU_Operand_Read();
    uint32_t t2;
    uint32_t R, Q;

    if (!t1)
    {
        MCU_ErrorTrap(); // FIXME: implement proper exception
        MCU_SetStatus(0, STATUS_N);
        MCU_SetStatus(1, STATUS_Z);
        MCU_SetStatus(0, STATUS_V);
        MCU_SetStatus(0, STATUS_C);
        return;
    }

    if (operand_size)
    {
        opcode_reg &= ~1;
        t2 = mcu.r[opcode_reg | 0] << 16;
        t2 |= mcu.r[opcode_reg | 1];

        R = t2 % t1;
        Q = t2 / t1;

        if (Q > UINT16_MAX)
        {
            MCU_SetStatus(0, STATUS_N);
            MCU_SetStatus(0, STATUS_Z);
            MCU_SetStatus(1, STATUS_V);
            MCU_SetStatus(0, STATUS_C);
        }
        else
        {
            mcu.r[opcode_reg | 0] = R;
            mcu.r[opcode_reg | 1] = Q;
            MCU_SetStatusCommon(Q, OPERAND_WORD);
            MCU_SetStatus(0, STATUS_C);
        }
    }
    else
    {
        t2 = mcu.r[opcode_reg];

        R = t2 % t1;
        Q = t2 / t1;

        if (Q > UINT8_MAX)
        {
            MCU_SetStatus(0, STATUS_N);
            MCU_SetStatus(0, STATUS_Z);
            MCU_SetStatus(1, STATUS_V);
            MCU_SetStatus(0, STATUS_C);
        }
        else
        {
            R &= 0xff;
            Q &= 0xff;
            mcu.r[opcode_reg] = (R << 8) | Q;
            MCU_SetStatusCommon(Q, OPERAND_BYTE);
            MCU_SetStatus(0, STATUS_C);
        }
    }
}

static void MCU_Opcode_ADDS(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t data = MCU_Operand_Read();
    if (!operand_size)
        data = (int8_t)data;
    mcu.r[opcode_reg] += data;
}

static void MCU_Opcode_XOR(uint8_t opcode, uint8_t opcode_reg)
{
    uint32_t data = MCU_Operand_Read();
    mcu.r[opcode_reg] ^= data;
    MCU_SetStatusCommon(mcu.r[opcode_reg], operand_size);
}

static void MCU_Opcode_ADDX(uint8_t opcode, uint8_t opcode_reg)
{
    int32_t t1 = mcu.r[opcode_reg];
    int32_t t2 = MCU_Operand_Read();
    int32_t C = (mcu.sr & STATUS_C) != 0;
    int32_t Z = (mcu.sr & STATUS_Z) != 0;
    t1 = MCU_ADD_Common(t1, t2, C, operand_size);
    if (!Z)
        MCU_SetStatus(0, STATUS_Z);

    if (operand_size)
        mcu.r[opcode_reg] = t1;
    else
    {
        mcu.r[opcode_reg] &= ~0xff;
        mcu.r[opcode_reg] |= t1 & 0xff;
    }
}

static void MCU_Opcode_SUBX(uint8_t opcode, uint8_t opcode_reg)
{
    int32_t t1 = mcu.r[opcode_reg];
    int32_t t2 = MCU_Operand_Read();
    int32_t C = (mcu.sr & STATUS_C) != 0;
    t1 = MCU_SUB_Common(t1, t2, C, operand_size);
    if (operand_size)
        mcu.r[opcode_reg] = t1;
    else
    {
        mcu.r[opcode_reg] &= ~0xff;
        mcu.r[opcode_reg] |= t1 & 0xff;
    }
}

void (*MCU_Operand_Table[256])(uint8_t operand) = {
    MCU_Operand_Nop, // 00
    MCU_Jump_JMP, // 01
    MCU_LDM, // 02
    MCU_Jump_PJSR, // 03
    MCU_Operand_General, // 04
    MCU_Operand_General, // 05
    MCU_Jump_JMP, // 06
    MCU_Jump_JMP, // 07
    MCU_TRAPA, // 08
    MCU_Operand_NotImplemented, // 09
    MCU_Jump_RTE, // 0A
    MCU_Operand_NotImplemented, // 0B
    MCU_Operand_General, // 0C
    MCU_Operand_General, // 0D
    MCU_Jump_BSR, // 0E
    MCU_Operand_NotImplemented, // 0F
    MCU_Jump_JMP, // 10
    MCU_Jump_JMP, // 11
    MCU_STM, // 12
    MCU_Jump_PJMP, // 13
    MCU_Jump_RTD, // 14
    MCU_Operand_General, // 15
    MCU_Operand_NotImplemented, // 16
    MCU_Operand_NotImplemented, // 17
    MCU_Jump_JSR, // 18
    MCU_Jump_RTS, // 19
    MCU_Operand_Sleep, // 1A
    MCU_Operand_NotImplemented, // 1B
    MCU_Jump_RTD, // 1C
    MCU_Operand_General, // 1D
    MCU_Jump_BSR, // 1E
    MCU_Operand_NotImplemented, // 1F
    MCU_Jump_Bcc, // 20
    MCU_Jump_Bcc, // 21
    MCU_Jump_Bcc, // 22
    MCU_Jump_Bcc, // 23
    MCU_Jump_Bcc, // 24
    MCU_Jump_Bcc, // 25
    MCU_Jump_Bcc, // 26
    MCU_Jump_Bcc, // 27
    MCU_Jump_Bcc, // 28
    MCU_Jump_Bcc, // 29
    MCU_Jump_Bcc, // 2A
    MCU_Jump_Bcc, // 2B
    MCU_Jump_Bcc, // 2C
    MCU_Jump_Bcc, // 2D
    MCU_Jump_Bcc, // 2E
    MCU_Jump_Bcc, // 2F
    MCU_Jump_Bcc, // 30
    MCU_Jump_Bcc, // 31
    MCU_Jump_Bcc, // 32
    MCU_Jump_Bcc, // 33
    MCU_Jump_Bcc, // 34
    MCU_Jump_Bcc, // 35
    MCU_Jump_Bcc, // 36
    MCU_Jump_Bcc, // 37
    MCU_Jump_Bcc, // 38
    MCU_Jump_Bcc, // 39
    MCU_Jump_Bcc, // 3A
    MCU_Jump_Bcc, // 3B
    MCU_Jump_Bcc, // 3C
    MCU_Jump_Bcc, // 3D
    MCU_Jump_Bcc, // 3E
    MCU_Jump_Bcc, // 3F
    MCU_Opcode_Short_CMP, // 40
    MCU_Opcode_Short_CMP, // 41
    MCU_Opcode_Short_CMP, // 42
    MCU_Opcode_Short_CMP, // 43
    MCU_Opcode_Short_CMP, // 44
    MCU_Opcode_Short_CMP, // 45
    MCU_Opcode_Short_CMP, // 46
    MCU_Opcode_Short_CMP, // 47
    MCU_Opcode_Short_CMP, // 48
    MCU_Opcode_Short_CMP, // 49
    MCU_Opcode_Short_CMP, // 4A
    MCU_Opcode_Short_CMP, // 4B
    MCU_Opcode_Short_CMP, // 4C
    MCU_Opcode_Short_CMP, // 4D
    MCU_Opcode_Short_CMP, // 4E
    MCU_Opcode_Short_CMP, // 4F
    MCU_Opcode_Short_MOVE, // 50
    MCU_Opcode_Short_MOVE, // 51
    MCU_Opcode_Short_MOVE, // 52
    MCU_Opcode_Short_MOVE, // 53
    MCU_Opcode_Short_MOVE, // 54
    MCU_Opcode_Short_MOVE, // 55
    MCU_Opcode_Short_MOVE, // 56
    MCU_Opcode_Short_MOVE, // 57
    MCU_Opcode_Short_MOVI, // 58
    MCU_Opcode_Short_MOVI, // 59
    MCU_Opcode_Short_MOVI, // 5A
    MCU_Opcode_Short_MOVI, // 5B
    MCU_Opcode_Short_MOVI, // 5C
    MCU_Opcode_Short_MOVI, // 5D
    MCU_Opcode_Short_MOVI, // 5E
    MCU_Opcode_Short_MOVI, // 5F
    MCU_Opcode_Short_MOVL, // 60
    MCU_Opcode_Short_MOVL, // 61
    MCU_Opcode_Short_MOVL, // 62
    MCU_Opcode_Short_MOVL, // 63
    MCU_Opcode_Short_MOVL, // 64
    MCU_Opcode_Short_MOVL, // 65
    MCU_Opcode_Short_MOVL, // 66
    MCU_Opcode_Short_MOVL, // 67
    MCU_Opcode_Short_MOVL, // 68
    MCU_Opcode_Short_MOVL, // 69
    MCU_Opcode_Short_MOVL, // 6A
    MCU_Opcode_Short_MOVL, // 6B
    MCU_Opcode_Short_MOVL, // 6C
    MCU_Opcode_Short_MOVL, // 6D
    MCU_Opcode_Short_MOVL, // 6E
    MCU_Opcode_Short_MOVL, // 6F
    MCU_Opcode_Short_MOVS, // 70
    MCU_Opcode_Short_MOVS, // 71
    MCU_Opcode_Short_MOVS, // 72
    MCU_Opcode_Short_MOVS, // 73
    MCU_Opcode_Short_MOVS, // 74
    MCU_Opcode_Short_MOVS, // 75
    MCU_Opcode_Short_MOVS, // 76
    MCU_Opcode_Short_MOVS, // 77
    MCU_Opcode_Short_MOVS, // 78
    MCU_Opcode_Short_MOVS, // 79
    MCU_Opcode_Short_MOVS, // 7A
    MCU_Opcode_Short_MOVS, // 7B
    MCU_Opcode_Short_MOVS, // 7C
    MCU_Opcode_Short_MOVS, // 7D
    MCU_Opcode_Short_MOVS, // 7E
    MCU_Opcode_Short_MOVS, // 7F
    MCU_Opcode_Short_MOVF, // 80
    MCU_Opcode_Short_MOVF, // 81
    MCU_Opcode_Short_MOVF, // 82
    MCU_Opcode_Short_MOVF, // 83
    MCU_Opcode_Short_MOVF, // 84
    MCU_Opcode_Short_MOVF, // 85
    MCU_Opcode_Short_MOVF, // 86
    MCU_Opcode_Short_MOVF, // 87
    MCU_Opcode_Short_MOVF, // 88
    MCU_Opcode_Short_MOVF, // 89
    MCU_Opcode_Short_MOVF, // 8A
    MCU_Opcode_Short_MOVF, // 8B
    MCU_Opcode_Short_MOVF, // 8C
    MCU_Opcode_Short_MOVF, // 8D
    MCU_Opcode_Short_MOVF, // 8E
    MCU_Opcode_Short_MOVF, // 8F
    MCU_Opcode_Short_MOVF, // 90
    MCU_Opcode_Short_MOVF, // 91
    MCU_Opcode_Short_MOVF, // 92
    MCU_Opcode_Short_MOVF, // 93
    MCU_Opcode_Short_MOVF, // 94
    MCU_Opcode_Short_MOVF, // 95
    MCU_Opcode_Short_MOVF, // 96
    MCU_Opcode_Short_MOVF, // 97
    MCU_Opcode_Short_MOVF, // 98
    MCU_Opcode_Short_MOVF, // 99
    MCU_Opcode_Short_MOVF, // 9A
    MCU_Opcode_Short_MOVF, // 9B
    MCU_Opcode_Short_MOVF, // 9C
    MCU_Opcode_Short_MOVF, // 9D
    MCU_Opcode_Short_MOVF, // 9E
    MCU_Opcode_Short_MOVF, // 9F
    MCU_Operand_General, // A0
    MCU_Operand_General, // A1
    MCU_Operand_General, // A2
    MCU_Operand_General, // A3
    MCU_Operand_General, // A4
    MCU_Operand_General, // A5
    MCU_Operand_General, // A6
    MCU_Operand_General, // A7
    MCU_Operand_General, // A8
    MCU_Operand_General, // A9
    MCU_Operand_General, // AA
    MCU_Operand_General, // AB
    MCU_Operand_General, // AC
    MCU_Operand_General, // AD
    MCU_Operand_General, // AE
    MCU_Operand_General, // AF
    MCU_Operand_General, // B0
    MCU_Operand_General, // B1
    MCU_Operand_General, // B2
    MCU_Operand_General, // B3
    MCU_Operand_General, // B4
    MCU_Operand_General, // B5
    MCU_Operand_General, // B6
    MCU_Operand_General, // B7
    MCU_Operand_General, // B8
    MCU_Operand_General, // B9
    MCU_Operand_General, // BA
    MCU_Operand_General, // BB
    MCU_Operand_General, // BC
    MCU_Operand_General, // BD
    MCU_Operand_General, // BE
    MCU_Operand_General, // BF
    MCU_Operand_General, // C0
    MCU_Operand_General, // C1
    MCU_Operand_General, // C2
    MCU_Operand_General, // C3
    MCU_Operand_General, // C4
    MCU_Operand_General, // C5
    MCU_Operand_General, // C6
    MCU_Operand_General, // C7
    MCU_Operand_General, // C8
    MCU_Operand_General, // C9
    MCU_Operand_General, // CA
    MCU_Operand_General, // CB
    MCU_Operand_General, // CC
    MCU_Operand_General, // CD
    MCU_Operand_General, // CE
    MCU_Operand_General, // CF
    MCU_Operand_General, // D0
    MCU_Operand_General, // D1
    MCU_Operand_General, // D2
    MCU_Operand_General, // D3
    MCU_Operand_General, // D4
    MCU_Operand_General, // D5
    MCU_Operand_General, // D6
    MCU_Operand_General, // D7
    MCU_Operand_General, // D8
    MCU_Operand_General, // D9
    MCU_Operand_General, // DA
    MCU_Operand_General, // DB
    MCU_Operand_General, // DC
    MCU_Operand_General, // DD
    MCU_Operand_General, // DE
    MCU_Operand_General, // DF
    MCU_Operand_General, // E0
    MCU_Operand_General, // E1
    MCU_Operand_General, // E2
    MCU_Operand_General, // E3
    MCU_Operand_General, // E4
    MCU_Operand_General, // E5
    MCU_Operand_General, // E6
    MCU_Operand_General, // E7
    MCU_Operand_General, // E8
    MCU_Operand_General, // E9
    MCU_Operand_General, // EA
    MCU_Operand_General, // EB
    MCU_Operand_General, // EC
    MCU_Operand_General, // ED
    MCU_Operand_General, // EE
    MCU_Operand_General, // EF
    MCU_Operand_General, // F0
    MCU_Operand_General, // F1
    MCU_Operand_General, // F2
    MCU_Operand_General, // F3
    MCU_Operand_General, // F4
    MCU_Operand_General, // F5
    MCU_Operand_General, // F6
    MCU_Operand_General, // F7
    MCU_Operand_General, // F8
    MCU_Operand_General, // F9
    MCU_Operand_General, // FA
    MCU_Operand_General, // FB
    MCU_Operand_General, // FC
    MCU_Operand_General, // FD
    MCU_Operand_General, // FE
    MCU_Operand_General, // FF
};

void (*MCU_Opcode_Table[32])(uint8_t opcode, uint8_t opcode_reg) = {
    MCU_Opcode_MOVG_Immediate, // 00
    MCU_Opcode_ADDQ, // 01
    MCU_Opcode_CLR, // 02
    MCU_Opcode_SHLR, // 03
    MCU_Opcode_ADD, // 04
    MCU_Opcode_ADDS, // 05
    MCU_Opcode_SUB, // 06
    MCU_Opcode_SUBS, // 07
    MCU_Opcode_OR, // 08
    MCU_Opcode_BSET_ORC, // 09
    MCU_Opcode_AND, // 0A
    MCU_Opcode_BCLR_ANDC, // 0B
    MCU_Opcode_XOR, // 0C
    MCU_Opcode_NotImplemented, // 0D
    MCU_Opcode_CMP, // 0E
    MCU_Opcode_BTST, // 0F
    MCU_Opcode_MOVG, // 10
    MCU_Opcode_LDC, // 11
    MCU_Opcode_MOVG, // 12
    MCU_Opcode_STC, // 13
    MCU_Opcode_ADDX, // 14
    MCU_Opcode_MULXU, // 15
    MCU_Opcode_SUBX, // 16
    MCU_Opcode_DIVXU, // 17
    MCU_Opcode_BSET, // 18
    MCU_Opcode_BSET, // 19
    MCU_Opcode_BCLR, // 1A
    MCU_Opcode_BCLR, // 1B
    MCU_Opcode_BNOTI, // 1C
    MCU_Opcode_BNOTI, // 1D
    MCU_Opcode_BTSTI, // 1E
    MCU_Opcode_BTSTI, // 1F
};
#endif // NUKEDSC55_MCU_OPCODES_CPP

#ifndef NUKEDSC55_MCU_TIMER_CPP
#define NUKEDSC55_MCU_TIMER_CPP
enum {
    REG_TCR = 0x00,
    REG_TCSR = 0x01,
    REG_FRCH = 0x02,
    REG_FRCL = 0x03,
    REG_OCRAH = 0x04,
    REG_OCRAL = 0x05,
    REG_OCRBH = 0x06,
    REG_OCRBL = 0x07,
    REG_ICRH = 0x08,
    REG_ICRL = 0x09,
};

static void TIMER_Reset(void)
{
    timer_cycles = 0;
    timer_tempreg = 0;
    memset(frt, 0, sizeof(frt));
    memset(&timer, 0, sizeof(timer));
    frt[0].tcr_mask = frt[1].tcr_mask = frt[2].tcr_mask = 3;
    timer.tcr_mask = 0xffff;
}

static void TIMER_Write(uint32_t address, uint8_t data)
{
    uint32_t t = (address >> 4) - 1;
    if (t > 2)
        return;
    address &= 0x0f;
    frt_t *timer = &frt[t];
    switch (address)
    {
    case REG_TCR:
        timer->tcr = data;
        switch (data & 3)
        {
            case 0: timer->tcr_mask =  3; break; // o /  4
            case 1: timer->tcr_mask =  7; break; // o /  8
            case 2: timer->tcr_mask = 31; break; // o / 31
            case 3: timer->tcr_mask = (mcu_mk1 ? 3 : 1); break; // ext (o / 2)
        }
        //const uint8_t frt_tcr_masks[4] = { 3, 7, 31, (mcu_mk1 ? 3 : 1) };
        break;
    case REG_TCSR:
        timer->tcsr &= ~0xf;
        timer->tcsr |= data & 0xf;
        if ((data & 0x10) == 0 && (timer->status_rd & 0x10) != 0)
        {
            timer->tcsr &= ~0x10;
            timer->status_rd &= ~0x10;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_FOVI + t * 4, 0);
        }
        if ((data & 0x20) == 0 && (timer->status_rd & 0x20) != 0)
        {
            timer->tcsr &= ~0x20;
            timer->status_rd &= ~0x20;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIA + t * 4, 0);
        }
        if ((data & 0x40) == 0 && (timer->status_rd & 0x40) != 0)
        {
            timer->tcsr &= ~0x40;
            timer->status_rd &= ~0x40;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIB + t * 4, 0);
        }
        break;
    case REG_FRCH:
    case REG_OCRAH:
    case REG_OCRBH:
    case REG_ICRH:
        timer_tempreg = data;
        break;
    case REG_FRCL:
        timer->frc = (timer_tempreg << 8) | data;
        break;
    case REG_OCRAL:
        timer->ocra = (timer_tempreg << 8) | data;
        break;
    case REG_OCRBL:
        timer->ocrb = (timer_tempreg << 8) | data;
        break;
    case REG_ICRL:
        timer->icr = (timer_tempreg << 8) | data;
        break;
    }
}

static uint8_t TIMER_Read(uint32_t address)
{
    uint32_t t = (address >> 4) - 1;
    if (t > 2)
        return 0xff;
    address &= 0x0f;
    frt_t *timer = &frt[t];
    switch (address)
    {
    case REG_TCR:
        return timer->tcr;
    case REG_TCSR:
    {
        uint8_t ret = timer->tcsr;
        timer->status_rd |= timer->tcsr & 0xf0;
        //timer->status_rd |= 0xf0;
        return ret;
    }
    case REG_FRCH:
        timer_tempreg = timer->frc & 0xff;
        return timer->frc >> 8;
    case REG_OCRAH:
        timer_tempreg = timer->ocra & 0xff;
        return timer->ocra >> 8;
    case REG_OCRBH:
        timer_tempreg = timer->ocrb & 0xff;
        return timer->ocrb >> 8;
    case REG_ICRH:
        timer_tempreg = timer->icr & 0xff;
        return timer->icr >> 8;
    case REG_FRCL:
    case REG_OCRAL:
    case REG_OCRBL:
    case REG_ICRL:
        return timer_tempreg;
    }
    return 0xff;
}

static void TIMER2_Write(uint32_t address, uint8_t data)
{
    switch (address)
    {
    case DEV_TMR_TCR:
        timer.tcr = data;
        switch (data & 7)
        {
            case 0:case 4:        timer.tcr_mask = 0xffff; break; // disabled
            case 1:               timer.tcr_mask = 7; break; // o / 8
            case 2:               timer.tcr_mask = 63; break; // o / 64
            case 3:               timer.tcr_mask = 1023; break; // o / 1024
            case 5:case 6:case 7: timer.tcr_mask = (mcu_mk1 ? 3 : 1); break; // ext (o / 2)
        }
        //const uint16_t timer_tcr_masks[8] = { 0xffff, 7, 63, 1023, 0xffff, frt_tcr_masks[3], frt_tcr_masks[3], frt_tcr_masks[3] };
        break;
    case DEV_TMR_TCSR:
        timer.tcsr &= ~0xf;
        timer.tcsr |= data & 0xf;
        if ((data & 0x20) == 0 && (timer.status_rd & 0x20) != 0)
        {
            timer.tcsr &= ~0x20;
            timer.status_rd &= ~0x20;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_TIMER_OVI, 0);
        }
        if ((data & 0x40) == 0 && (timer.status_rd & 0x40) != 0)
        {
            timer.tcsr &= ~0x40;
            timer.status_rd &= ~0x40;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_TIMER_CMIA, 0);
        }
        if ((data & 0x80) == 0 && (timer.status_rd & 0x80) != 0)
        {
            timer.tcsr &= ~0x80;
            timer.status_rd &= ~0x80;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_TIMER_CMIB, 0);
        }
        break;
    case DEV_TMR_TCORA:
        timer.tcora = data;
        break;
    case DEV_TMR_TCORB:
        timer.tcorb = data;
        break;
    case DEV_TMR_TCNT:
        timer.tcnt = data;
        break;
    }
}

static uint8_t TIMER_Read2(uint32_t address)
{
    switch (address)
    {
    case DEV_TMR_TCR:
        return timer.tcr;
    case DEV_TMR_TCSR:
    {
        uint8_t ret = timer.tcsr;
        timer.status_rd |= timer.tcsr & 0xe0;
        return ret;
    }
    case DEV_TMR_TCORA:
        return timer.tcora;
    case DEV_TMR_TCORB:
        return timer.tcorb;
    case DEV_TMR_TCNT:
        return timer.tcnt;
    }
    return 0xff;
}

static void TIMER_Clock(uint64_t cycles)
{
    // Simplified, changed to use precalculated timer masks and unrolled the frt loop
    const uint32_t tc_mask = (frt[0].tcr_mask & frt[1].tcr_mask & frt[2].tcr_mask & timer.tcr_mask), tc_step = (tc_mask + 1);
    const uint64_t cycles_half = cycles / 2;
    for (uint64_t tc = (timer_cycles + tc_mask) & ~(uint64_t)tc_mask; tc < cycles_half; tc += tc_step)
    {
        if (!(tc & frt[0].tcr_mask))
        {
            uint32_t tcr = frt[0].tcr, value = frt[0].frc, tcsr = frt[0].tcsr; bool matcha = (value == frt[0].ocra), matchb = (value == frt[0].ocrb);
            value = ((matcha && (tcsr & 1)) ? 0 : (value + 1)); // CCLRA
            if (value == 0x10000) tcsr |= 0x10;
            if (matcha) tcsr |= 0x20;
            if (matchb) tcsr |= 0x40;
            if (tcr & tcsr & 0x10) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_FOVI + 0, 1);
            if (tcr & tcsr & 0x20) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIA + 0, 1);
            if (tcr & tcsr & 0x40) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIB + 0, 1);
            frt[0].frc = (uint16_t)value;
            frt[0].tcsr = (uint8_t)tcsr;
        }

        if (!(tc & frt[1].tcr_mask))
        {
            uint32_t tcr = frt[1].tcr, value = frt[1].frc, tcsr = frt[1].tcsr;
            bool matcha = (value == frt[1].ocra), matchb = (value == frt[1].ocrb);
            value = ((matcha && (tcsr & 1)) ? 0 : (value + 1)); // CCLRA
            if (value == 0x10000) tcsr |= 0x10;
            if (matcha) tcsr |= 0x20;
            if (matchb) tcsr |= 0x40;
            if (tcr & tcsr & 0x10) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_FOVI + 4, 1);
            if (tcr & tcsr & 0x20) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIA + 4, 1);
            if (tcr & tcsr & 0x40) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIB + 4, 1);
            frt[1].frc = (uint16_t)value;
            frt[1].tcsr = (uint8_t)tcsr;
        }

        if (!(tc & frt[2].tcr_mask))
        {
            uint32_t tcr = frt[2].tcr, value = frt[2].frc, tcsr = frt[2].tcsr;
            bool matcha = (value == frt[2].ocra), matchb = (value == frt[2].ocrb);
            value = ((matcha && (tcsr & 1)) ? 0 : (value + 1)); // CCLRA
            if (value == 0x10000) tcsr |= 0x10;
            if (matcha) tcsr |= 0x20;
            if (matchb) tcsr |= 0x40;
            if (tcr & tcsr & 0x10) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_FOVI + 8, 1);
            if (tcr & tcsr & 0x20) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIA + 8, 1);
            if (tcr & tcsr & 0x40) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_FRT0_OCIB + 8, 1);
            frt[2].frc = (uint16_t)value;
            frt[2].tcsr = (uint8_t)tcsr;
        }

        if (!(tc & timer.tcr_mask) && (timer.tcr & 3))
        {
            uint32_t tcr = timer.tcr, value = timer.tcnt, tcsr = timer.tcsr;
            bool matcha = (value == timer.tcora), matchb = (value == timer.tcorb);
            value = (((matcha && (tcr & 24) == 8) || (matchb && (tcr & 24) == 16)) ? 0 : (value + 1));
            if (value == 0x100) tcsr |= 0x20;
            if (matcha) tcsr |= 0x40;
            if (matchb) tcsr |= 0x80;
            if (tcr & tcsr & 0x20) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_TIMER_OVI, 1);
            if (tcr & tcsr & 0x40) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_TIMER_CMIA, 1);
            if (tcr & tcsr & 0x80) MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_TIMER_CMIB, 1);
            timer.tcsr = (uint8_t)tcsr;
            timer.tcnt = (uint8_t)value;
        }
    }
    timer_cycles = cycles_half;
}
#endif // NUKEDSC55_MCU_TIMER_CPP

#ifndef NUKEDSC55_PCM_CPP
#define NUKEDSC55_PCM_CPP
static const int interp_lut[3][128] = {
    { 3385, 3401, 3417, 3432, 3448, 3463, 3478, 3492, 3506, 3521, 3535, 3548, 3562, 3575, 3588, 3601,
    3614, 3626, 3638, 3650, 3662, 3673, 3685, 3696, 3707, 3718, 3728, 3739, 3749, 3759, 3768, 3778,
    3787, 3796, 3805, 3814, 3823, 3831, 3839, 3847, 3855, 3863, 3870, 3878, 3885, 3892, 3899, 3905,
    3912, 3918, 3924, 3930, 3936, 3942, 3948, 3953, 3958, 3963, 3968, 3973, 3978, 3983, 3987, 3991,
    3995, 4000, 4004, 4007, 4011, 4015, 4018, 4022, 4025, 4028, 4031, 4034, 4037, 4040, 4042, 4045,
    4047, 4050, 4052, 4054, 4057, 4059, 4061, 4063, 4064, 4066, 4068, 4070, 4071, 4073, 4074, 4076,
    4077, 4078, 4079, 4081, 4082, 4083, 4084, 4085, 4086, 4086, 4087, 4088, 4089, 4089, 4090, 4091,
    4091, 4092, 4092, 4093, 4093, 4094, 4094, 4094, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095, },

    { 710, 726, 742, 758, 775, 792, 809, 826, 844, 861, 879, 897, 915, 933, 952, 971,
    990, 1009, 1028, 1047, 1067, 1087, 1106, 1126, 1147, 1167, 1188, 1208, 1229, 1250, 1271, 1292,
    1314, 1335, 1357, 1379, 1400, 1423, 1445, 1467, 1489, 1512, 1534, 1557, 1580, 1602, 1625, 1648,
    1671, 1695, 1718, 1741, 1764, 1788, 1811, 1835, 1858, 1882, 1906, 1929, 1953, 1977, 2000, 2024,
    2048, 2071, 2095, 2119, 2143, 2166, 2190, 2214, 2237, 2261, 2284, 2308, 2331, 2355, 2378, 2401,
    2425, 2448, 2471, 2494, 2517, 2539, 2562, 2585, 2607, 2630, 2652, 2674, 2696, 2718, 2740, 2762,
    2783, 2805, 2826, 2847, 2868, 2889, 2910, 2931, 2951, 2971, 2991, 3011, 3031, 3051, 3070, 3089,
    3108, 3127, 3146, 3164, 3182, 3200, 3218, 3236, 3253, 3271, 3288, 3304, 3321, 3338, 3354, 3370, },

    { 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 8, 8, 9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 22, 23, 24, 26, 27, 29, 30, 32, 34, 36, 38, 40, 42, 44, 46,
    49, 51, 53, 56, 59, 62, 65, 68, 71, 74, 77, 81, 84, 88, 92, 96,
    100, 104, 109, 113, 118, 122, 127, 132, 137, 143, 148, 154, 160, 165, 171, 178,
    184, 191, 197, 204, 211, 219, 226, 234, 241, 249, 257, 266, 274, 283, 292, 301,
    310, 319, 329, 339, 349, 359, 369, 380, 391, 402, 413, 424, 436, 448, 460, 472,
    484, 497, 510, 523, 536, 549, 563, 577, 591, 605, 619, 634, 648, 663, 679, 694, },
};

static inline uint8_t PCM_ReadROM(uint32_t address)
{
    // Optimized to use bank array and precalculated bank shift
    const int bank = (address >> pcm.bank_shift) & 7;
    return pcm.banks[bank][address & pcm.bank_masks[bank]];
}

static void PCM_Write(uint32_t address, uint8_t data)
{
    address &= 0x3f;
    if (address < 0x4) // voice enable
    {
        switch (address & 3)
        {
            case 0:
                pcm.voice_mask_pending &= ~0xf000000;
                pcm.voice_mask_pending |= (data & 0xf) << 24;
                break;
            case 1:
                pcm.voice_mask_pending &= ~0xff0000;
                pcm.voice_mask_pending |= (data & 0xff) << 16;
                break;
            case 2:
                pcm.voice_mask_pending &= ~0xff00;
                pcm.voice_mask_pending |= (data & 0xff) << 8;
                break;
            case 3:
                pcm.voice_mask_pending &= ~0xff;
                pcm.voice_mask_pending |= (data & 0xff) << 0;
                break;
        }
        pcm.voice_mask_updating = 1;
    }
    else if (address >= 0x20 && address < 0x24) // wave rom
    {
        switch (address & 3)
        {
            case 1:
                pcm.wave_read_address &= ~0xff0000;
                pcm.wave_read_address |= (data & 0xff) << 16;
                break;
            case 2:
                pcm.wave_read_address &= ~0xff00;
                pcm.wave_read_address |= (data & 0xff) << 8;
                break;
            case 3:
                pcm.wave_read_address &= ~0xff;
                pcm.wave_read_address |= (data & 0xff) << 0;
                pcm.wave_byte_latch = PCM_ReadROM(pcm.wave_read_address);
                break;
        }
    }
    else if (address == 0x3c)
    {
        pcm.config_reg_3c = data;
    }
    else if (address == 0x3d)
    {
        pcm.config_reg_3d = data;
        pcm.bank_shift = ((pcm.config_reg_3d & 0x20) ? 21 : 19);
    }
    else if (address == 0x3e)
    {
        pcm.select_channel = data & 0x1f;
    }
    else if ((address >= 0x4 && address < 0x10) || (address >= 0x24 && address < 0x30))
    {
        switch (address & 3)
        {
            case 1:
                pcm.write_latch &= ~0xf0000;
                pcm.write_latch |= (data & 0xf) << 16;
                break;
            case 2:
                pcm.write_latch &= ~0xff00;
                pcm.write_latch |= (data & 0xff) << 8;
                break;
            case 3:
                pcm.write_latch &= ~0xff;
                pcm.write_latch |= (data & 0xff) << 0;
                break;
        }
        if ((address & 3) == 3)
        {
            int ix = 0;
            if (address & 32)
                ix |= 1;
            if ((address & 8) == 0)
                ix |= 4;
            if ((address & 4) == 0)
                ix |= 2;

            pcm.ram1[pcm.select_channel][ix] = pcm.write_latch;
        }
    }
    else if ((address >= 0x10 && address < 0x20) || (address >= 0x30 && address < 0x38))
    {
        switch (address & 1)
        {
        case 0:
            pcm.write_latch &= ~0xff00;
            pcm.write_latch |= (data & 0xff) << 8;
            break;
        case 1:
            pcm.write_latch &= ~0xff;
            pcm.write_latch |= (data & 0xff) << 0;
            break;
        }
        if ((address & 1) == 1)
        {
            int ix = (address >> 1) & 7;
            if (address & 32)
                ix |= 8;

            pcm.ram2[pcm.select_channel][ix] = pcm.write_latch;
        }
    }
}

static uint8_t PCM_Read(uint32_t address)
{
    address &= 0x3f;
    //printf("PCM Read: %.2x\n", address);

    if (address < 0x4)
    {
        if (pcm.voice_mask_updating)
            pcm.voice_mask = pcm.voice_mask_pending;
        pcm.voice_mask_updating = 0;
    }
    else if (address == 0x3c || address == 0x3e) // status
    {
        uint8_t status = 0;
        if (address == 0x3e && pcm.irq_assert)
        {
            pcm.irq_assert = 0;
            if (mcu_jv880)
                MCU_GA_SetGAInt(5, 0);
            else
                MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_IRQ0, 0);
        }

        status |= pcm.irq_channel;
        if (pcm.voice_mask_updating)
            status |= 32;

        return status;
    }
    else if (address == 0x3f)
    {
        return pcm.wave_byte_latch;
    }
    else if ((address >= 0x4 && address < 0x10) || (address >= 0x24 && address < 0x30))
    {
        if ((address & 3) == 1)
        {
            int ix = 0;
            if (address & 32)
                ix |= 1;
            if ((address & 8) == 0)
                ix |= 4;
            if ((address & 4) == 0)
                ix |= 2;

            pcm.read_latch = pcm.ram1[pcm.select_channel][ix];
        }
    }
    else if ((address >= 0x10 && address < 0x20) || (address >= 0x30 && address < 0x38))
    {
        if ((address & 1) == 0)
        {
            int ix = (address >> 1) & 7;
            if (address & 32)
                ix |= 8;

            pcm.read_latch = pcm.ram2[pcm.select_channel][ix];
        }
    }
    else if (address >= 0x39 && address <= 0x3b)
    {
        switch (address & 3)
        {
            case 1:
                return (pcm.read_latch >> 16) & 0xf;
            case 2:
                return (pcm.read_latch >> 8) & 0xff;
            case 3:
                return (pcm.read_latch >> 0) & 0xff;
        }
    }

    return 0;
}

static void PCM_Reset(uint16_t* pcm_eram, const uint8_t* waverom1, const uint8_t* waverom2, const uint8_t* waverom3, const uint8_t* waveromexp)
{
    memset(&pcm, 0, sizeof(pcm));
    pcm.eram = pcm_eram;
    pcm.bank_shift = 19;

    SC55_ASSERT(interp_lut[2][0] == 0); // a zero byte we can point to
    const uint8_t* zerobyte = (const uint8_t*)&interp_lut[2][0];
    pcm.banks[0] =               waverom1;                            pcm.bank_masks[0] = (mcu_mk1    ? 0xfffff : 0x1fffff);
    pcm.banks[1] =               waverom2;                            pcm.bank_masks[1] = (!mcu_jv880 ? 0xfffff : 0x1fffff);
    pcm.banks[2] =               waverom3;                            pcm.bank_masks[2] = (!mcu_jv880 ? 0xfffff : 0x1fffff);
    pcm.banks[3] = (!mcu_jv880 ? zerobyte : (waveromexp + 0x000000)); pcm.bank_masks[3] = (!mcu_jv880 ? 0       : 0x1fffff);
    pcm.banks[4] = (!mcu_jv880 ? zerobyte : (waveromexp + 0x200000)); pcm.bank_masks[4] = (!mcu_jv880 ? 0       : 0x1fffff);
    pcm.banks[5] = (!mcu_jv880 ? zerobyte : (waveromexp + 0x400000)); pcm.bank_masks[5] = (!mcu_jv880 ? 0       : 0x1fffff);
    pcm.banks[6] = (!mcu_jv880 ? zerobyte : (waveromexp + 0x600000)); pcm.bank_masks[6] = (!mcu_jv880 ? 0       : 0x1fffff);
    pcm.banks[7] =               zerobyte;                            pcm.bank_masks[7] = 0;
}

static inline uint32_t addclip20(uint32_t add1, uint32_t add2, uint32_t cin)
{
    uint32_t sum = (add1 + add2 + cin) & 0xfffff;
    if ((add1 & 0x80000) != 0 && (add2 & 0x80000) != 0 && (sum & 0x80000) == 0)
        sum = 0x80000;
    else if ((add1 & 0x80000) == 0 && (add2 & 0x80000) == 0 && (sum & 0x80000) != 0)
        sum = 0x7ffff;
    return sum;
}

static inline int32_t multi(int32_t val1, int8_t val2)
{
    if (val1 & 0x80000)
        val1 |= ~0xfffff;
    else
        val1 &= 0x7ffff;

    val1 *= val2;
    if (val1 & 0x8000000)
        val1 |= ~0x1ffffff;
    else
        val1 &= 0x1ffffff;
    return val1;
}

static void calc_tv(int e, const int adjust, uint16_t *levelcur, const int active, int *volmul)
{
    // Added some micro optimizations
    *levelcur &= 0x7fff;
    const int speed = (adjust & 0xff), target = ((adjust >> 8) & 0xff);
    const int w1 = !(speed & 0xf0);
    const int w2 = w1 | ((speed >> 4) & 1);
    const int w3 = pcm.nfs && ((speed & 0x80) == 0 || ((speed & 0x40) == 0 && (!w2 || (speed & 0x20) == 0)));
    const int type = w2 | (w3 << 3) | ((speed >> 4) & 2) | ((speed < 0xC0) << 2);

    int write = !active, addlow;
    if (type & 4)
    {
        const uint32_t c = pcm.tv_counter << 3;
        addlow = ((c >> 6) & 1) | ((c >> 4) & 2) | ((c >> 2) & 4) | (c & 8);
        write |= 1;
    }
    else
    {
        static const uint8_t shifts[] = { 0, 2, 4, 6 }, masks[]  = { 3, 15, 63, 127 };
        const int type_mask = (type & 3);
        const uint32_t c = pcm.tv_counter << 1 >> shifts[type_mask];
        addlow = ((c >> 6) & 1) | ((c >> 4) & 2) | ((c >> 2) & 4) | (c & 8);
        write |= !(pcm.tv_counter & masks[type_mask]);
    }

    if ((type & 8) == 0)
    {
        const int shift = (10 - (speed & 15)) & 15;

        int sum1 = (target << 11); // 5
        if (e != 2 || active)
            sum1 -= (*levelcur << 4); // 6

        int shifted = (sum1 >> shift) - sum1;

        int sum2 = (target << 11) + addlow + shifted;
        if (write && pcm.nfs)
            *levelcur = (sum2 >> 4) & 0x7fff;

        if (e != 2)
            *volmul = (sum2 >> 4) & 0x7ffe;
    }
    else
    {
        const int shift = (10 - (((speed >> 4) & 14) | w2)) & 15;

        int sum1 = target << 11; // 5
        if (e != 2 || active)
            sum1 -= (*levelcur << 4); // 6
        int preshift = ((speed & 15) << 9) | ((!w1) << 13);
        int neg = (sum1 & 0x80000);
        if (neg)
            preshift ^= ~0x3f;

        int sum2 = (preshift >> shift);
        if (e != 2 || active)
            sum2 += (*levelcur << 4) | addlow;
        int sum2_l = (sum2 >> 4);
        int sum3 = (target << 11) - (sum2_l << 4);
        int neg2 = (sum3 & 0x80000);
        int negxor = (neg2 ^ neg);

        if (write && pcm.nfs)
            *levelcur = (negxor ? (target << 7) : (sum2_l & 0x7fff));

        if (e == 0)
            *volmul = sum2_l & 0x7ffe;
        else if (e == 1)
            *volmul = (negxor ? (target << 7) : (sum2_l & 0x7ffe));
    }
}

static inline int eram_unpack(int addr, int type = 0)
{
    addr &= 0x3fff;
    int data = pcm.eram[addr];
    int val = data & 0x3fff;
    int sh = (data >> 14) & 3;

    val <<= 18;
    return val >> (18 - sh * 2 + type);
}

static inline void eram_pack(int addr, int val)
{
    addr &= 0x3fff;
    int sh = 0;
    int top = (val >> 13) & 0x7f;
    if (top & 0x40)
        top ^= 0x7f;
    if (top >= 16)
        sh = 3;
    else if (top >= 4)
        sh = 2;
    else if (top >= 1)
        sh = 1;
    else
        sh = 0;

    int data = (val >> (sh * 2)) & 0x3fff;
    data |= sh << 14;
    pcm.eram[addr] = data;
}

static void PCM_Update(uint64_t cycles)
{
    int reg_slots = (pcm.config_reg_3d & 31) + 1;
    int voice_active = pcm.voice_mask & pcm.voice_mask_pending;
    while (pcm.cycles < cycles)
    {
        int tt[2] = {};

        { // final mixing
            int noise_mask = 0;
            int orval = 0;
            int write_mask = 0;
            //int dac_mask = 0;
            if ((pcm.config_reg_3c & 0x30) != 0)
            {
                switch ((pcm.config_reg_3c >> 2) & 3)
                {
                    case 1:
                        noise_mask = 3;
                        break;
                    case 2:
                        noise_mask = 7;
                        break;
                    case 3:
                        noise_mask = 15;
                        break;
                }
                switch (pcm.config_reg_3c & 3)
                {
                    case 1:
                        orval |= 1 << 8;
                        break;
                    case 2:
                        orval |= 1 << 10;
                        break;
                }
                write_mask = 15;
                //dac_mask = ~15;
            }
            else
            {
                switch ((pcm.config_reg_3c >> 2) & 3)
                {
                    case 2:
                        noise_mask = 1;
                        break;
                    case 3:
                        noise_mask = 3;
                        break;
                }
                switch (pcm.config_reg_3c & 3)
                {
                    case 1:
                        orval |= 1 << 6;
                        break;
                    case 2:
                        orval |= 1 << 8;
                        break;
                }
                write_mask = 3;
                //dac_mask = ~3;
            }
            if ((pcm.config_reg_3c & 0x80) == 0)
                write_mask = 0;
            if ((pcm.config_reg_3c & 0x30) == 0x30)
                orval |= 1 << 12;


            int shifter = pcm.ram2[30][10];
            int xr = ((shifter >> 0) ^ (shifter >> 1) ^ (shifter >> 7) ^ (shifter >> 12)) & 1;
            shifter = (shifter >> 1) | (xr << 15);
            pcm.ram2[30][10] = shifter;

            pcm.accum_l = addclip20(pcm.accum_l, pcm.ram1[30][0], 0);
            pcm.accum_r = addclip20(pcm.accum_r, pcm.ram1[30][1], 0);

            pcm.ram1[30][2] = addclip20(pcm.accum_l,
                orval | (shifter & noise_mask), 0);

            pcm.ram1[30][4] = addclip20(pcm.accum_r,
                orval | (shifter & noise_mask), 0);

            pcm.ram1[30][0] = pcm.accum_l & write_mask;
            pcm.ram1[30][1] = pcm.accum_r & write_mask;


            tt[0] = (int)((pcm.ram1[30][2] & ~write_mask) << 12);
            tt[1] = (int)((pcm.ram1[30][4] & ~write_mask) << 12);

            MCU_PostSample(tt);

            xr = ((shifter >> 0) ^ (shifter >> 1) ^ (shifter >> 7) ^ (shifter >> 12)) & 1;
            shifter = (shifter >> 1) | (xr << 15);

            pcm.accum_l = addclip20(pcm.accum_l, pcm.ram1[30][0], 0);
            pcm.accum_r = addclip20(pcm.accum_r, pcm.ram1[30][1], 0);

            pcm.ram1[30][3] = addclip20(pcm.accum_l,
                orval | (shifter & noise_mask), 0);

            pcm.ram1[30][5] = addclip20(pcm.accum_r,
                orval | (shifter & noise_mask), 0);

#ifdef NUKEDSC55_USE_OVERSAMPLING
            if (pcm.config_reg_3c & 0x40) // oversampling
            {
                pcm.ram2[30][10] = shifter;

                pcm.ram1[30][0] = pcm.accum_l & write_mask;
                pcm.ram1[30][1] = pcm.accum_r & write_mask;


                tt[0] = (int)((pcm.ram1[30][3] & ~write_mask) << 12);
                tt[1] = (int)((pcm.ram1[30][5] & ~write_mask) << 12);

                MCU_PostSample(tt);
            }
#endif
        }

        { // global counter for envelopes
            if (!pcm.nfs)
                pcm.tv_counter = pcm.ram2[31][8]; // fixme

            pcm.tv_counter -= 1;

            pcm.tv_counter &= 0x3fff;
        }

        // chorus/reverb

        { // fixme
            if (pcm.ram2[31][8] & 0x8000)
                pcm.ram2[31][9] = pcm.ram2[31][8] & 0x7fff;
            else
                pcm.ram2[31][10] = pcm.ram2[31][8] & 0x7fff;

            if ((0x4000 - pcm.ram2[31][8]) & 0x8000)
                pcm.ram2[31][10] = (0x4000 - pcm.ram2[31][8]) & 0x7fff;
            else
                pcm.ram2[31][9] = (0x4000 - pcm.ram2[31][8]) & 0x7fff;
        }

        {
            int v1 = pcm.ram2[31][1];

            int m1 = multi(pcm.ram1[29][1], v1 >> 8) >> 5; // 14
            int m2 = multi(pcm.rcsum[1], v1 & 255) >> 5; // 15

            pcm.ram1[29][1] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1); // 16
        }

        {
            int okey = (pcm.ram2[31][7] & 0x20) != 0;
            int key = 1;
            int active = okey && key;
            int u = 0;
            calc_tv(1, pcm.ram2[30][0], &pcm.ram2[30][9], active, &u);
        }

        {
            int v1 = pcm.ram2[30][1];
            int m1 = multi(pcm.ram1[29][0], v1 >> 8) >> 5; // 17
            int m2 = multi(pcm.rcsum[0], v1 & 255) >> 5; // 18

            pcm.ram1[29][0] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1); // 19
        }

        int rcadd[6] = {};
        int rcadd2[6] = {};

        {
            {
                // 1
                int v1 = pcm.ram2[30][4];
                int m1 = multi(pcm.ram1[29][0], (v1 >> 8)) >> 6;
                int v2 = 0;
                int s1 = eram_unpack(pcm.ram2[28][1] + pcm.tv_counter, 1);
                int s2 = eram_unpack(pcm.ram2[28][1] + pcm.tv_counter);
                if ((v1 & 0x30) != 0)
                {
                    v2 = s1;
                }
                int v3 = addclip20(m1, v2 ^ 0xfffff, 1);
                pcm.ram1[29][4] = v3;
                int m2 = multi(v3, v1 & 255) >> 5;
                pcm.ram1[29][5] = addclip20(m2 >> 1, s2, m2 & 1);
            }
            {
                // 2
                int v1 = pcm.ram2[30][4];
                int v2 = 0;
                int s1 = eram_unpack(pcm.ram2[28][2] + pcm.tv_counter, 1);
                int s2 = eram_unpack(pcm.ram2[28][2] + pcm.tv_counter);
                if ((v1 & 0x30) != 0)
                {
                    v2 = s1;
                }
                int v3 = addclip20(pcm.ram1[29][5], v2 ^ 0xfffff, 1);
                pcm.ram1[29][5] = v3;
                int m2 = multi(v3, v1 & 255) >> 5;
                pcm.ram1[28][0] = addclip20(m2 >> 1, s2, m2 & 1);
            }
            {
                // 3
                int v1 = pcm.ram2[30][4];
                int v2 = 0;
                int s1 = eram_unpack(pcm.ram2[28][3] + pcm.tv_counter, 1);
                int s2 = eram_unpack(pcm.ram2[28][3] + pcm.tv_counter);
                if ((v1 & 0x30) != 0)
                {
                    v2 = s1;
                }
                int v3 = addclip20(pcm.ram1[28][0], v2 ^ 0xfffff, 1);
                pcm.ram1[28][0] = v3;
                int m2 = multi(v3, v1 & 255) >> 5;
                pcm.ram1[28][1] = addclip20(m2 >> 1, s2, m2 & 1);


                pcm.ram1[28][2] = eram_unpack(pcm.ram2[28][5] + pcm.tv_counter);
            }
            {
                // 4
                int v1 = pcm.ram2[30][5];
                int v2 = 0;
                int s1 = eram_unpack(pcm.ram2[28][4] + pcm.tv_counter, 1);
                int s2 = eram_unpack(pcm.ram2[28][4] + pcm.tv_counter);
                if ((v1 & 0x30) != 0)
                {
                    v2 = s1;
                }
                int v3 = addclip20(pcm.ram1[28][1], v2 ^ 0xfffff, 1);
                pcm.ram1[28][1] = v3;
                int m2 = multi(v3, v1 & 255) >> 5;
                pcm.ram1[28][3] = addclip20(m2 >> 1, s2, m2 & 1);


                pcm.ram1[28][4] = eram_unpack(pcm.ram2[29][1] + pcm.tv_counter);
            }
            {
                // 5

                int v1 = pcm.ram2[30][7];
                int m1 = multi(pcm.ram1[29][2], (v1 >> 8)) >> 5;
                int s1 = eram_unpack(pcm.ram2[29][0] + pcm.tv_counter);
                int m2 = multi(s1, v1 & 255) >> 5;
                pcm.ram1[29][2] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1);

                eram_pack(pcm.ram2[28][0] + pcm.tv_counter, pcm.ram1[29][4]);
            }
            {
                // 6

                int v1 = pcm.ram2[30][8];
                int m1 = multi(pcm.ram1[29][3], (v1 >> 8)) >> 5;
                int s1 = eram_unpack(pcm.ram2[29][8] + pcm.tv_counter);
                int m2 = multi(s1, v1 & 255) >> 5;
                pcm.ram1[29][3] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1);

                eram_pack(pcm.ram2[28][1] + pcm.tv_counter, pcm.ram1[29][5]);

                eram_pack(pcm.ram2[28][2] + pcm.tv_counter, pcm.ram1[28][0]);
            }
            {
                // 7

                int v1 = pcm.ram2[30][9];
                int v2 = pcm.ram1[28][3];
                int m1 = multi(pcm.ram1[29][2], (v1 >> 8)) >> 5;
                int m2 = multi(pcm.ram1[29][3], (v1 >> 8)) >> 5;
                pcm.ram1[28][3] = addclip20(v2, m1 >> 1, m1 & 1);
                pcm.ram1[28][5] = addclip20(v2, m2 >> 1, m2 & 1);

                eram_pack(pcm.ram2[28][3] + pcm.tv_counter, pcm.ram1[28][1]);
            }
            {
                // 8

                int v1 = pcm.ram2[30][6];
                int m1 = multi(pcm.ram1[28][2], v1 >> 8) >> 5;

                int v2 = addclip20(pcm.ram1[28][3], m1 >> 1, m1 & 1);
                pcm.ram1[28][3] = v2;
                int m2 = multi(v2, v1 & 255) >> 5;
                pcm.ram1[28][2] = addclip20(pcm.ram1[28][2], m2 >> 1, m2 & 1);


                pcm.ram1[28][1] = eram_unpack(pcm.ram2[28][9] + pcm.tv_counter);
            }
            {
                // 9

                int v1 = pcm.ram2[30][6];
                int m1 = multi(pcm.ram1[28][4], v1 >> 8) >> 5;

                int v2 = addclip20(pcm.ram1[28][5], m1 >> 1, m1 & 1);
                pcm.ram1[28][5] = v2;
                int m2 = multi(v2, v1 & 255) >> 5;
                pcm.ram1[28][4] = addclip20(pcm.ram1[28][4], m2 >> 1, m2 & 1);


                pcm.ram1[29][4] = eram_unpack(pcm.ram2[29][5] + pcm.tv_counter);
            }
            {
                // 10

                int v1 = pcm.ram2[30][6];
                int v2 = pcm.ram1[28][1];
                int m1 = multi(v2, v1 >> 8) >> 5;
                int s1 = eram_unpack(pcm.ram2[28][8] + pcm.tv_counter);
                int v3 = addclip20(m1 >> 1, s1, m1 & 1);
                pcm.ram1[28][1] = v3;
                int m2 = multi(v3, v1 & 255) >> 5;
                pcm.ram1[29][5] = addclip20(m2 >> 1, v2, m2 & 1);

                eram_pack(pcm.ram2[28][4] + pcm.tv_counter, pcm.ram1[28][3]);
            }
            {
                // 11

                int v1 = pcm.ram2[30][6];
                int v2 = pcm.ram1[29][4];
                int m1 = multi(v2, v1 >> 8) >> 5;
                int s1 = eram_unpack(pcm.ram2[29][4] + pcm.tv_counter);
                int v3 = addclip20(m1 >> 1, s1, m1 & 1);
                pcm.ram1[29][4] = v3;
                int m2 = multi(v3, v1 & 255) >> 5;
                pcm.ram1[28][0] = addclip20(m2 >> 1, v2, m2 & 1);


                eram_pack(pcm.ram2[28][5] + pcm.tv_counter, pcm.ram1[28][2]);

                eram_pack(pcm.ram2[29][0] + pcm.tv_counter, pcm.ram1[28][5]);
            }
            {
                // 12

                pcm.ram1[28][5] = eram_unpack(pcm.ram2[28][6] + pcm.tv_counter);
            }

            {
                // 13

                int s1 = eram_unpack(pcm.ram2[28][10] + pcm.tv_counter);
                pcm.ram1[28][5] = addclip20(pcm.ram1[28][5], s1, 0);

                pcm.ram1[28][2] = eram_unpack(pcm.ram2[29][2] + pcm.tv_counter);
            }

            {
                // 14

                int s1 = eram_unpack(pcm.ram2[29][6] + pcm.tv_counter);
                int t1 = addclip20(s1, pcm.ram1[28][2], 0); // 6

                pcm.ram1[28][5] = addclip20(t1, pcm.ram1[28][5], 0);

                pcm.ram1[28][2] = eram_unpack(pcm.ram2[28][7] + pcm.tv_counter);
            }

            {
                // 15

                int s1 = eram_unpack(pcm.ram2[28][11] + pcm.tv_counter);
                pcm.ram1[28][2] = addclip20(pcm.ram1[28][2], s1, 0);

                pcm.ram1[28][3] = eram_unpack(pcm.ram2[29][3] + pcm.tv_counter);
            }

            {
                // 16

                int s1 = eram_unpack(pcm.ram2[29][7] + pcm.tv_counter);
                int t1 = addclip20(s1, pcm.ram1[28][2], 0);
                pcm.ram1[28][2] = addclip20(t1, pcm.ram1[28][3], 0);


                eram_pack(pcm.ram2[29][1] + pcm.tv_counter, pcm.ram1[28][4]);

                eram_pack(pcm.ram2[28][8] + pcm.tv_counter, pcm.ram1[28][1]);
            }

            {
                // 17
                int v1 = pcm.ram2[30][2];
                int v2 = pcm.ram1[28][5];

                int m1 = multi(v2, v1 >> 8) >> 5;

                rcadd[0] = m1;

                rcadd2[0] = multi(v2, v1 & 255) >> 5;

                int t1 = eram_unpack(pcm.ram2[29][10] + pcm.tv_counter + 1); //? 3a6e
                eram_pack(pcm.ram2[28][9] + pcm.tv_counter, pcm.ram1[29][5]);
                pcm.ram1[29][5] = t1;
            }

            {
                // 18
                int v1 = pcm.ram2[30][3];
                int v2 = pcm.ram1[28][2];

                int m1 = multi(v2, v1 >> 8) >> 5;

                rcadd[1] = m1;

                rcadd2[1] = multi(v2, v1 & 255) >> 5;

                pcm.ram1[28][1] = eram_unpack(pcm.ram2[29][11] + pcm.tv_counter + 1); //? 3a1e
            }
            {
                // 19

                int v1 = pcm.ram2[31][9];

                int s1 = eram_unpack(pcm.ram2[29][10] + pcm.tv_counter); //? 3a6d

                eram_pack(pcm.ram2[29][4] + pcm.tv_counter, pcm.ram1[29][4]);

                int m1 = multi(s1, v1 >> 8) >> 5;
                int m2 = multi(pcm.ram1[29][5], v1 >> 8) >> 5;

                int t2 = addclip20(s1, (m1 >> 1) ^ 0xfffff, 1);

                pcm.ram1[29][5] = addclip20(t2, m2 >> 1, m2 & 1);
            }
            {
                // 20

                int v1 = pcm.ram2[31][10];

                int s1 = eram_unpack(pcm.ram2[29][11] + pcm.tv_counter); //? 3a1d

                eram_pack(pcm.ram2[29][5] + pcm.tv_counter, pcm.ram1[28][0]);

                int m1 = multi(s1, v1 >> 8) >> 5;
                int m2 = multi(pcm.ram1[28][1], v1 >> 8) >> 5;

                int t2 = addclip20(s1, (m1 >> 1) ^ 0xfffff, 1);

                pcm.ram1[28][1] = addclip20(t2, m2 >> 1, m2 & 1);

                eram_pack(pcm.ram2[29][9] + pcm.tv_counter, pcm.ram1[29][1]);
            }
            {
                // 21

                int v1 = pcm.ram2[31][2];
                int v2 = pcm.ram1[29][5];

                int m1 = multi(v2, v1 >> 8) >> 5;
                int m2 = multi(v2, v1 & 255) >> 5;

                rcadd[2] = m1;
                rcadd2[2] = m2;
            }
            {
                // 22

                int v1 = pcm.ram2[31][3];
                int v2 = pcm.ram1[29][5];

                int m1 = multi(v2, v1 >> 8) >> 5;
                int m2 = multi(v2, v1 & 255) >> 5;

                rcadd[3] = m1;
                rcadd2[3] = m2;
            }
            {
                // 23

                int v1 = pcm.ram2[31][4];
                int v2 = pcm.ram1[28][1];

                int m1 = multi(v2, v1 >> 8) >> 5;
                int m2 = multi(v2, v1 & 255) >> 5;

                rcadd[4] = m1;
                rcadd2[4] = m2;
            }
            {
                // 31

                int v1 = pcm.ram2[31][5];
                int v2 = pcm.ram1[28][1];

                int m1 = multi(v2, v1 >> 8) >> 5;
                int m2 = multi(v2, v1 & 255) >> 5;

                rcadd[5] = m1;
                rcadd2[5] = m2;

                {
                    // address generator

                    int key = 1;
                    int okey = (pcm.ram2[31][7] & 0x20) != 0;
                    int active = key && okey;
                    int kon = key && !okey;

                    int b15 = (pcm.ram2[31][8] & 0x8000) != 0; // 0
                    int b6 = (pcm.ram2[31][7] & 0x40) != 0; // 1
                    int b7 = (pcm.ram2[31][7] & 0x80) != 0; // 1
                    //int old_nibble = (pcm.ram2[31][7] >> 12) & 15; // 1

                    int address = pcm.ram1[31][4]; // 0
                    int address_end = pcm.ram1[31][0]; // 1 or 2
                    int address_loop = pcm.ram1[31][2]; // 2 or 1

                    int sub_phase = (pcm.ram2[31][8] & 0x3fff); // 1
                    //int interp_ratio = (sub_phase >> 7) & 127;
                    sub_phase += pcm.ram2[pcm.ram2[31][7] & 31][0]; // 5
                    int sub_phase_of = (sub_phase >> 14) & 7;
                    if (pcm.nfs)
                    {
                        pcm.ram2[31][8] &= ~0x3fff;
                        pcm.ram2[31][8] |= sub_phase & 0x3fff;
                    }


                    // address 0
                    int address_cnt = address;

                    int cmp1 = b15 ? address_loop : address_end;
                    int cmp2 = address_cnt;
                    int address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 9
                    int next_b15 = b15;

                    int next_address = address_cnt; // 11

                    cmp1 = (!b6 && address_cmp) ? address_loop : address_cnt;
                    cmp2 = address_cnt;
                    int address_cnt2 = (kon || (!b6 && address_cmp)) ? cmp1 : cmp2;

                    int address_add = (!address_cmp && b6 && !b15) || (!address_cmp && !b6);
                    int address_sub = !address_cmp && b6 && b15;
                    if (b7)
                        address_cnt2 -= address_add - address_sub;
                    else
                        address_cnt2 += address_add - address_sub;
                    address_cnt = address_cnt2 & 0xfffff; // 11
                    b15 = b6 && (b15 ^ address_cmp); // 11

                    cmp1 = b15 ? address_loop : address_end;
                    cmp2 = address_cnt;
                    address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 13

                    if (sub_phase_of >= 1)
                    {
                        next_address = address_cnt; // 13
                        next_b15 = b15;
                    }

                    if (active && pcm.nfs)
                        pcm.ram1[31][4] = next_address;

                    if (pcm.nfs)
                    {
                        pcm.ram2[31][8] &= ~0x8000;
                        pcm.ram2[31][8] |= next_b15 << 15;
                    }

                    int t1 = address_loop; // 18
                    int t2 = pcm.ram1[31][4] - t1; // 19
                    int t3 = address_end - t2; // 20
                    int t4 = pcm.ram1[31][4]; // 23

                    pcm.ram2[29][10] = t3;
                    pcm.ram2[29][11] = t4;
                }
            }
        }

        pcm.ram1[31][1] = 0;
        pcm.ram1[31][3] = 0;
        pcm.rcsum[0] = 0;
        pcm.rcsum[1] = 0;

        for (int slot = 0; slot < reg_slots; slot++)
        {
            uint32_t *ram1 = pcm.ram1[slot];
            uint16_t *ram2 = pcm.ram2[slot];
            int okey = (ram2[7] & 0x20) != 0;
            int key = (voice_active >> slot) & 1;

            int active = okey && key;
            int kon = key && !okey;

            // address generator

            int b15 = (ram2[8] & 0x8000) != 0; // 0
            int b6 = (ram2[7] & 0x40) != 0; // 1
            int b7 = (ram2[7] & 0x80) != 0; // 1
            int hiaddr = (ram2[7] >> 8) & 15; // 1
            int old_nibble = (ram2[7] >> 12) & 15; // 1

            int address = ram1[4]; // 0
            int address_end = ram1[0]; // 1 or 2
            int address_loop = ram1[2]; // 2 or 1

            int cmp1 = b15 ? address_loop : address_end;
            int cmp2 = address;
            int nibble_cmp1 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 2
            int irq_flag = 0;

            // fixme:
            if (kon)
                irq_flag = ((cmp1 + address_loop) & 0x100000) != 0;
            else
                irq_flag = ((address + ((-address_loop) & 0xfffff)) & 0x100000) != 0;
            irq_flag ^= b7;

            int nibble_address = (!b6 && nibble_cmp1) ? address_loop : address; // 3
            int address_b4 = (nibble_address & 0x10) != 0;
            int wave_address = nibble_address >> 5;
            int xor2 = (address_b4 ^ b7);
            int check1 = xor2 && active;
            int xor1 = (b15 ^ !nibble_cmp1);
            int nibble_add = b6 ? check1 && xor1 : (!nibble_cmp1 && check1);
            int nibble_subtract = b6 && !xor1 && active && !xor2;
            if (b7)
                wave_address -= nibble_add - nibble_subtract;
            else
                wave_address += nibble_add - nibble_subtract;
            wave_address &= 0xfffff;

            int newnibble = PCM_ReadROM((hiaddr << 20) | wave_address);
            int newnibble_sel = address_b4 ^ ((b6 || !nibble_cmp1) && okey);
            if (newnibble_sel)
                newnibble = (newnibble >> 4) & 15;
            else
                newnibble &= 15;

            int sub_phase = (ram2[8] & 0x3fff); // 1
            int interp_ratio = (sub_phase >> 7) & 127;
            sub_phase += pcm.ram2[ram2[7] & 31][0]; // 5
            int sub_phase_of = (sub_phase >> 14) & 7;
            if (pcm.nfs)
            {
                ram2[8] &= ~0x3fff;
                ram2[8] |= sub_phase & 0x3fff;
            }


            // address 0
            int address_cnt = address;
            int samp0 = (int8_t)PCM_ReadROM((hiaddr << 20) | address_cnt); // 18

            cmp1 = address;
            cmp2 = address_cnt;
            int nibble_cmp2 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 8
            cmp1 = b15 ? address_loop : address_end;
            cmp2 = address_cnt;
            int address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 9

            int next_address = address_cnt; // 11
            int usenew = !nibble_cmp2;
            int next_b15 = b15;

            cmp1 = (!b6 && address_cmp) ? address_loop : address_cnt;
            cmp2 = address_cnt;
            int address_cnt2 = (kon || (!b6 && address_cmp)) ? cmp1 : cmp2;

            int address_add = (!address_cmp && b6 && !b15) || (!address_cmp && !b6);
            int address_sub = !address_cmp && b6 && b15;
            if (b7)
                address_cnt2 -= address_add - address_sub;
            else
                address_cnt2 += address_add - address_sub;
            address_cnt = address_cnt2 & 0xfffff; // 11
            b15 = b6 && (b15 ^ address_cmp); // 11

            int samp1 = (int8_t)PCM_ReadROM((hiaddr << 20) | address_cnt); // 20

            cmp1 = address;
            cmp2 = address_cnt;
            int nibble_cmp3 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 12
            cmp1 = b15 ? address_loop : address_end;
            cmp2 = address_cnt;
            address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 13

            if (sub_phase_of >= 1)
            {
                next_address = address_cnt; // 13
                usenew = !nibble_cmp3;
                next_b15 = b15;
            }

            cmp1 = (!b6 && address_cmp) ? address_loop : address_cnt;
            cmp2 = address_cnt;
            address_cnt2 = (kon || (!b6 && address_cmp)) ? cmp1 : cmp2;

            address_add = (!address_cmp && b6 && !b15) || (!address_cmp && !b6);
            address_sub = !address_cmp && b6 && b15;
            if (b7)
                address_cnt2 -= address_add - address_sub;
            else
                address_cnt2 += address_add - address_sub;
            address_cnt = address_cnt2 & 0xfffff; // 15
            b15 = b6 && (b15 ^ address_cmp); // 15

            int samp2 = (int8_t)PCM_ReadROM((hiaddr << 20) | address_cnt); // 1

            cmp1 = address;
            cmp2 = address_cnt;
            int nibble_cmp4 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 16
            cmp1 = b15 ? address_loop : address_end;
            cmp2 = address_cnt;
            address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 17

            if (sub_phase_of >= 2)
            {
                next_address = address_cnt; // 17
                usenew = !nibble_cmp4;
                next_b15 = b15;
            }

            cmp1 = (!b6 && address_cmp) ? address_loop : address_cnt;
            cmp2 = address_cnt;
            address_cnt2 = (kon || (!b6 && address_cmp)) ? cmp1 : cmp2;

            address_add = (!address_cmp && b6 && !b15) || (!address_cmp && !b6);
            address_sub = !address_cmp && b6 && b15;
            if (b7)
                address_cnt2 -= address_add - address_sub;
            else
                address_cnt2 += address_add - address_sub;
            address_cnt = address_cnt2 & 0xfffff; // 19
            b15 = b6 && (b15 ^ address_cmp); // 19

            int samp3 = (int8_t)PCM_ReadROM((hiaddr << 20) | address_cnt); // 5

            cmp1 = address;
            cmp2 = address_cnt;
            int nibble_cmp5 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 20
            cmp1 = b15 ? address_loop : address_end;
            cmp2 = address_cnt;
            address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 21

            if (sub_phase_of >= 3)
            {
                next_address = address_cnt; // 21
                usenew = !nibble_cmp5;
                next_b15 = b15;
            }

            cmp1 = (!b6 && address_cmp) ? address_loop : address_cnt;
            cmp2 = address_cnt;
            address_cnt2 = (kon || (!b6 && address_cmp)) ? cmp1 : cmp2;

            address_add = (!address_cmp && b6 && !b15) || (!address_cmp && !b6);
            address_sub = !address_cmp && b6 && b15;
            if (b7)
                address_cnt2 -= address_add - address_sub;
            else
                address_cnt2 += address_add - address_sub;
            address_cnt = address_cnt2 & 0xfffff; // 23
            // b15 = b6 && (b15 ^ address_cmp); // 23

            cmp1 = address;
            cmp2 = address_cnt;
            int nibble_cmp6 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 24

            if (sub_phase_of >= 4)
            {
                next_address = address_cnt; // 1
                usenew = !nibble_cmp6;
                // b15 is not updated?
            }

            if (active && pcm.nfs)
                ram1[4] = next_address;

            if (pcm.nfs)
            {
                ram2[8] &= ~0x8000;
                ram2[8] |= next_b15 << 15;
            }

            // dpcm

            // 18
            int reference = ram1[5];

            // 19
            int preshift = samp0 << 10;
            int select_nibble = nibble_cmp2 ? old_nibble : newnibble;
            int shift = (10 - select_nibble) & 15;

            int shifted = (preshift << 1) >> shift;

            if (sub_phase_of >= 1)
                reference = addclip20(reference, shifted >> 1, shifted & 1);

            preshift = samp1 << 10;
            select_nibble = nibble_cmp3 ? old_nibble : newnibble;
            shift = (10 - select_nibble) & 15;

            shifted = (preshift << 1) >> shift;

            if (sub_phase_of >= 2)
                reference = addclip20(reference, shifted >> 1, shifted & 1);

            preshift = samp2 << 10;
            select_nibble = nibble_cmp4 ? old_nibble : newnibble;
            shift = (10 - select_nibble) & 15;

            shifted = (preshift << 1) >> shift;

            if (sub_phase_of >= 3)
                reference = addclip20(reference, shifted >> 1, shifted & 1);

            preshift = samp3 << 10;
            select_nibble = nibble_cmp5 ? old_nibble : newnibble;
            shift = (10 - select_nibble) & 15;

            shifted = (preshift << 1) >> shift;

            if (sub_phase_of >= 4)
                reference = addclip20(reference, shifted >> 1, shifted & 1);

            // interpolation

            int test = ram1[5];

            int step0 = multi(interp_lut[0][interp_ratio] << 6, samp0) >> 8;
            select_nibble = nibble_cmp2 ? old_nibble : newnibble;
            shift = (10 - select_nibble) & 15;
            step0 =  (step0 << 1) >> shift;

            test = addclip20(test, step0 >> 1, step0 & 1);


            int step1 = multi(interp_lut[1][interp_ratio] << 6, samp1) >> 8;
            select_nibble = nibble_cmp3 ? old_nibble : newnibble;
            shift = (10 - select_nibble) & 15;
            step1 = (step1 << 1) >> shift;

            test = addclip20(test, step1 >> 1, step1 & 1);

            int step2 = multi(interp_lut[2][interp_ratio] << 6, samp2) >> 8;
            select_nibble = nibble_cmp4 ? old_nibble : newnibble;
            shift = (10 - select_nibble) & 15;
            step2 = (step2 << 1) >> shift;

            int reg1 = ram1[1];
            int reg3 = ram1[3];
            int reg2_6 = (ram2[6] >> 8) & 127;

            test = addclip20(test, step2 >> 1, step2 & 1);

            int filter = ram2[11];
            int v3;

            if (mcu_mk1)
            {
                int mult1 = multi(reg1, filter >> 8); // 8
                int mult2 = multi(reg1, (filter >> 1) & 127); // 9
                int mult3 = multi(reg1, reg2_6); // 10

                int v2 = addclip20(reg3, mult1 >> 6, (mult1 >> 5) & 1); // 9
                int v1 = addclip20(v2, mult2 >> 13, (mult2 >> 12) & 1); // 10
                int subvar = addclip20(v1, (mult3 >> 6), (mult3 >> 5) & 1); // 11

                ram1[3] = v1;

                v3 = addclip20(test, subvar ^ 0xfffff, 1); // 12

                int mult4 = multi(v3, filter >> 8);
                int mult5 = multi(v3, (filter >> 1) & 127);
                int v4 = addclip20(reg1, mult4 >> 6, (mult4 >> 5) & 1); // 14
                int v5 = addclip20(v4, mult5 >> 13, (mult5 >> 12) & 1); // 15

                ram1[1] = v5;
            }
            else
            {
                // hack: use 32-bit math to avoid overflow
                int mult1 = reg1 * (int8_t)(filter >> 8); // 8
                int mult2 = reg1 * (int8_t)((filter >> 1) & 127); // 9
                int mult3 = reg1 * (int8_t)reg2_6; // 10

                int v2 = reg3 + (mult1 >> 6) + ((mult1 >> 5) & 1); // 9
                int v1 = v2 + (mult2 >> 13) + ((mult2 >> 12) & 1); // 10
                int subvar = v1 + (mult3 >> 6) + ((mult3 >> 5) & 1); // 11

                ram1[3] = v1;

                int tests = test;
                tests <<= 12;
                tests >>= 12;

                v3 = tests - subvar; // 12

                int mult4 = v3 * (int8_t)(filter >> 8);
                int mult5 = v3 * (int8_t)((filter >> 1) & 127);
                int v4 = reg1 + (mult4 >> 6) + ((mult4 >> 5) & 1); // 14
                int v5 = v4 + (mult5 >> 13) + ((mult5 >> 12) & 1); // 15

                ram1[1] = v5;
            }


            ram1[5] = reference;

            if (active && (ram2[6] & 1) != 0 && (ram2[8] & 0x4000) == 0 && !pcm.irq_assert && irq_flag)
            {
                //printf("irq voice %i\n", slot);
                if (pcm.nfs)
                    ram2[8] |= 0x4000;
                pcm.irq_assert = 1;
                pcm.irq_channel = slot;
                if (mcu_jv880)
                    MCU_GA_SetGAInt(5, 1);
                else
                    MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_IRQ0, 1);
            }

            int volmul1 = 0;
            int volmul2 = 0;

            calc_tv(0, ram2[3], &ram2[9], active, &volmul1);
            calc_tv(1, ram2[4], &ram2[10], active, &volmul2);
            calc_tv(2, ram2[5], &ram2[11], active, NULL);

            // if (volmul1 && volmul2)
            //     volmul1 += 0;

            int sample = (ram2[6] & 2) == 0 ? ram1[3] : v3;
            //sample = test;

            int multiv1 = multi(sample, volmul1 >> 8);
            int multiv2 = multi(sample, (volmul1 >> 1) & 127);

            int sample2 = addclip20(multiv1 >> 6, multiv2 >> 13, ((multiv2 >> 12) | (multiv1 >> 5)) & 1);

            int multiv3 = multi(sample2, volmul2 >> 8);
            int multiv4 = multi(sample2, (volmul2 >> 1) & 127);

            int sample3 = addclip20(multiv3 >> 6, multiv4 >> 13, ((multiv4 >> 12) | (multiv3 >> 5)) & 1);

            int pan = active ? ram2[1] : 0;
            int rc = active ? ram2[2] : 0;

            int sampl = multi(sample3, (pan >> 8) & 255);
            int sampr = multi(sample3, (pan >> 0) & 255);

            int rc0 = multi(sample3, (rc >> 8) & 255) >> 5; // reverb
            int rc1 = multi(sample3, (rc >> 0) & 255) >> 5; // chorus

            // mix reverb/chorus?
            int slot2 = (slot == reg_slots - 1) ? 31 : slot + 1;
            switch (slot2)
            {
                // 17, 18 - reverb

                case 17:
                    pcm.ram1[31][1] = addclip20(pcm.ram1[31][1], rcadd[0] >> 1, rcadd[0] & 1);
                    break;
                case 18:
                    pcm.ram1[31][3] = addclip20(pcm.ram1[31][3], rcadd[1] >> 1, rcadd[1] & 1);
                    break;
                case 21:
                    pcm.ram1[31][1] = addclip20(pcm.ram1[31][1], rcadd[2] >> 1, rcadd[2] & 1);
                    break;
                case 22:
                    pcm.ram1[31][3] = addclip20(pcm.ram1[31][3], rcadd[3] >> 1, rcadd[3] & 1);
                    break;
                case 23:
                    pcm.ram1[31][1] = addclip20(pcm.ram1[31][1], rcadd[4] >> 1, rcadd[4] & 1);
                    break;
                case 31:
                    pcm.ram1[31][3] = addclip20(pcm.ram1[31][3], rcadd[5] >> 1, rcadd[5] & 1);
                    break;
            }

            int suml = addclip20(pcm.ram1[31][1], sampl >> 6, (sampl >> 5) & 1);
            int sumr = addclip20(pcm.ram1[31][3], sampr >> 6, (sampr >> 5) & 1);

            switch (slot2)
            {
                case 17:
                    pcm.rcsum[1] = addclip20(pcm.rcsum[1], rcadd2[0] >> 1, rcadd2[0] & 1);
                    break;
                case 18:
                    pcm.rcsum[1] = addclip20(pcm.rcsum[1], rcadd2[1] >> 1, rcadd2[1] & 1);
                    break;
                case 21:
                    pcm.rcsum[0] = addclip20(pcm.rcsum[0], rcadd2[2] >> 1, rcadd2[2] & 1);
                    break;
                case 22:
                    pcm.rcsum[1] = addclip20(pcm.rcsum[1], rcadd2[3] >> 1, rcadd2[3] & 1);
                    break;
                case 23:
                    pcm.rcsum[0] = addclip20(pcm.rcsum[0], rcadd2[4] >> 1, rcadd2[4] & 1);
                    break;
                case 31:
                    pcm.rcsum[1] = addclip20(pcm.rcsum[1], rcadd2[5] >> 1, rcadd2[5] & 1);
                    break;
            }

            pcm.rcsum[0] = addclip20(pcm.rcsum[0], rc0 >> 1, rc0 & 1);
            pcm.rcsum[1] = addclip20(pcm.rcsum[1], rc1 >> 1, rc1 & 1);

            if (slot != reg_slots - 1)
            {
                pcm.ram1[31][1] = suml;
                pcm.ram1[31][3] = sumr;
            }
            else
            {
                pcm.accum_l = suml;
                pcm.accum_r = sumr;
            }

            if (key && pcm.nfs)
            {
                ram2[7] &= ~0xf020;
                ram2[7] |= ((usenew || kon) ? newnibble : old_nibble) << 12;

                // update key
                ram2[7] |= key << 5;
            }

            if (!active)
            {
                if (pcm.nfs)
                {
                    ram1[1] = 0;
                    ram1[3] = 0;
                    ram1[5] = 0;
                }

                ram2[8] = 0;
                ram2[9] = 0;
                ram2[10] = 0;
            }
        }

        if (pcm.nfs)
        {
            pcm.ram2[31][7] |= 0x20;
        }

        pcm.nfs = 1;

        int cycles = (reg_slots + 1) * 25;

        pcm.cycles += mcu_jv880 ? (cycles * 25) / 29 : cycles;
    }
}
#endif // NUKEDSC55_PCM_CPP

#ifndef NUKEDSC55_SUBMCU_CPP
#define NUKEDSC55_SUBMCU_CPP
enum {
    SM_STATUS_C = 1,
    SM_STATUS_Z = 2,
    SM_STATUS_I = 4,
    SM_STATUS_D = 8,
    SM_STATUS_B = 16,
    SM_STATUS_T = 32,
    SM_STATUS_V = 64,
    SM_STATUS_N = 128
};

enum {
    SM_VECTOR_UART3_TX = 0,
    SM_VECTOR_UART2_TX,
    SM_VECTOR_UART1_TX,
    SM_VECTOR_COLLISION,
    SM_VECTOR_TIMER_X,
    SM_VECTOR_IPCM0,
    SM_VECTOR_UART3_RX,
    SM_VECTOR_UART2_RX,
    SM_VECTOR_UART1_RX,
    SM_VECTOR_RESET
};

enum {
    SM_DEV_P1_DATA = 0x00,
    SM_DEV_P1_DIR = 0x01,
    SM_DEV_RAM_DIR = 0x02,
    SM_DEV_UART1_MODE_STATUS = 0x05,
    SM_DEV_UART1_CTRL = 0x06,
    SM_DEV_UART2_DATA = 0x08,
    SM_DEV_UART2_MODE_STATUS = 0x09,
    SM_DEV_UART2_CTRL = 0x0a,
    SM_DEV_UART3_MODE_STATUS = 0x0d,
    SM_DEV_UART3_CTRL = 0x0e,
    SM_DEV_IPCM0 = 0x10,
    SM_DEV_IPCM1 = 0x11,
    SM_DEV_IPCM2 = 0x12,
    SM_DEV_IPCM3 = 0x13,
    SM_DEV_IPCE0 = 0x14,
    SM_DEV_IPCE1 = 0x15,
    SM_DEV_IPCE2 = 0x16,
    SM_DEV_IPCE3 = 0x17,
    SM_DEV_SEMAPHORE = 0x19,
    SM_DEV_COLLISION = 0x1a,
    SM_DEV_INT_ENABLE = 0x1b,
    SM_DEV_INT_REQUEST = 0x1c,
    SM_DEV_PRESCALER = 0x1d,
    SM_DEV_TIMER = 0x1e,
    SM_DEV_TIMER_CTRL = 0x1f
};

static void SM_ErrorTrap(void)
{
    printf("%.4x\n", sm.pc);
}

static uint8_t SM_Read(uint16_t address)
{
    address &= 0x1fff;
    if (address & 0x1000)
    {
        return sm_rom[address & 0xfff];
    }
    else if (address < 0x80)
    {
        return sm_ram[address];
    }
    else if (address >= 0xc0 && address < 0xd8)
    {
        return sm_access[address & 0x1f];
    }
    else if (address >= 0xe0 && address < 0x100)
    {
        address &= 0x1f;
        switch (address)
        {
            case SM_DEV_UART2_DATA:
            {
                sm_uart_rx_gotbyte = 0;
                return sm_uart_rx_byte;
            }
            case SM_DEV_UART1_MODE_STATUS:
            {
                uint8_t ret = 0;
                ret |= 5;
                return ret;
            }
            case SM_DEV_UART2_MODE_STATUS:
            {
                uint8_t ret = sm_uart_rx_gotbyte << 1;
                ret |= 5;
                return ret;
            }
            case SM_DEV_UART3_MODE_STATUS:
            {
                uint8_t ret = 0;
                ret |= 5;
                return ret;
            }
            case SM_DEV_P1_DATA:
                return MCU_ReadP1();
            case SM_DEV_P1_DIR:
                return sm_p1_dir;
            case SM_DEV_PRESCALER:
                return sm_timer_prescaler;
            case SM_DEV_TIMER:
                return sm_timer_counter;
        }
        return sm_device_mode[address];
    }
    else if (address >= 0x200 && address < 0x2c0)
    {
        address &= 0xff;
        if (sm_device_mode[SM_DEV_RAM_DIR] & (1<<(address>>5)))
            sm_access[address>>3] &= ~(1<<(address&7));
        return sm_shared_ram[address];
    }
    else
    {
        printf("sm: unknown read %x\n", address);
        return 0;
    }
}

static void SM_Write(uint16_t address, uint8_t data)
{
    address &= 0x1fff;
    if (address < 0x80)
    {
        sm_ram[address] = data;
    }
    else if (address >= 0xe0 && address < 0x100)
    {
        address &= 0x1f;
        switch (address)
        {
            case SM_DEV_P1_DATA:
                MCU_WriteP1(data);
                break;
            case SM_DEV_P1_DIR:
                sm_p1_dir = data;
                break;
            case SM_DEV_IPCM0:
            case SM_DEV_IPCM1:
            case SM_DEV_IPCM2:
            case SM_DEV_IPCM3:
                sm_device_mode[address] = data;
                break;
            case SM_DEV_IPCE0:
            case SM_DEV_IPCE1:
            case SM_DEV_IPCE2:
            case SM_DEV_IPCE3:
                sm_device_mode[address] = data;
                break;
            case SM_DEV_INT_REQUEST:
                sm_device_mode[SM_DEV_INT_REQUEST] &= data;
                break;
            case SM_DEV_COLLISION:
                sm_device_mode[SM_DEV_COLLISION] &= ~0x7f;
                sm_device_mode[SM_DEV_COLLISION] |= data & 0x7f;
                if ((data & 0x80) == 0)
                    sm_device_mode[SM_DEV_COLLISION] &= ~0x80;
                break;
            default:
                sm_device_mode[address] = data;
                break;
        }
        if (address == SM_DEV_UART3_MODE_STATUS || address == SM_DEV_UART3_CTRL)
            MCU_GA_SetGAInt(5, (sm_device_mode[SM_DEV_UART3_MODE_STATUS] & 0x80) != 0
                && (sm_device_mode[SM_DEV_UART3_CTRL] & 0x20) == 0);
    }
    else if (address >= 0x200 && address < 0x2c0)
    {
        address &= 0xff;
        sm_access[address>>3] |= 1<<(address&7);
        sm_shared_ram[address] = data;
    }
    else
    {
        printf("sm: unknown write %x %x\n", address, data);
    }
}

static void SM_SysWrite(uint32_t address, uint8_t data)
{
    address &= 0xff;
    if (address < 0xc0)
    {
        address &= 0xff;
        sm_access[address>>3] |= 1<<(address&7);
        sm_shared_ram[address] = data;
    }
    else if (address >= 0xf8 && address < 0xfc)
    {
        sm_device_mode[SM_DEV_IPCM0 + (address & 3)] = data;
        if ((address & 3) == 0)
        {
            sm_device_mode[SM_DEV_INT_REQUEST] |= 0x10;
            sm_device_mode[SM_DEV_SEMAPHORE] &= ~0x80;
        }
    }
    else if (address == 0xff)
    {
        sm_device_mode[SM_DEV_SEMAPHORE] &= ~0x1f;
        sm_device_mode[SM_DEV_SEMAPHORE] |= data & 0x1f;
    }
    else if (address == 0xf5)
    {
        MCU_WriteP1(data);
    }
    else if (address == 0xf6)
    {
        MCU_WriteP0(data);
    }
    else if (address == 0xf7)
    {
        sm_p0_dir = data;
    }
    else
    {
        printf("sm: unknown sys write %x %x\n", address, data);
    }
}

static uint8_t SM_SysRead(uint32_t address)
{
    address &= 0xff;
    if (address < 0xc0)
    {
        if ((sm_device_mode[SM_DEV_RAM_DIR] & (1<<(address>>5))) == 0)
            sm_access[address>>3] &= ~(1<<(address&7));
        return sm_shared_ram[address];
    }
    else if (address >= 0xf8 && address < 0xfc)
    {
        if ((address & 3) == 0)
        {
            sm_device_mode[SM_DEV_INT_REQUEST] |= 0x10;
        }
        uint8_t val = sm_device_mode[SM_DEV_IPCE0 + (address & 3)];
        sm_device_mode[SM_DEV_IPCE0 + (address & 3)] = 0; // FIXME
        return val;
    }
    else if (address == 0xff)
    {
        return sm_device_mode[SM_DEV_SEMAPHORE];
    }
    else if (address == 0xf5)
    {
        return MCU_ReadP1();
    }
    else if (address == 0xf6)
    {
        return MCU_ReadP0();
    }
    else if (address == 0xf7)
    {
        return sm_p0_dir;
    }
    else
    {
        printf("sm: unknown sys read %x\n", address);
        return 0;
    }
}

static uint16_t SM_GetVectorAddress(uint32_t vector)
{
    uint16_t pc = SM_Read(0x1fec + vector * 2);
    pc |= SM_Read(0x1fec + vector * 2 + 1) << 8;
    return pc;
}

static void SM_SetStatus(uint32_t condition, uint32_t mask)
{
    if (condition)
        sm.sr |= mask;
    else
        sm.sr &= ~mask;
}

static void SM_Reset(void)
{
    // Added zeroing of other globals
    memset(&sm_access, 0, sizeof(sm_access));
    memset(&sm_device_mode, 0, sizeof(sm_device_mode));
    sm_p0_dir = sm_p1_dir = sm_cts = sm_timer_prescaler = sm_timer_counter = sm_uart_rx_gotbyte = sm_uart_rx_byte = 0;
    sm_uart_rx_delay = 0;

    memset(&sm, 0, sizeof(sm));
    sm.pc = SM_GetVectorAddress(SM_VECTOR_RESET);
}

static uint8_t SM_ReadAdvance(void)
{
    uint8_t byte = SM_Read(sm.pc);
    sm.pc++;
    return byte;
}

static uint16_t SM_ReadAdvance16(void)
{
    uint16_t word = SM_ReadAdvance();
    word |= SM_ReadAdvance() << 8;
    return word;
}

static uint16_t SM_Read16(uint16_t address)
{
    uint16_t word = SM_Read(address);
    word |= SM_Read(address) << 8;
    return word;
}

static void SM_Update_NZ(uint8_t val)
{
    SM_SetStatus(val == 0, SM_STATUS_Z);
    SM_SetStatus(val & 0x80, SM_STATUS_N);
}

static void SM_PushStack(uint8_t data)
{
    SM_Write(sm.s, data);
    sm.s--;
}

static uint8_t SM_PopStack(void)
{
    sm.s++;
    return SM_Read(sm.s);
}

static void SM_Opcode_NotImplemented(uint8_t opcode)
{
    SM_ErrorTrap();
}

static void SM_Opcode_SEI(uint8_t opcode) // 78
{
    SM_SetStatus(1, SM_STATUS_I);
}

static void SM_Opcode_CLD(uint8_t opcode) // d8
{
    SM_SetStatus(0, SM_STATUS_D);
}

static void SM_Opcode_CLT(uint8_t opcode) // 12
{
    SM_SetStatus(0, SM_STATUS_T);
}

static void SM_Opcode_LDX(uint8_t opcode) // a2, a6, ae, b6, be
{
    uint8_t val = 0;
    switch (opcode)
    {
        case 0xa2:
            val = SM_ReadAdvance();
            break;
        case 0xa6:
            val = SM_Read(SM_ReadAdvance());
            break;
        case 0xb6:
            val = SM_Read((SM_ReadAdvance() + sm.y) & 0xff);
            break;
        case 0xae:
            val = SM_Read(SM_ReadAdvance16());
            break;
        case 0xbe:
            val = SM_Read(SM_ReadAdvance16() + sm.y);
            break;
    }
    sm.x = val;
    SM_Update_NZ(sm.x);
}

static void SM_Opcode_LDY(uint8_t opcode) // a0, a4, ac, b4, bc
{
    uint8_t val = 0;
    switch (opcode)
    {
        case 0xa0:
            val = SM_ReadAdvance();
            break;
        case 0xa4:
            val = SM_Read(SM_ReadAdvance());
            break;
        case 0xac:
            val = SM_Read(SM_ReadAdvance16());
            break;
        case 0xb4:
            val = SM_Read((SM_ReadAdvance() + sm.x) & 0xff);
            break;
        case 0xbc:
            val = SM_Read(SM_ReadAdvance16() + sm.x);
            break;
    }
    sm.y = val;
    SM_Update_NZ(sm.y);
}

static void SM_Opcode_TXS(uint8_t opcode) // 9a
{
    sm.s = sm.x;
}

static void SM_Opcode_TXA(uint8_t opcode) // 8a
{
    sm.a = sm.x;
    SM_Update_NZ(sm.a);
}

static void SM_Opcode_STA(uint8_t opcode) // 85, 95, 8d, 9d, 99, 81, 91
{
    uint16_t dest = 0;
    switch (opcode)
    {
        case 0x85:
            dest = SM_ReadAdvance();
            break;
        case 0x95:
            dest = SM_ReadAdvance() + sm.x;
            break;
        case 0x8d:
            dest = SM_ReadAdvance16();
            break;
        case 0x9d:
            dest = SM_ReadAdvance16() + sm.x;
            break;
        case 0x99:
            dest = SM_ReadAdvance16() + sm.y;
            break;
        case 0x81:
            dest = SM_Read16((SM_ReadAdvance() + sm.x) & 0xff);
            break;
        case 0x91:
            dest = SM_Read16(SM_ReadAdvance()) + sm.y;
            break;
    }

    SM_Write(dest, sm.a);
}

static void SM_Opcode_INX(uint8_t opcode) // e8
{
    sm.x++;
    SM_Update_NZ(sm.x);
}

static void SM_Opcode_INY(uint8_t opcode) // c8
{
    sm.y++;
    SM_Update_NZ(sm.y);
}

static void SM_Opcode_BBC_BBS(uint8_t opcode)
{
    int32_t zp = (opcode & 4) != 0;
    int32_t bit = (opcode >> 5) & 7;
    int32_t type = (opcode >> 4) & 1;
    uint8_t val = 0;

    if (!zp)
    {
        val = sm.a;
    }
    else
    {
        val = SM_Read(SM_ReadAdvance());
    }

    int8_t diff = SM_ReadAdvance();

    int32_t set = (val >> bit) & 1;

    if (set != type)
        sm.pc += diff;
}

static void SM_Opcode_CPX(uint8_t opcode) // e0, e4, ec
{
    uint8_t operand = 0;
    switch (opcode)
    {
        case 0xe0:
            operand = SM_ReadAdvance();
            break;
        case 0xe4:
            operand = SM_Read(SM_ReadAdvance());
            break;
        case 0xec:
            operand = SM_Read(SM_ReadAdvance16());
            break;
    }
    int diff = sm.x - operand;
    SM_SetStatus((diff & 0x100) == 0, SM_STATUS_C);
    SM_Update_NZ(diff & 0xff);
}

static void SM_Opcode_CPY(uint8_t opcode) // c0, c4, cc
{
    uint8_t operand = 0;
    switch (opcode)
    {
        case 0xc0:
            operand = SM_ReadAdvance();
            break;
        case 0xc4:
            operand = SM_Read(SM_ReadAdvance());
            break;
        case 0xcc:
            operand = SM_Read(SM_ReadAdvance16());
            break;
    }
    int diff = sm.y - operand;
    SM_SetStatus((diff & 0x100) == 0, SM_STATUS_C);
    SM_Update_NZ(diff & 0xff);
}

static void SM_Opcode_BEQ(uint8_t opcode) // f0
{
    int8_t diff = SM_ReadAdvance();
    if ((sm.sr & SM_STATUS_Z) != 0)
        sm.pc += diff;
}

static void SM_Opcode_BCC(uint8_t opcode) // 90
{
    int8_t diff = SM_ReadAdvance();
    if ((sm.sr & SM_STATUS_C) == 0)
        sm.pc += diff;
}

static void SM_Opcode_BCS(uint8_t opcode) // b0
{
    int8_t diff = SM_ReadAdvance();
    if ((sm.sr & SM_STATUS_C) != 0)
        sm.pc += diff;
}

static void SM_Opcode_LDM(uint8_t opcode) // 3c
{
    uint8_t val = SM_ReadAdvance();
    SM_Write(SM_ReadAdvance(), val);
}

static void SM_Opcode_LDA(uint8_t opcode) // a9, a5, b5, ad, bd, b9, a1, b1
{
    uint8_t val = 0;
    switch (opcode)
    {
        case 0xa9:
            val = SM_ReadAdvance();
            break;
        case 0xa5:
            val = SM_Read(SM_ReadAdvance());
            break;
        case 0xb5:
            val = SM_Read((SM_ReadAdvance() + sm.x) & 0xff);
            break;
        case 0xad:
            val = SM_Read(SM_ReadAdvance16());
            break;
        case 0xbd:
            val = SM_Read(SM_ReadAdvance16() + sm.x);
            break;
        case 0xb9:
            val = SM_Read(SM_ReadAdvance16() + sm.y);
            break;
        case 0xa1:
            val = SM_Read(SM_Read16((SM_ReadAdvance() + sm.x) & 0xff));
            break;
        case 0xb1:
            val = SM_Read(SM_Read16(SM_ReadAdvance()) + sm.y);
            break;
    }

    if ((sm.sr & SM_STATUS_T) == 0)
    {
        sm.a = val;
        SM_Update_NZ(val);
    }
    else
    {
        // FIXME
        SM_Write(sm.x, val);
    }
}

static void SM_Opcode_CLI(uint8_t opcode) // 58
{
    SM_SetStatus(0, SM_STATUS_I);
}

static void SM_Opcode_STP(uint8_t opcode) // 42
{
    sm.sleep = 1;
}

static void SM_Opcode_PHA(uint8_t opcode) // 48
{
    SM_PushStack(sm.a);
}

static void SM_Opcode_SEB_CLB(uint8_t opcode)
{
    int32_t zp = (opcode & 4) != 0;
    int32_t bit = (opcode >> 5) & 7;
    int32_t type = (opcode >> 4) & 1;
    uint8_t val = 0;
    uint8_t dest = 0;

    if (!zp)
    {
        val = sm.a;
    }
    else
    {
        dest = SM_ReadAdvance();
        val = SM_Read(dest);
    }

    if (type)
        val &= ~(1 << bit);
    else
        val |= 1 << bit;

    if (!zp)
    {
        sm.a = val;
    }
    else
    {
        SM_Write(dest, val);
    }
}

static void SM_Opcode_RTI(uint8_t opcode) // 40
{
    sm.sr = SM_PopStack();
    sm.pc = SM_PopStack();
    sm.pc |= SM_PopStack() << 8;
}

static void SM_Opcode_PLA(uint8_t opcode) // 68
{
    sm.a = SM_PopStack();
    SM_Update_NZ(sm.a);
}

static void SM_Opcode_BRA(uint8_t opcode) // 80
{
    int8_t disp = SM_ReadAdvance();
    sm.pc += disp;
}

static void SM_Opcode_JSR(uint8_t opcode) // 20, 02, 22
{
    uint16_t newpc = 0;
    switch (opcode)
    {
        case 0x20:
            newpc = SM_ReadAdvance16();
            break;
        case 0x02:
            newpc = SM_Read16(SM_ReadAdvance());
            break;
        case 0x22:
            newpc = 0xff00 | SM_ReadAdvance();
            break;
    }

    SM_PushStack(sm.pc >> 8);
    SM_PushStack(sm.pc & 0xff);
    sm.pc = newpc;
}

static void SM_Opcode_CMP(uint8_t opcode) // c9, c5, d5, cd, dd, d9, c1, d1
{
    uint8_t operand = 0;
    switch (opcode)
    {
        case 0xc9:
            operand = SM_ReadAdvance();
            break;
        case 0xc5:
            operand = SM_Read(SM_ReadAdvance());
            break;
        case 0xd5:
            operand = SM_Read((SM_ReadAdvance()+sm.x)&0xff);
            break;
        case 0xcd:
            operand = SM_Read(SM_ReadAdvance16());
            break;
        case 0xdd:
            operand = SM_Read(SM_ReadAdvance16() + sm.x);
            break;
        case 0xd9:
            operand = SM_Read(SM_ReadAdvance16() + sm.y);
            break;
        case 0xc1:
            operand = SM_Read(SM_Read16((SM_ReadAdvance() + sm.x) & 0xff));
            break;
        case 0xd1:
            operand = SM_Read(SM_Read16(SM_ReadAdvance()) + sm.y);
            break;
    }
    int diff = sm.a - operand;
    SM_SetStatus((diff & 0x100) == 0, SM_STATUS_C);
    SM_Update_NZ(diff & 0xff);
}

static void SM_Opcode_BNE(uint8_t opcode) // d0
{
    int8_t diff = SM_ReadAdvance();
    if ((sm.sr & SM_STATUS_Z) == 0)
        sm.pc += diff;
}

static void SM_Opcode_RTS(uint8_t opcode) // 60
{
    sm.pc = SM_PopStack();
    sm.pc |= SM_PopStack() << 8;
}

static void SM_Opcode_JMP(uint8_t opcode) // 4c, 6c, b2
{
    switch (opcode)
    {
        case 0x4c:
            sm.pc = SM_ReadAdvance16();
            break;
        case 0x6c:
            sm.pc = SM_Read16(SM_ReadAdvance16());
            break;
        case 0xb2:
            sm.pc = SM_Read16(SM_ReadAdvance());
            break;
    }
}

static void SM_Opcode_ORA(uint8_t opcode) // 09, 05, 15, 0d, 1d, 01, 11
{
    uint8_t val = 0;
    uint8_t val2 = 0;

    if ((sm.sr & SM_STATUS_T) == 0)
    {
        val = sm.a;
    }
    else
    {
        // FIXME
        val = SM_Read(sm.x);
    }

    switch (opcode)
    {
        case 0x09:
            val2 = SM_ReadAdvance();
            break;
        case 0x05:
            val2 = SM_Read(SM_ReadAdvance());
            break;
        case 0x15:
            val2 = SM_Read((SM_ReadAdvance() + sm.x) & 0xff);
            break;
        case 0x0d:
            val2 = SM_Read(SM_ReadAdvance16());
            break;
        case 0x1d:
            val2 = SM_Read(SM_ReadAdvance16() + sm.x);
            break;
        case 0x19:
            val2 = SM_Read(SM_ReadAdvance16() + sm.y);
            break;
        case 0x01:
            val2 = SM_Read(SM_Read16((SM_ReadAdvance() + sm.x) & 0xff));
            break;
        case 0x11:
            val2 = SM_Read(SM_Read16(SM_ReadAdvance()) + sm.y);
            break;
    }

    val |= val2;

    if ((sm.sr & SM_STATUS_T) == 0)
    {
        sm.a = val;

        SM_Update_NZ(val);
    }
    else
    {
        // FIXME
        SM_Write(sm.x, val);
    }
}

static void SM_Opcode_DEC(uint8_t opcode) // 1a, c6, d6, ce, de
{
    uint8_t val = 0;
    uint16_t dest = 0;
    switch (opcode)
    {
        case 0x1a:
            sm.a--;
            SM_Update_NZ(sm.a);
            return;
        case 0xc6:
            dest = SM_ReadAdvance();
            break;
        case 0xd6:
            dest = (SM_ReadAdvance() + sm.x) & 0xff;
            break;
        case 0xce:
            dest = SM_ReadAdvance16();
            break;
        case 0xde:
            dest = SM_ReadAdvance16() + sm.x;
            break;
    }
    val = SM_Read(dest);
    val--;
    SM_Write(dest, val);
    SM_Update_NZ(val);
}

static void SM_Opcode_TAX(uint8_t opcode) // aa
{
    sm.x = sm.a;
    SM_Update_NZ(sm.x);
}

static void SM_Opcode_STX(uint8_t opcode) // 86 96 8e
{
    uint16_t dest = 0;
    switch (opcode)
    {
        case 0x86:
            dest = SM_ReadAdvance();
            break;
        case 0x96:
            dest = SM_ReadAdvance() + sm.x;
            break;
        case 0x8e:
            dest = SM_ReadAdvance16();
            break;
    }

    SM_Write(dest, sm.x);
}

static void SM_Opcode_STY(uint8_t opcode) // 84 8c 94
{
    uint16_t dest = 0;
    switch (opcode)
    {
        case 0x84:
            dest = SM_ReadAdvance();
            break;
        case 0x94:
            dest = (SM_ReadAdvance() + sm.x) & 0xff;
            break;
        case 0x8c:
            dest = SM_ReadAdvance16();
            break;
    }

    SM_Write(dest, sm.y);
}

static void SM_Opcode_SEC(uint8_t opcode) // 38
{
    SM_SetStatus(1, SM_STATUS_C);
}

static void SM_Opcode_NOP(uint8_t opcode) // EA
{
}

static void SM_Opcode_BPL(uint8_t opcode) // 10
{
    int8_t diff = SM_ReadAdvance();
    if ((sm.sr & SM_STATUS_N) == 0)
        sm.pc += diff;
}

static void SM_Opcode_CLC(uint8_t opcode) // 18
{
    SM_SetStatus(0, SM_STATUS_C);
}

static void SM_Opcode_AND(uint8_t opcode) // 29, 25, 35, 2d, 3d, 21, 31
{
    uint8_t val = 0;
    uint8_t val2 = 0;

    if ((sm.sr & SM_STATUS_T) == 0)
    {
        val = sm.a;
    }
    else
    {
        // FIXME
        val = SM_Read(sm.x);
    }

    switch (opcode)
    {
        case 0x29:
            val2 = SM_ReadAdvance();
            break;
        case 0x25:
            val2 = SM_Read(SM_ReadAdvance());
            break;
        case 0x35:
            val2 = SM_Read((SM_ReadAdvance() + sm.x) & 0xff);
            break;
        case 0x2d:
            val2 = SM_Read(SM_ReadAdvance16());
            break;
        case 0x3d:
            val2 = SM_Read(SM_ReadAdvance16() + sm.x);
            break;
        case 0x39:
            val2 = SM_Read(SM_ReadAdvance16() + sm.y);
            break;
        case 0x21:
            val2 = SM_Read(SM_Read16((SM_ReadAdvance() + sm.x) & 0xff));
            break;
        case 0x31:
            val2 = SM_Read(SM_Read16(SM_ReadAdvance()) + sm.y);
            break;
    }

    val &= val2;

    if ((sm.sr & SM_STATUS_T) == 0)
    {
        sm.a = val;

        SM_Update_NZ(val);
    }
    else
    {
        // FIXME
        SM_Write(sm.x, val);
    }
}

static void SM_Opcode_INC(uint8_t opcode) // 3a, e6, f6, ee, fe
{
    uint8_t val = 0;
    uint16_t dest = 0;
    switch (opcode)
    {
        case 0x3a:
            sm.a++;
            SM_Update_NZ(sm.a);
            return;
        case 0xe6:
            dest = SM_ReadAdvance();
            break;
        case 0xf6:
            dest = (SM_ReadAdvance() + sm.x) & 0xff;
            break;
        case 0xee:
            dest = SM_ReadAdvance16();
            break;
        case 0xfe:
            dest = SM_ReadAdvance16() + sm.x;
            break;
    }
    val = SM_Read(dest);
    val++;
    SM_Write(dest, val);
    SM_Update_NZ(val);
}

static void (*SM_Opcode_Table[256])(uint8_t opcode)
{
    SM_Opcode_NotImplemented, // 00
    SM_Opcode_ORA, // 01
    SM_Opcode_JSR, // 02
    SM_Opcode_BBC_BBS, // 03
    SM_Opcode_NotImplemented, // 04
    SM_Opcode_ORA, // 05
    SM_Opcode_NotImplemented, // 06
    SM_Opcode_BBC_BBS, // 07
    SM_Opcode_NotImplemented, // 08
    SM_Opcode_ORA, // 09
    SM_Opcode_NotImplemented, // 0a
    SM_Opcode_SEB_CLB, // 0b
    SM_Opcode_NotImplemented, // 0c
    SM_Opcode_ORA, // 0d
    SM_Opcode_NotImplemented, // 0e
    SM_Opcode_SEB_CLB, // 0f
    SM_Opcode_BPL, // 10
    SM_Opcode_ORA, // 11
    SM_Opcode_CLT, // 12
    SM_Opcode_BBC_BBS, // 13
    SM_Opcode_NotImplemented, // 14
    SM_Opcode_ORA, // 15
    SM_Opcode_NotImplemented, // 16
    SM_Opcode_BBC_BBS, // 17
    SM_Opcode_CLC, // 18
    SM_Opcode_ORA, // 19
    SM_Opcode_DEC, // 1a
    SM_Opcode_SEB_CLB, // 1b
    SM_Opcode_NotImplemented, // 1c
    SM_Opcode_ORA, // 1d
    SM_Opcode_NotImplemented, // 1e
    SM_Opcode_SEB_CLB, // 1f
    SM_Opcode_JSR, // 20
    SM_Opcode_AND, // 21
    SM_Opcode_JSR, // 22
    SM_Opcode_BBC_BBS, // 23
    SM_Opcode_NotImplemented, // 24
    SM_Opcode_AND, // 25
    SM_Opcode_NotImplemented, // 26
    SM_Opcode_BBC_BBS, // 27
    SM_Opcode_NotImplemented, // 28
    SM_Opcode_AND, // 29
    SM_Opcode_NotImplemented, // 2a
    SM_Opcode_SEB_CLB, // 2b
    SM_Opcode_NotImplemented, // 2c
    SM_Opcode_AND, // 2d
    SM_Opcode_NotImplemented, // 2e
    SM_Opcode_SEB_CLB, // 2f
    SM_Opcode_NotImplemented, // 30
    SM_Opcode_AND, // 31
    SM_Opcode_NotImplemented, // 32
    SM_Opcode_BBC_BBS, // 33
    SM_Opcode_NotImplemented, // 34
    SM_Opcode_AND, // 35
    SM_Opcode_NotImplemented, // 36
    SM_Opcode_BBC_BBS, // 37
    SM_Opcode_SEC, // 38
    SM_Opcode_AND, // 39
    SM_Opcode_INC, // 3a
    SM_Opcode_SEB_CLB, // 3b
    SM_Opcode_LDM, // 3c
    SM_Opcode_AND, // 3d
    SM_Opcode_NotImplemented, // 3e
    SM_Opcode_SEB_CLB, // 3f
    SM_Opcode_RTI, // 40
    SM_Opcode_NotImplemented, // 41
    SM_Opcode_STP, // 42
    SM_Opcode_BBC_BBS, // 43
    SM_Opcode_NotImplemented, // 44
    SM_Opcode_NotImplemented, // 45
    SM_Opcode_NotImplemented, // 46
    SM_Opcode_BBC_BBS, // 47
    SM_Opcode_PHA, // 48
    SM_Opcode_NotImplemented, // 49
    SM_Opcode_NotImplemented, // 4a
    SM_Opcode_SEB_CLB, // 4b
    SM_Opcode_JMP, // 4c
    SM_Opcode_NotImplemented, // 4d
    SM_Opcode_NotImplemented, // 4e
    SM_Opcode_SEB_CLB, // 4f
    SM_Opcode_NotImplemented, // 50
    SM_Opcode_NotImplemented, // 51
    SM_Opcode_NotImplemented, // 52
    SM_Opcode_BBC_BBS, // 53
    SM_Opcode_NotImplemented, // 54
    SM_Opcode_NotImplemented, // 55
    SM_Opcode_NotImplemented, // 56
    SM_Opcode_BBC_BBS, // 57
    SM_Opcode_CLI, // 58
    SM_Opcode_NotImplemented, // 59
    SM_Opcode_NotImplemented, // 5a
    SM_Opcode_SEB_CLB, // 5b
    SM_Opcode_NotImplemented, // 5c
    SM_Opcode_NotImplemented, // 5d
    SM_Opcode_NotImplemented, // 5e
    SM_Opcode_SEB_CLB, // 5f
    SM_Opcode_RTS, // 60
    SM_Opcode_NotImplemented, // 61
    SM_Opcode_NotImplemented, // 62
    SM_Opcode_BBC_BBS, // 63
    SM_Opcode_NotImplemented, // 64
    SM_Opcode_NotImplemented, // 65
    SM_Opcode_NotImplemented, // 66
    SM_Opcode_BBC_BBS, // 67
    SM_Opcode_PLA, // 68
    SM_Opcode_NotImplemented, // 69
    SM_Opcode_NotImplemented, // 6a
    SM_Opcode_SEB_CLB, // 6b
    SM_Opcode_JMP, // 6c
    SM_Opcode_NotImplemented, // 6d
    SM_Opcode_NotImplemented, // 6e
    SM_Opcode_SEB_CLB, // 6f
    SM_Opcode_NotImplemented, // 70
    SM_Opcode_NotImplemented, // 71
    SM_Opcode_NotImplemented, // 72
    SM_Opcode_BBC_BBS, // 73
    SM_Opcode_NotImplemented, // 74
    SM_Opcode_NotImplemented, // 75
    SM_Opcode_NotImplemented, // 76
    SM_Opcode_BBC_BBS, // 77
    SM_Opcode_SEI, // 78
    SM_Opcode_NotImplemented, // 79
    SM_Opcode_NotImplemented, // 7a
    SM_Opcode_SEB_CLB, // 7b
    SM_Opcode_NotImplemented, // 7c
    SM_Opcode_NotImplemented, // 7d
    SM_Opcode_NotImplemented, // 7e
    SM_Opcode_SEB_CLB, // 7f
    SM_Opcode_BRA, // 80
    SM_Opcode_STA, // 81
    SM_Opcode_NotImplemented, // 82
    SM_Opcode_BBC_BBS, // 83
    SM_Opcode_STY, // 84
    SM_Opcode_STA, // 85
    SM_Opcode_STX, // 86
    SM_Opcode_BBC_BBS, // 87
    SM_Opcode_NotImplemented, // 88
    SM_Opcode_NotImplemented, // 89
    SM_Opcode_TXA, // 8a
    SM_Opcode_SEB_CLB, // 8b
    SM_Opcode_STY, // 8c
    SM_Opcode_STA, // 8d
    SM_Opcode_STX, // 8e
    SM_Opcode_SEB_CLB, // 8f
    SM_Opcode_BCC, // 90
    SM_Opcode_STA, // 91
    SM_Opcode_NotImplemented, // 92
    SM_Opcode_BBC_BBS, // 93
    SM_Opcode_STY, // 94
    SM_Opcode_STA, // 95
    SM_Opcode_STX, // 96
    SM_Opcode_BBC_BBS, // 97
    SM_Opcode_NotImplemented, // 98
    SM_Opcode_STA, // 99
    SM_Opcode_TXS, // 9a
    SM_Opcode_SEB_CLB, // 9b
    SM_Opcode_NotImplemented, // 9c
    SM_Opcode_STA, // 9d
    SM_Opcode_NotImplemented, // 9e
    SM_Opcode_SEB_CLB, // 9f
    SM_Opcode_LDY, // a0
    SM_Opcode_LDA, // a1
    SM_Opcode_LDX, // a2
    SM_Opcode_BBC_BBS, // a3
    SM_Opcode_LDY, // a4
    SM_Opcode_LDA, // a5
    SM_Opcode_LDX, // a6
    SM_Opcode_BBC_BBS, // a7
    SM_Opcode_NotImplemented, // a8
    SM_Opcode_LDA, // a9
    SM_Opcode_TAX, // aa
    SM_Opcode_SEB_CLB, // ab
    SM_Opcode_LDY, // ac
    SM_Opcode_LDA, // ad
    SM_Opcode_LDX, // ae
    SM_Opcode_SEB_CLB, // af
    SM_Opcode_BCS, // b0
    SM_Opcode_LDA, // b1
    SM_Opcode_JMP, // b2
    SM_Opcode_BBC_BBS, // b3
    SM_Opcode_LDY, // b4
    SM_Opcode_LDA, // b5
    SM_Opcode_LDX, // b6
    SM_Opcode_BBC_BBS, // b7
    SM_Opcode_NotImplemented, // b8
    SM_Opcode_LDA, // b9
    SM_Opcode_NotImplemented, // ba
    SM_Opcode_SEB_CLB, // bb
    SM_Opcode_LDY, // bc
    SM_Opcode_LDA, // bd
    SM_Opcode_LDX, // be
    SM_Opcode_SEB_CLB, // bf
    SM_Opcode_CPY, // c0
    SM_Opcode_CMP, // c1
    SM_Opcode_NotImplemented, // c2
    SM_Opcode_BBC_BBS, // c3
    SM_Opcode_CPY, // c4
    SM_Opcode_CMP, // c5
    SM_Opcode_DEC, // c6
    SM_Opcode_BBC_BBS, // c7
    SM_Opcode_INY, // c8
    SM_Opcode_CMP, // c9
    SM_Opcode_NotImplemented, // ca
    SM_Opcode_SEB_CLB, // cb
    SM_Opcode_CPY, // cc
    SM_Opcode_CMP, // cd
    SM_Opcode_DEC, // ce
    SM_Opcode_SEB_CLB, // cf
    SM_Opcode_BNE, // d0
    SM_Opcode_CMP, // d1
    SM_Opcode_NotImplemented, // d2
    SM_Opcode_BBC_BBS, // d3
    SM_Opcode_NotImplemented, // d4
    SM_Opcode_CMP, // d5
    SM_Opcode_DEC, // d6
    SM_Opcode_BBC_BBS, // d7
    SM_Opcode_CLD, // d8
    SM_Opcode_CMP, // d9
    SM_Opcode_NotImplemented, // da
    SM_Opcode_SEB_CLB, // db
    SM_Opcode_NotImplemented, // dc
    SM_Opcode_CMP, // dd
    SM_Opcode_DEC, // de
    SM_Opcode_SEB_CLB, // df
    SM_Opcode_CPX, // e0
    SM_Opcode_NotImplemented, // e1
    SM_Opcode_NotImplemented, // e2
    SM_Opcode_BBC_BBS, // e3
    SM_Opcode_CPX, // e4
    SM_Opcode_NotImplemented, // e5
    SM_Opcode_INC, // e6
    SM_Opcode_BBC_BBS, // e7
    SM_Opcode_INX, // e8
    SM_Opcode_NotImplemented, // e9
    SM_Opcode_NOP, // ea
    SM_Opcode_SEB_CLB, // eb
    SM_Opcode_CPX, // ec
    SM_Opcode_NotImplemented, // ed
    SM_Opcode_INC, // ee
    SM_Opcode_SEB_CLB, // ef
    SM_Opcode_BEQ, // f0
    SM_Opcode_NotImplemented, // f1
    SM_Opcode_NotImplemented, // f2
    SM_Opcode_BBC_BBS, // f3
    SM_Opcode_NotImplemented, // f4
    SM_Opcode_NotImplemented, // f5
    SM_Opcode_INC, // f6
    SM_Opcode_BBC_BBS, // f7
    SM_Opcode_NotImplemented, // f8
    SM_Opcode_NotImplemented, // f9
    SM_Opcode_NotImplemented, // fa
    SM_Opcode_SEB_CLB, // fb
    SM_Opcode_NotImplemented, // fc
    SM_Opcode_NotImplemented, // fd
    SM_Opcode_INC, // fe
    SM_Opcode_SEB_CLB, // ff
};

static void SM_StartVector(uint32_t vector)
{
    SM_PushStack(sm.pc >> 8);
    SM_PushStack(sm.pc & 0xff);
    SM_PushStack(sm.sr);

    sm.sr |= SM_STATUS_I;
    sm.sleep = 0;

    sm.pc = SM_GetVectorAddress(vector);
}

static void SM_HandleInterrupt(void)
{
    if (sm.sr & SM_STATUS_I)
        return;

    if ((sm_device_mode[SM_DEV_UART1_CTRL] & 0x8) != 0
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x80) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x80) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x80;
        SM_StartVector(SM_VECTOR_UART1_RX);
        return;
    }
    if ((sm_device_mode[SM_DEV_UART2_CTRL] & 0x8) != 0
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x40) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x40) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x40;
        SM_StartVector(SM_VECTOR_UART2_RX);
        return;
    }
    if ((sm_device_mode[SM_DEV_UART3_CTRL] & 0x8) != 0
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x20) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x20) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x20;
        SM_StartVector(SM_VECTOR_UART3_RX);
        return;
    }
    if ((sm_device_mode[SM_DEV_TIMER_CTRL] & 0x80) != 0
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x10) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x10) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x10;
        SM_StartVector(SM_VECTOR_IPCM0);
        return;
    }
    if ((sm_device_mode[SM_DEV_TIMER_CTRL] & 0x40) != 0
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x8) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x8) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x8;
        SM_StartVector(SM_VECTOR_TIMER_X);
        return;
    }
    if ((sm_device_mode[SM_DEV_COLLISION] & 0xc0) == 0xc0)
    {
        sm_device_mode[SM_DEV_COLLISION] &= ~0x80;
        SM_StartVector(SM_VECTOR_COLLISION);
        return;
    }
    if (((sm_device_mode[SM_DEV_UART1_CTRL] & 0x10) == 0
        || (sm_cts & 1) != 0)
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x4) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x4) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x4;
        SM_StartVector(SM_VECTOR_UART1_TX);
        return;
    }
    if (((sm_device_mode[SM_DEV_UART2_CTRL] & 0x10) == 0
        || (sm_cts & 2) != 0)
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x2) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x2) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x2;
        SM_StartVector(SM_VECTOR_UART2_TX);
        return;
    }
    if (((sm_device_mode[SM_DEV_UART3_CTRL] & 0x10) == 0
        || (sm_cts & 4) != 0)
        && (sm_device_mode[SM_DEV_INT_ENABLE] & 0x1) != 0
        && (sm_device_mode[SM_DEV_INT_REQUEST] & 0x1) != 0)
    {
        sm_device_mode[SM_DEV_INT_REQUEST] &= ~0x1;
        SM_StartVector(SM_VECTOR_UART3_TX);
        return;
    }
}

static void SM_UpdateTimer(void)
{
    // Simplified code with early returns
    // SM_UpdateTimer is currently guaranteed to be called every 48 cycles (which equals 3 timer ticks)
    if ((sm_device_mode[SM_DEV_TIMER_CTRL] & 0x20) || sm.sleep) return;
    if (sm_timer_prescaler >= 3) { sm_timer_prescaler -= 3; return; }
    for (uint8_t i = 3; i; i--)
    {
        if (sm_timer_prescaler >= i) { sm_timer_prescaler -= i; return; }
        i -= sm_timer_prescaler;
        sm_timer_prescaler = sm_device_mode[SM_DEV_PRESCALER];
        if (sm_timer_counter) { sm_timer_counter--; continue; }
        sm_timer_counter = sm_device_mode[SM_DEV_TIMER];
        sm_device_mode[SM_DEV_INT_REQUEST] |= 0x8;
    }
}

static void SM_UpdateUART(void)
{
    if ((sm_device_mode[SM_DEV_UART1_CTRL] & 4) == 0) // RX disabled
        return;
    if (uart_write_ptr == uart_read_ptr) // no byte
        return;

    if (sm_uart_rx_gotbyte)
        return;

    if (sm.cycles < sm_uart_rx_delay)
        return;

    sm_uart_rx_byte = uart_buffer[uart_read_ptr];
    uart_read_ptr = (uart_read_ptr + 1) % uart_buffer_size;
    sm_uart_rx_gotbyte = 1;
    sm_device_mode[SM_DEV_INT_REQUEST] |= 0x40;

    sm_uart_rx_delay = sm.cycles + 3000 * 4;
}

static void SM_Update(uint64_t cycles)
{
    while (sm.cycles < cycles * 5)
    {
        SM_HandleInterrupt();

        if (!sm.sleep)
        {
            uint8_t opcode = SM_ReadAdvance();

            SM_Opcode_Table[opcode](opcode);
        }

        sm.cycles += 12 * 4; // FIXME

        SM_UpdateTimer();
        SM_UpdateUART();
    }
}
#endif // NUKEDSC55_SUBMCU_CPP

#ifndef NUKEDSC55_MCU_CPP
#define NUKEDSC55_MCU_CPP
static uint8_t RCU_Read(void)
{
    return 0;
}

enum {
    ANALOG_LEVEL_RCU_LOW = 0,
    ANALOG_LEVEL_RCU_HIGH = 0,
    ANALOG_LEVEL_SW_0 = 0,
    ANALOG_LEVEL_SW_1 = 0x155,
    ANALOG_LEVEL_SW_2 = 0x2aa,
    ANALOG_LEVEL_SW_3 = 0x3ff,
    ANALOG_LEVEL_BATTERY = 0x2a0,
};

static uint16_t MCU_SC155Sliders(uint32_t index)
{
    // 0 - 1/9
    // 1 - 2/10
    // 2 - 3/11
    // 3 - 4/12
    // 4 - 5/13
    // 5 - 6/14
    // 6 - 7/15
    // 7 - 8/16
    // 8 - ALL
    return 0x0;
}

static uint16_t MCU_AnalogReadPin(uint32_t pin)
{
    if (mcu_cm300)
        return 0;
    if (mcu_jv880)
    {
        if (pin == 1)
            return ANALOG_LEVEL_BATTERY;
        return 0x3ff;
    }
    if (0)
    {
READ_RCU:
        uint8_t rcu = RCU_Read();
        if (rcu & (1 << pin))
            return ANALOG_LEVEL_RCU_HIGH;
        else
            return ANALOG_LEVEL_RCU_LOW;
    }
    if (mcu_mk1)
    {
        if (mcu_sc155 && (dev_register[DEV_P9DR] & 1) != 0)
        {
            return MCU_SC155Sliders(pin);
        }
        if (pin == 7)
        {
            if (mcu_sc155 && (dev_register[DEV_P9DR] & 2) != 0)
                return MCU_SC155Sliders(8);
            else
                return ANALOG_LEVEL_BATTERY;
        }
        else
            goto READ_RCU;
    }
    else
    {
        if (mcu_sc155 && (io_sd & 16) != 0)
        {
            return MCU_SC155Sliders(pin);
        }
        if (pin == 7)
        {
            if (mcu_mk1)
                return ANALOG_LEVEL_BATTERY;
            switch ((io_sd >> 2) & 3)
            {
            case 0: // Battery voltage
                return ANALOG_LEVEL_BATTERY;
            case 1: // NC
                if (mcu_sc155)
                    return MCU_SC155Sliders(8);
                return 0;
            case 2: // SW
                // Originally there was a sw_pos variable to choose beween 0 to 3 but it was fixed at 3
                return ANALOG_LEVEL_SW_3;
            case 3: // RCU
                goto READ_RCU;
            }
        }
        else
            goto READ_RCU;
    }
    return 0; // should not hit
}

static void MCU_AnalogSample(int channel)
{
    int value = MCU_AnalogReadPin(channel);
    int dest = (channel << 1) & 6;
    dev_register[DEV_ADDRAH + dest] = value >> 2;
    dev_register[DEV_ADDRAL + dest] = (value << 6) & 0xc0;
}

static void MCU_DeviceWrite(uint32_t address, uint8_t data)
{
    address &= 0x7f;
    if (address >= 0x10 && address < 0x40)
    {
        TIMER_Write(address, data);
        return;
    }
    if (address >= 0x50 && address < 0x55)
    {
        TIMER2_Write(address, data);
        return;
    }
    switch (address)
    {
    case DEV_P1DDR: // P1DDR
        break;
    case DEV_P5DDR:
        break;
    case DEV_P6DDR:
        break;
    case DEV_P7DDR:
        break;
    case DEV_SCR:
        break;
    case DEV_WCR:
        break;
    case DEV_P9DDR:
        break;
    case DEV_RAME: // RAME
        break;
    case DEV_P1CR: // P1CR
        break;
    case DEV_DTEA:
        break;
    case DEV_DTEB:
        break;
    case DEV_DTEC:
        break;
    case DEV_DTED:
        break;
    case DEV_SMR:
        break;
    case DEV_BRR:
        break;
    case DEV_IPRA:
        break;
    case DEV_IPRB:
        break;
    case DEV_IPRC:
        break;
    case DEV_IPRD:
        break;
    case DEV_PWM1_DTR:
        break;
    case DEV_PWM1_TCR:
        break;
    case DEV_PWM2_DTR:
        break;
    case DEV_PWM2_TCR:
        break;
    case DEV_PWM3_DTR:
        break;
    case DEV_PWM3_TCR:
        break;
    case DEV_P7DR:
        break;
    case DEV_TMR_TCNT:
        break;
    case DEV_TMR_TCR:
        break;
    case DEV_TMR_TCSR:
        break;
    case DEV_TMR_TCORA:
        break;
    case DEV_TDR:
        break;
    case DEV_ADCSR:
    {
        dev_register[address] &= ~0x7f;
        dev_register[address] |= data & 0x7f;
        if ((data & 0x80) == 0 && adf_rd)
        {
            dev_register[address] &= ~0x80;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_ANALOG, 0);
        }
        if ((data & 0x40) == 0)
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_ANALOG, 0);
        return;
    }
    case DEV_SSR:
    {
        if ((data & 0x80) == 0 && (ssr_rd & 0x80) != 0)
        {
            dev_register[address] &= ~0x80;
            mcu_uart_tx_delay = mcu.cycles + 3000;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_UART_TX, 0);
        }
        if ((data & 0x40) == 0 && (ssr_rd & 0x40) != 0)
        {
            mcu_uart_rx_delay = mcu.cycles + 3000;
            dev_register[address] &= ~0x40;
            MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_UART_RX, 0);
        }
        if ((data & 0x20) == 0 && (ssr_rd & 0x20) != 0)
        {
            dev_register[address] &= ~0x20;
        }
        if ((data & 0x10) == 0 && (ssr_rd & 0x10) != 0)
        {
            dev_register[address] &= ~0x10;
        }
        break;
    }
    default:
        address += 0;
        break;
    }
    dev_register[address] = data;
}

static uint8_t MCU_DeviceRead(uint32_t address)
{
    address &= 0x7f;
    if (address >= 0x10 && address < 0x40)
    {
        return TIMER_Read(address);
    }
    if (address >= 0x50 && address < 0x55)
    {
        return TIMER_Read2(address);
    }
    switch (address)
    {
    case DEV_ADDRAH:
    case DEV_ADDRAL:
    case DEV_ADDRBH:
    case DEV_ADDRBL:
    case DEV_ADDRCH:
    case DEV_ADDRCL:
    case DEV_ADDRDH:
    case DEV_ADDRDL:
        return dev_register[address];
    case DEV_ADCSR:
        adf_rd = (dev_register[address] & 0x80) != 0;
        return dev_register[address];
    case DEV_SSR:
        ssr_rd = dev_register[address];
        return dev_register[address];
    case DEV_RDR:
        return mcu_uart_rx_byte;
    case 0x00:
        return 0xff;
    case DEV_P7DR:
    {
        if (!mcu_jv880) return 0xff;

        uint8_t data = 0xff;
        uint32_t button_pressed = 0; // DBP: REMOVED: (uint32_t)SDL_AtomicGet(&mcu_button_pressed);

        if (io_sd == 0xFB)
            data &= ((button_pressed >> 0) & 0x1F) ^ 0xFF;
        if (io_sd == 0xF7)
            data &= ((button_pressed >> 5) & 0x1F) ^ 0xFF;
        if (io_sd == 0xEF)
            data &= ((button_pressed >> 10) & 0xF) ^ 0xFF;

        data |= 0x80;
        return data;
    }
    case DEV_P9DR:
    {
        int cfg = 0;
        if (!mcu_mk1)
            cfg = mcu_sc155 ? 0 : 2; // bit 1: 0 - SC-155mk2 (???), 1 - SC-55mk2

        int dir = dev_register[DEV_P9DDR];

        int val = cfg & (dir ^ 0xff);
        val |= dev_register[DEV_P9DR] & dir;
        return val;
    }
    case DEV_SCR:
    case DEV_TDR:
    case DEV_SMR:
        return dev_register[address];
    case DEV_IPRC:
    case DEV_IPRD:
    case DEV_DTEC:
    case DEV_DTED:
    case DEV_FRT2_TCSR:
    case DEV_FRT1_TCSR:
    case DEV_FRT1_TCR:
    case DEV_FRT1_FRCH:
    case DEV_FRT1_FRCL:
    case DEV_FRT3_TCSR:
    case DEV_FRT3_OCRAH:
    case DEV_FRT3_OCRAL:
        return dev_register[address];
    }
    return dev_register[address];
}

static void MCU_UpdateAnalog(uint64_t cycles)
{
    int ctrl = dev_register[DEV_ADCSR];
    int isscan = (ctrl & 16) != 0;

    if (ctrl & 0x20)
    {
        if (analog_end_time == 0)
            analog_end_time = cycles + 200;
        else if (analog_end_time < cycles)
        {
            if (isscan)
            {
                int base = ctrl & 4;
                for (int i = 0; i <= (ctrl & 3); i++)
                    MCU_AnalogSample(base + i);
                analog_end_time = cycles + 200;
            }
            else
            {
                MCU_AnalogSample(ctrl & 7);
                dev_register[DEV_ADCSR] &= ~0x20;
                analog_end_time = 0;
            }
            dev_register[DEV_ADCSR] |= 0x80;
            if (ctrl & 0x40)
                MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_ANALOG, 1);
        }
    }
    else
        analog_end_time = 0;
}

static uint8_t MCU_Read(uint32_t address)
{
    uint32_t address_rom = address & 0x3ffff;
    if (address & 0x80000 && !mcu_jv880)
        address_rom |= 0x40000;
    uint8_t page = (address >> 16) & 0xf;
    address &= 0xffff;
    uint8_t ret = 0xff;
    switch (page)
    {
    case 0:
        if (!(address & 0x8000))
            ret = rom1[address & 0x7fff];
        else
        {
            if (!mcu_mk1)
            {
                uint16_t base = mcu_jv880 ? 0xf000 : 0xe000;
                if (address >= base && address < (uint32_t)(base | 0x400))
                {
                    ret = PCM_Read(address & 0x3f);
                }
                else if (!mcu_scb55 && address >= 0xec00 && address < 0xf000)
                {
                    ret = SM_SysRead(address & 0xff);
                }
                else if (address >= 0xff80)
                {
                    ret = MCU_DeviceRead(address & 0x7f);
                }
                else if (address >= 0xfb80 && address < 0xff80
                    && (dev_register[DEV_RAME] & 0x80) != 0)
                    ret = ram[(address - 0xfb80) & 0x3ff];
                else if (address >= 0x8000 && address < 0xe000)
                {
                    ret = sram[address & 0x7fff];
                }
                else if (address == (base | 0x402))
                {
                    ret = ga_int_trigger;
                    ga_int_trigger = 0;
                    MCU_Interrupt_SetRequest(mcu_jv880 ? INTERRUPT_SOURCE_IRQ0 : INTERRUPT_SOURCE_IRQ1, 0);
                }
                else
                {
                    printf("Unknown read %x\n", address);
                    ret = 0xff;
                }
                //
                // e402:2-0 irq source
                //
            }
            else
            {
                if (address >= 0xe000 && address < 0xe040)
                {
                    ret = PCM_Read(address & 0x3f);
                }
                else if (address >= 0xff80)
                {
                    ret = MCU_DeviceRead(address & 0x7f);
                }
                else if (address >= 0xfb80 && address < 0xff80
                    && (dev_register[DEV_RAME] & 0x80) != 0)
                {
                    ret = ram[(address - 0xfb80) & 0x3ff];
                }
                else if (address >= 0x8000 && address < 0xe000)
                {
                    ret = sram[address & 0x7fff];
                }
                else if (address >= 0xf000 && address < 0xf100)
                {
                    io_sd = address & 0xff;

                    if (mcu_cm300)
                        return 0xff;

                    LCD_Enable((io_sd & 8) != 0);

                    uint8_t data = 0xff;
                    uint32_t button_pressed = 0; // DBP: REMOVED: (uint32_t)SDL_AtomicGet(&mcu_button_pressed);

                    if ((io_sd & 1) == 0)
                        data &= ((button_pressed >> 0) & 255) ^ 255;
                    if ((io_sd & 2) == 0)
                        data &= ((button_pressed >> 8) & 255) ^ 255;
                    if ((io_sd & 4) == 0)
                        data &= ((button_pressed >> 16) & 255) ^ 255;
                    if ((io_sd & 8) == 0)
                        data &= ((button_pressed >> 24) & 255) ^ 255;
                    return data;
                }
                else if (address == 0xf106)
                {
                    ret = ga_int_trigger;
                    ga_int_trigger = 0;
                    MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_IRQ1, 0);
                }
                else
                {
                    printf("Unknown read %x\n", address);
                    ret = 0xff;
                }
                //
                // f106:2-0 irq source
                //
            }
        }
        break;
#if 0
    case 3:
        ret = rom2[address | 0x30000];
        break;
    case 4:
        ret = rom2[address];
        break;
    case 10:
        ret = rom2[address | 0x60000]; // FIXME
        break;
    case 1:
        ret = rom2[address | 0x10000];
        break;
#endif
    case 1:
        ret = rom2[address_rom & rom2_mask];
        break;
    case 2:
        ret = rom2[address_rom & rom2_mask];
        break;
    case 3:
        ret = rom2[address_rom & rom2_mask];
        break;
    case 4:
        ret = rom2[address_rom & rom2_mask];
        break;
    case 8:
        if (!mcu_jv880)
            ret = rom2[address_rom & rom2_mask];
        else
            ret = 0xff;
        break;
    case 9:
        if (!mcu_jv880)
            ret = rom2[address_rom & rom2_mask];
        else
            ret = 0xff;
        break;
    case 14:
    case 15:
        if (!mcu_jv880)
            ret = rom2[address_rom & rom2_mask];
        else
            ret = cardram[address & 0x7fff]; // FIXME
        break;
    case 10:
    case 11:
        if (!mcu_mk1)
            ret = sram[address & 0x7fff]; // FIXME
        else
            ret = 0xff;
        break;
    case 12:
    case 13:
        if (mcu_jv880)
            ret = nvram[address & 0x7fff]; // FIXME
        else
            ret = 0xff;
        break;
    case 5:
        if (mcu_mk1)
            ret = sram[address & 0x7fff]; // FIXME
        else
            ret = 0xff;
        break;
    default:
        ret = 0x00;
        break;
    }
    return ret;
}

static uint16_t MCU_Read16(uint32_t address)
{
    address &= ~1;
    uint8_t b0, b1;
    b0 = MCU_Read(address);
    b1 = MCU_Read(address+1);
    return (b0 << 8) + b1;
}

static uint32_t MCU_Read32(uint32_t address)
{
    address &= ~3;
    uint8_t b0, b1, b2, b3;
    b0 = MCU_Read(address);
    b1 = MCU_Read(address+1);
    b2 = MCU_Read(address+2);
    b3 = MCU_Read(address+3);
    return (b0 << 24) + (b1 << 16) + (b2 << 8) + b3;
}

static void MCU_Write(uint32_t address, uint8_t value)
{
    uint8_t page = (address >> 16) & 0xf;
    address &= 0xffff;
    if (page == 0)
    {
        if (address & 0x8000)
        {
            if (!mcu_mk1)
            {
                uint16_t base = mcu_jv880 ? 0xf000 : 0xe000;
                if (address >= (uint32_t)(base | 0x400) && address < (uint32_t)(base | 0x800))
                {
                    if (address == (base | 0x404) || address == (uint32_t)(base | 0x405))
                        LCD_Write(address & 1, value);
                    else if (address == (base | 0x401))
                    {
                        io_sd = value;
                        LCD_Enable((value & 1) == 0);
                    }
                    else if (address == (base | 0x402))
                        ga_int_enable = (uint8_t)(value << 1);
                    else
                        printf("Unknown write %x %x\n", address, value);
                    //
                    // e400: always 4?
                    // e401: SC0-6?
                    // e402: enable/disable IRQ?
                    // e403: always 1?
                    // e404: LCD
                    // e405: LCD
                    // e406: 0 or 40
                    // e407: 0, e406 continuation?
                    //
                }
                else if (address >= (uint32_t)(base | 0x000) && address < (uint32_t)(base | 0x400))
                {
                    PCM_Write(address & 0x3f, value);
                }
                else if (!mcu_scb55 && address >= 0xec00 && address < 0xf000)
                {
                    SM_SysWrite(address & 0xff, value);
                }
                else if (address >= 0xff80)
                {
                    MCU_DeviceWrite(address & 0x7f, value);
                }
                else if (address >= 0xfb80 && address < 0xff80
                    && (dev_register[DEV_RAME] & 0x80) != 0)
                {
                    ram[(address - 0xfb80) & 0x3ff] = value;
                }
                else if (address >= 0x8000 && address < 0xe000)
                {
                    sram[address & 0x7fff] = value;
                }
                else
                {
                    printf("Unknown write %x %x\n", address, value);
                }
            }
            else
            {
                if (address >= 0xe000 && address < 0xe040)
                {
                    PCM_Write(address & 0x3f, value);
                }
                else if (address >= 0xff80)
                {
                    MCU_DeviceWrite(address & 0x7f, value);
                }
                else if (address >= 0xfb80 && address < 0xff80
                    && (dev_register[DEV_RAME] & 0x80) != 0)
                {
                    ram[(address - 0xfb80) & 0x3ff] = value;
                }
                else if (address >= 0x8000 && address < 0xe000)
                {
                    sram[address & 0x7fff] = value;
                }
                else if (address >= 0xf000 && address < 0xf100)
                {
                    io_sd = address & 0xff;
                    LCD_Enable((io_sd & 8) != 0);
                }
                else if (address == 0xf105)
                {
                    LCD_Write(0, value);
                    ga_lcd_counter = 500;
                }
                else if (address == 0xf104)
                {
                    LCD_Write(1, value);
                    ga_lcd_counter = 500;
                }
                else if (address == 0xf107)
                {
                    io_sd = value;
                }
                else
                {
                    printf("Unknown write %x %x\n", address, value);
                }
            }
        }
        else if (mcu_jv880 && address >= 0x6196 && address <= 0x6199)
        {
            // nop: the jv880 rom writes into the rom at 002E77-002E7D
        }
        else
        {
            printf("Unknown write %x %x\n", address, value);
        }
    }
    else if (page == 5 && mcu_mk1)
    {
        sram[address & 0x7fff] = value; // FIXME
    }
    else if (page == 10 && !mcu_mk1)
    {
        sram[address & 0x7fff] = value; // FIXME
    }
    else if (page == 12 && mcu_jv880)
    {
        nvram[address & 0x7fff] = value; // FIXME
    }
    else if (page == 14 && mcu_jv880)
    {
        cardram[address & 0x7fff] = value; // FIXME
    }
    else
    {
        printf("Unknown write %x %x\n", (page << 16) | address, value);
    }
}

static void MCU_Write16(uint32_t address, uint16_t value)
{
    address &= ~1;
    MCU_Write(address, value >> 8);
    MCU_Write(address + 1, value & 0xff);
}

static void MCU_ReadInstruction(void)
{
    uint8_t operand = MCU_ReadCodeAdvance();

    MCU_Operand_Table[operand](operand);

    if (mcu.sr & STATUS_T)
    {
        MCU_Interrupt_Exception(EXCEPTION_SOURCE_TRACE);
    }
}

static void MCU_Reset(void)
{
    // Added zeroing of other globals
    ga_int = ga_int_enable = ga_int_trigger = 0;
    ga_lcd_counter = 0;
    memset(dev_register, 0, sizeof(dev_register));
    io_sd = 0x00;
    adf_rd = ssr_rd = 0;
    analog_end_time = 0;
    uart_write_ptr = uart_read_ptr = 0;
    mcu_uart_rx_byte = 0;
    mcu_uart_rx_delay = mcu_uart_tx_delay = 0;
    mcu_p0_data = 0x00;
    operand_type = 0;
    operand_ea = operand_data = 0;
    opcode_extended = operand_ep = operand_size = operand_reg = 0;

    memset(&mcu, 0, sizeof(mcu_t));

    mcu.sr = 0x700;

    uint32_t reset_address = MCU_GetVectorAddress(VECTOR_RESET);
    mcu.cp = (reset_address >> 16) & 0xff;
    mcu.pc = reset_address & 0xffff;

    mcu.exception_pending = -1;

    //MCU_DeviceReset
    // dev_register[0x00] = 0x03;
    // dev_register[0x7c] = 0x87;
    dev_register[DEV_RAME] = 0x80;
    dev_register[DEV_SSR] = 0x80;

    if (mcu_mk1)
    {
        ga_int_enable = 255;
    }
}

void MCU_PostUART(uint8_t data)
{
    uart_buffer[uart_write_ptr] = data;
    uart_write_ptr = (uart_write_ptr + 1) % uart_buffer_size;
}

static void MCU_UpdateUART_RX(void)
{
    if ((dev_register[DEV_SCR] & 16) == 0) // RX disabled
        return;
    if (uart_write_ptr == uart_read_ptr) // no byte
        return;

    if (dev_register[DEV_SSR] & 0x40)
        return;

    if (mcu.cycles < mcu_uart_rx_delay)
        return;

    mcu_uart_rx_byte = uart_buffer[uart_read_ptr];
    uart_read_ptr = (uart_read_ptr + 1) % uart_buffer_size;
    dev_register[DEV_SSR] |= 0x40;
    MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_UART_RX, (dev_register[DEV_SCR] & 0x40) != 0);
}

// dummy TX
static void MCU_UpdateUART_TX(void)
{
    if ((dev_register[DEV_SCR] & 32) == 0) // TX disabled
        return;

    if (dev_register[DEV_SSR] & 0x80)
        return;

    if (mcu.cycles < mcu_uart_tx_delay)
        return;

    dev_register[DEV_SSR] |= 0x80;
    MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_UART_TX, (dev_register[DEV_SCR] & 0x80) != 0);

    // printf("tx:%x\n", dev_register[DEV_TDR]);
}

static uint8_t MCU_ReadP0(void)
{
    return 0xff;
}

static uint8_t MCU_ReadP1(void)
{
    uint8_t data = 0xff;
    uint32_t button_pressed = 0; // DBP: REMOVED: (uint32_t)SDL_AtomicGet(&mcu_button_pressed);

    if ((mcu_p0_data & 1) == 0)
        data &= ((button_pressed >> 0) & 255) ^ 255;
    if ((mcu_p0_data & 2) == 0)
        data &= ((button_pressed >> 8) & 255) ^ 255;
    if ((mcu_p0_data & 4) == 0)
        data &= ((button_pressed >> 16) & 255) ^ 255;
    if ((mcu_p0_data & 8) == 0)
        data &= ((button_pressed >> 24) & 255) ^ 255;

    return data;
}

static void unscramble(uint8_t *src, uint8_t *dst, int len)
{
    // Improved to use look up tables (still very fast if calculated every time)
    uint8_t dd_lut[256];
    uint32_t addr_lut_low[256], addr_lut_mid[256], addr_lut_high[16];
    for (unsigned b = 0; b != 256; b++)
    {
        uint32_t val_dd = 0, val_low = 0, val_mid = 0, val_high = 0;
        for (uint8_t j = 0; j != 8; j++)
        {
            static const uint8_t dd[] = { 2, 0, 4, 5, 7, 6, 3, 1 }, al[] = { 2, 0, 3, 4, 1, 9, 13, 10 }, am[] = { 18, 17, 6, 15, 11, 16, 8, 5 }, ah[] = { 12, 7, 14, 19 };
            if (b & (1 << dd[j])) val_dd |= (1 << j);
            if (b & (1 << j)) { val_low |= (1 << al[j]); val_mid |= (1 << am[j]); if (j < 4 && b < 16) { val_high |= (1 << ah[j]); } }
        }
        addr_lut_low[b] = val_low;
        addr_lut_mid[b] = val_mid;
        if (b < 16) addr_lut_high[b] = val_high;
        dd_lut[b] = (uint8_t)val_dd;
    }
    for (int i = 0; i < len; i++)
        dst[i] = dd_lut[src[(i & ~0xfffff) | addr_lut_low[i & 0xff] | addr_lut_mid[(i >> 8) & 0xff] | addr_lut_high[(i >> 16) & 0x0f]]];
}

static inline void MCU_PostSample(int *sample)
{
    if (!render_output) return; // during NUKEDSC55_Init
    sample[0] >>= 15;
    if (sample[0] > INT16_MAX)
        sample[0] = INT16_MAX;
    else if (sample[0] < INT16_MIN)
        sample[0] = INT16_MIN;
    sample[1] >>= 15;
    if (sample[1] > INT16_MAX)
        sample[1] = INT16_MAX;
    else if (sample[1] < INT16_MIN)
        sample[1] = INT16_MIN;
    *(render_output++) = sample[0];
    *(render_output++) = sample[1];
}

static void MCU_GA_SetGAInt(uint8_t line, int value)
{
    // guesswork
    // Changed ga_int to be a bitmask
    if (value && !(ga_int & (1 << line)) && (ga_int_enable & (1 << line)) != 0)
        ga_int_trigger = line;
    ga_int = (value ? (uint8_t)(ga_int | (1 << line)) : (uint8_t)(ga_int & (~(1 << line))));

    if (mcu_jv880)
        MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_IRQ0, ga_int_trigger != 0);
    else
        MCU_Interrupt_SetRequest(INTERRUPT_SOURCE_IRQ1, ga_int_trigger != 0);
}

enum class ResetType {
    NONE,
    GS_RESET,
    GM_RESET,
};

static void MIDI_Reset(ResetType resetType)
{
    const unsigned char gmReset[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    const unsigned char gsReset[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };

    if (resetType == ResetType::GS_RESET)
    {
        for (size_t i = 0; i < sizeof(gsReset); i++)
        {
            MCU_PostUART(gsReset[i]);
        }
    }
    else  if (resetType == ResetType::GM_RESET)
    {
        for (size_t i = 0; i < sizeof(gmReset); i++)
        {
            MCU_PostUART(gmReset[i]);
        }
    }
}
#endif // NUKEDSC55_MCU_CPP

void NUKEDSC55_Shutdown()
{
    delete [] sc55buffers;
    sc55buffers = NULL;
}

unsigned NUKEDSC55_Init(bool _mk1, bool _cm300, bool _jv880, bool _scb55, bool _sc155, uint8_t* pRom1, uint8_t* pRom2, int rom2size, uint8_t* pRomSM, uint8_t* pRomWave1, uint8_t* pRomWave2, uint8_t* pRomWave3)
{
    NUKEDSC55_Shutdown();

    mcu_mk1 = _mk1;
    mcu_cm300 = _cm300;
    mcu_jv880 = _jv880;
    mcu_scb55 = _scb55;
    mcu_sc155 = _sc155;

    const size_t buffers_size = (mcu_mk1 ? 0x100000 : 0x200000) + (mcu_jv880 ? 0x200000 : 0x100000) + (mcu_jv880 ? 0x200000 : 0x100000) + (mcu_jv880 ? 0x800000 : 0)
        + 4096 + 128 + 192 + uart_buffer_size + ROM1_SIZE + rom2size + RAM_SIZE + SRAM_SIZE + NVRAM_SIZE + CARDRAM_SIZE + (sizeof(uint16_t) * 0x4000);

    uint8_t* buffers = sc55buffers = new uint8_t[buffers_size];
    uint8_t* waverom1   = buffers; buffers += (mcu_mk1 ? 0x100000 : 0x200000);
    uint8_t* waverom2   = buffers; buffers += (mcu_jv880 ? 0x200000 : 0x100000);
    uint8_t* waverom3   = buffers; buffers += (mcu_jv880 ? 0x200000 : 0x100000);
    uint8_t* waveromexp = buffers; buffers += (mcu_jv880 ? 0x800000 : 0);
    sm_rom              = buffers; buffers += 4096;
    sm_ram              = buffers; buffers += 128;
    sm_shared_ram       = buffers; buffers += 192;
    uart_buffer         = buffers; buffers += uart_buffer_size;
    rom1                = buffers; buffers += ROM1_SIZE;
    rom2                = buffers; buffers += rom2size;
    ram                 = buffers; buffers += RAM_SIZE;
    sram                = buffers; buffers += SRAM_SIZE;
    nvram               = buffers; buffers += NVRAM_SIZE;
    cardram             = buffers; buffers += CARDRAM_SIZE;
    uint16_t* pcm_eram = (uint16_t*)buffers;

    memcpy(rom1, pRom1, ROM1_SIZE);
    memcpy(rom2, pRom2, rom2size);
    rom2_mask = rom2size - 1;

    if (mcu_mk1)
    {
        unscramble(pRomWave1, waverom1, 0x100000);
        unscramble(pRomWave2, waverom2, 0x100000);
        unscramble(pRomWave3, waverom3, 0x100000);
    }
    else if (mcu_jv880)
    {
        unscramble(pRomWave1, waverom1, 0x200000);
        unscramble(pRomWave2, waverom2, 0x200000);
    }
    else
    {
        unscramble(pRomWave1, waverom1, 0x200000);
        if (pRomWave2)
            unscramble(pRomWave2, mcu_scb55 ? waverom3 : waverom2, 0x100000);
        if (pRomSM)
            memcpy(sm_rom, pRomSM, ROMSM_SIZE);
    }

    MCU_Reset();
    SM_Reset();
    PCM_Reset(pcm_eram, waverom1, waverom2, waverom3, waveromexp);
    TIMER_Reset();

    MIDI_Reset(ResetType::GS_RESET);
    for (const uint64_t boot_cycles = (mcu_mk1 ? 8400000 : 120000000); mcu.cycles < boot_cycles;)
    {
        if (!mcu.ex_ignore)
            MCU_Interrupt_Handle();
        else
            mcu.ex_ignore = 0;

        if (!mcu.sleep)
            MCU_ReadInstruction();

        mcu.cycles += 12; // FIXME: assume 12 cycles per instruction

        PCM_Update(mcu.cycles);

        TIMER_Clock(mcu.cycles);

        if (!mcu_mk1 && !mcu_jv880 && !mcu_scb55)
            SM_Update(mcu.cycles);
        else
        {
            MCU_UpdateUART_RX();
            MCU_UpdateUART_TX();
        }

        MCU_UpdateAnalog(mcu.cycles);

        if (mcu_mk1 && ga_lcd_counter && !--ga_lcd_counter)
        {
            MCU_GA_SetGAInt(1, 0);
            MCU_GA_SetGAInt(1, 1);
        }
    }

    #ifdef NUKEDSC55_USE_OVERSAMPLING
    return ((mcu_mk1 || mcu_jv880) ? 64000 : 66207);
    #else
    return ((mcu_mk1 || mcu_jv880) ? 64000 : 66207) / 2;
    #endif
}

void NUKEDSC55_Render(short* buf, uint32_t len)
{
    // PCM writes a sample at most every 20 or so cycles, so we can just loop the overall emulation until we got as many samples as requested because PCM_Update won't ever post more than 1 stereo sample pair.
    for (render_output = buf, buf += len; render_output != buf;) // wait until len samples have been written
    {
        #ifdef NUKEDSC55_USE_OVERSAMPLING
        #error With oversampling, requesting an uneven number of stereo sample pairs needs to be handled
        if (pcm.config_reg_3c & 0x40) { /*handle oversampling*/ }
        #endif

        if (!mcu.ex_ignore)
            MCU_Interrupt_Handle();
        else
            mcu.ex_ignore = 0;

        if (!mcu.sleep)
            MCU_ReadInstruction();

        mcu.cycles += 12; // FIXME: assume 12 cycles per instruction

        PCM_Update(mcu.cycles);

        TIMER_Clock(mcu.cycles);

        if (!mcu_mk1 && !mcu_jv880 && !mcu_scb55)
            SM_Update(mcu.cycles);
        else
        {
            MCU_UpdateUART_RX();
            MCU_UpdateUART_TX();
        }

        MCU_UpdateAnalog(mcu.cycles);

        if (mcu_mk1 && ga_lcd_counter && !--ga_lcd_counter)
        {
            MCU_GA_SetGAInt(1, 0);
            MCU_GA_SetGAInt(1, 1);
        }
    }
    render_output = NULL;
}
