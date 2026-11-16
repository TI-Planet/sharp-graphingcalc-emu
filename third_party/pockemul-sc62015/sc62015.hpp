/*** PC-E500 Emulator [SC62015.CC] ************************/
/* SC62015 CPU emulation                                  */
/*                       Copyright (C) Matsumo 1998,1999  */
/**********************************************************/
// SPDX-License-Identifier: GPL-2.0-only
// Adapted from PockEmul 898c891abb25da94df8951f13a9fc4a0b986c69d.
// Standalone bus, internal I/O, bounds, interrupt and timing integration:
// Copyright (c) 2026 Sharp graphing calculator emulator contributors.
// See README.md in this directory for the extraction and local changes.
#pragma once
#include "sharp/sc62015.hpp"
#include <bit>
#include <cstddef>
#include <stdexcept>
namespace sharp::pock {
using BYTE=std::uint8_t; using WORD=std::uint16_t; using UINT32=std::uint32_t;
#define		MASK_4		0xf				/*  4bit data mask */
#define		MASK_8		0xff			/*  8bit data mask */
#define		MASK_16		0xffff			/* 16bit data mask */
#define		MASK_20		0xfffff			/* 20bit data mask */
#define		MASK_24		0xffffff		/* 24bit data mask */
#define		SIZE_8		1				/*  8bit data size(by byte) */
#define		SIZE_16		2				/* 16bit data size(by byte) */
#define		SIZE_20		3				/* 20bit data size(by byte) */
#define		SIZE_24		SIZE_20+1		/* 24bit dummy data size */
#define		IMEM_KOL	0xf0			// Key Output Port(L)
#define		IMEM_KOH	0xf1			// Key Output Port(H)
#define		IMEM_KI		0xf2			// Key Input Port
#define		IMEM_EOL	0xf3			// E-port Output Buffer(L)
#define		IMEM_EOH	0xf4			// E-port Output Buffer(H)
#define		IMEM_EIL	0xf5			// E-port Input Buffer(L)
#define		IMEM_EIH	0xf6			// E-port Input Buffer(H)
#define		IMEM_UCR	0xf7			// UART Control Register
#define		IMEM_USR	0xf8			// UART Status Register
#define		IMEM_RxD	0xf9			// UART Receive Buffer
#define		IMEM_TxD	0xfa			// UART Send Buffer
#define		IMEM_IISR	0xeb			/* Interrupt In Service Register address*/
#define		IMEM_AMC	0xef
#define		IMEM_IMR	0xfb			/* Interrupt Mask Register address*/
#define		IMEM_ISR	0xfc			/* Interrupt Status Register address*/
#define		IMEM_SCR	0xfd			/* System Control Register address*/
#define		IMEM_ACM	0xfe
#define		IMEM_SSR	0xff			/* System Status Register address*/
#define		VECT_IR		0xffffa			/* Interrupt Vector address */
#define		VECT_RESET	0xffffd			/* Reset Vector address */
#define IMEM_BGN		(HEAD_LEN+REG_LEN)
#define IMEM_LEN		256
typedef
union {
    struct first{
        BYTE	a,b,il,ih;
        WORD	pc,ps,xl,xh,yl,yh,ul,uh,sl,sh;
        BYTE	c:1;
        BYTE	z:1;
    }r;
    struct second{
        WORD	ba,i;
        UINT32	p,x,y,u,s;
        BYTE	f;
    }x;
} SCREG;


static_assert(std::endian::native == std::endian::little, "PockEmul register layout requires a little-endian host");
static_assert(offsetof(SCREG, x.f) == 24);
class Cpu {
public:
    explicit Cpu(Sc62015Bus& b) : bus(b) {}
    Sc62015Bus& bus;
    SCREG reg{};
    std::array<BYTE,256> imem{};
    BYTE pre_1{1},pre_2{1};
    bool halt{},off{};
    std::uint32_t executed{}, prefix_depth{};
    std::uint64_t main_timer{},sub_timer{};
    void AddState(unsigned n) { executed+=n; }
    UINT32 read_internal(BYTE address,unsigned size);
    void write_internal(BYTE address,UINT32 value,unsigned size);
    BYTE read8(UINT32 a) { return bus.read8(a&0xfffff); }
    UINT32 read16(UINT32 a) { return read8(a)|(UINT32(read8(a+1))<<8); }
    UINT32 read24(UINT32 a) { return read16(a)|(UINT32(read8(a+2))<<16); }
    UINT32 read20(UINT32 a) { return read24(a)&0xfffff; }
    UINT32 fetch16(UINT32 a) { return read8(a)|(UINT32(read8((a&0xf0000)|((a+1)&0xffff)))<<8); }
    UINT32 fetch24(UINT32 a) { return fetch16(a)|(UINT32(read8((a&0xf0000)|((a+2)&0xffff)))<<16); }
    UINT32 fetch20(UINT32 a) { return fetch24(a)&0xfffff; }
    void write8(UINT32 a,UINT32 v) { bus.write8(a&0xfffff,BYTE(v)); }
    void write16(UINT32 a,UINT32 v) { write8(a,v);write8(a+1,v>>8); }
    void write24(UINT32 a,UINT32 v) { write16(a,v);write8(a+2,v>>16); }
    void write20(UINT32 a,UINT32 v) { write24(a,v&0xfffff); }
    void Step_sc62015_();
    void OpExec(BYTE op);
    void interrupt();
    std::uint32_t step();
BYTE Conv_imemAdr(BYTE d,bool m);
BYTE Get_i8(BYTE a,bool m);
WORD Get_i16(BYTE a,bool m);
UINT32 Get_i20(BYTE a,bool m);
UINT32 Get_i24(BYTE a,bool m);
void Set_i8(BYTE a,BYTE d,bool m);
void Set_i16(BYTE a,WORD d,bool m);
void Set_i20(BYTE a,UINT32 d,bool m);
void Set_i24(BYTE a,UINT32 d,bool m);
UINT32 Get_r(BYTE r);
UINT32 Get_r2(BYTE r);
void Set_r(BYTE r,UINT32 d);
void Set_r2(BYTE r,UINT32 d);
void Chk_Flag(UINT32 d,BYTE len);
void Chk_Zero(UINT32 d,BYTE len);
UINT32 Get_d(BYTE len);
UINT32 Get_d2(BYTE len, BYTE *r);
UINT32 Get_i(void);
UINT32 Get_i2(BYTE *r);
BYTE bcd2hex(BYTE d);
WORD hex2bcd(BYTE d);
void Op_00(void);
void Op_01(void);
void Op_02(void);
void Op_03(void);
void Op_04(void);
void Op_05(void);
void Op_06(void);
void Op_07(void);
void Op_08(void);
void Op_09(void);
void Op_0a(void);
void Op_0b(void);
void Op_0c(void);
void Op_0d(void);
void Op_0e(void);
void Op_0f(void);
void Op_10(void);
void Op_11(void);
void Op_12(void);
void Op_13(void);
void Op_14(void);
void Op_15(void);
void Op_16(void);
void Op_17(void);
void Op_18(void);
void Op_19(void);
void Op_1a(void);
void Op_1b(void);
void Op_1c(void);
void Op_1d(void);
void Op_1e(void);
void Op_1f(void);
void Op_20(void);
void Op_21(void);
void Op_22(void);
void Op_23(void);
void Op_24(void);
void Op_25(void);
void Op_26(void);
void Op_27(void);
void Op_28(void);
void Op_29(void);
void Op_2a(void);
void Op_2b(void);
void Op_2c(void);
void Op_2d(void);
void Op_2e(void);
void Op_2f(void);
void Op_30(void);
void Op_31(void);
void Op_32(void);
void Op_33(void);
void Op_34(void);
void Op_35(void);
void Op_36(void);
void Op_37(void);
void Op_38(void);
void Op_39(void);
void Op_3a(void);
void Op_3b(void);
void Op_3c(void);
void Op_3d(void);
void Op_3e(void);
void Op_3f(void);
void Op_40(void);
void Op_41(void);
void Op_42(void);
void Op_43(void);
void Op_44(void);
void Op_45(void);
void Op_46(void);
void Op_47(void);
void Op_48(void);
void Op_49(void);
void Op_4a(void);
void Op_4b(void);
void Op_4c(void);
void Op_4d(void);
void Op_4e(void);
void Op_4f(void);
void Op_50(void);
void Op_51(void);
void Op_52(void);
void Op_53(void);
void Op_54(void);
void Op_55(void);
void Op_56(void);
void Op_57(void);
void Op_58(void);
void Op_59(void);
void Op_5a(void);
void Op_5b(void);
void Op_5c(void);
void Op_5d(void);
void Op_5e(void);
void Op_5f(void);
void Op_60(void);
void Op_61(void);
void Op_62(void);
void Op_63(void);
void Op_64(void);
void Op_65(void);
void Op_66(void);
void Op_67(void);
void Op_68(void);
void Op_69(void);
void Op_6a(void);
void Op_6b(void);
void Op_6c(void);
void Op_6d(void);
void Op_6e(void);
void Op_6f(void);
void Op_70(void);
void Op_71(void);
void Op_72(void);
void Op_73(void);
void Op_74(void);
void Op_75(void);
void Op_76(void);
void Op_77(void);
void Op_78(void);
void Op_79(void);
void Op_7a(void);
void Op_7b(void);
void Op_7c(void);
void Op_7d(void);
void Op_7e(void);
void Op_7f(void);
void Op_80(void);
void Op_81(void);
void Op_82(void);
void Op_83(void);
void Op_84(void);
void Op_85(void);
void Op_86(void);
void Op_87(void);
void Op_88(void);
void Op_89(void);
void Op_8a(void);
void Op_8b(void);
void Op_8c(void);
void Op_8d(void);
void Op_8e(void);
void Op_8f(void);
void Op_90(void);
void Op_91(void);
void Op_92(void);
void Op_93(void);
void Op_94(void);
void Op_95(void);
void Op_96(void);
void Op_97(void);
void Op_98(void);
void Op_99(void);
void Op_9a(void);
void Op_9b(void);
void Op_9c(void);
void Op_9d(void);
void Op_9e(void);
void Op_9f(void);
void Op_a0(void);
void Op_a1(void);
void Op_a2(void);
void Op_a3(void);
void Op_a4(void);
void Op_a5(void);
void Op_a6(void);
void Op_a7(void);
void Op_a8(void);
void Op_a9(void);
void Op_aa(void);
void Op_ab(void);
void Op_ac(void);
void Op_ad(void);
void Op_ae(void);
void Op_af(void);
void Op_b0(void);
void Op_b1(void);
void Op_b2(void);
void Op_b3(void);
void Op_b4(void);
void Op_b5(void);
void Op_b6(void);
void Op_b7(void);
void Op_b8(void);
void Op_b9(void);
void Op_ba(void);
void Op_bb(void);
void Op_bc(void);
void Op_bd(void);
void Op_be(void);
void Op_bf(void);
void Op_c0(void);
void Op_c1(void);
void Op_c2(void);
void Op_c3(void);
void Op_c4(void);
void Op_c5(void);
void Op_c6(void);
void Op_c7(void);
void Op_c8(void);
void Op_c9(void);
void Op_ca(void);
void Op_cb(void);
void Op_cc(void);
void Op_cd(void);
void Op_ce(void);
void Op_cf(void);
void Op_d0(void);
void Op_d1(void);
void Op_d2(void);
void Op_d3(void);
void Op_d4(void);
void Op_d5(void);
void Op_d6(void);
void Op_d7(void);
void Op_d8(void);
void Op_d9(void);
void Op_da(void);
void Op_db(void);
void Op_dc(void);
void Op_dd(void);
void Op_de(void);
void Op_df(void);
void Op_e0(void);
void Op_e1(void);
void Op_e2(void);
void Op_e3(void);
void Op_e4(void);
void Op_e5(void);
void Op_e6(void);
void Op_e7(void);
void Op_e8(void);
void Op_e9(void);
void Op_ea(void);
void Op_eb(void);
void Op_ec(void);
void Op_ed(void);
void Op_ee(void);
void Op_ef(void);
void Op_f0(void);
void Op_f1(void);
void Op_f2(void);
void Op_f3(void);
void Op_f4(void);
void Op_f5(void);
void Op_f6(void);
void Op_f7(void);
void Op_f8(void);
void Op_f9(void);
void Op_fa(void);
void Op_fb(void);
void Op_fc(void);
void Op_fd(void);
void Op_fe(void);
void Op_ff(void);
};
} // namespace sharp::pock
