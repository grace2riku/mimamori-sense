/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2026 by Ken Sakamura.
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2026/04.
 *
 *----------------------------------------------------------------------
 */

/*
 *	sysdef.h
 *
 *	System dependencies definition (EK-RA8P1)
 *	Included also from assembler program.
 */

#ifndef _MTKBSP_SYS_SYSDEF_DEPEND_H_
#define _MTKBSP_SYS_SYSDEF_DEPEND_H_

/* CPU-dependent definition */
/* [mimamori-sense R-003] BSP2 v1.00.04 のベンダ不具合修正: EK-RA8P1 の sysdef が
 * 誤って RA8M1 の CPU 定義（INTERNAL_RAM_SIZE=0xE0000=896KB）を include していた。
 * RA8P1 は SRAM 1872KB（INTERNAL_RAM_SIZE=0x1D4000）。誤値だと INTERNAL_RAM_END=
 * 0x220E0000 となり、実 RAM 使用末尾（__mtk3_SYSMEM_START≒0x2211e100）より低くなって
 * μT-Kernel システムメモリプールが空/負になり、初期タスク生成が E_NOMEM で失敗する
 * （"!ERROR! Initial Task can not creat"）。正しい RA8P1 定義を include する。
 * （→ migration guide 7.1） */
#include <sys/sysdepend/ra_fsp/cpu/ra8p1/sysdef.h>


/* ------------------------------------------------------------------------ */
/* Clock frequency
 */
#define CPUCLK_MHz	(1000)
#define ICLK_MHz	(250)
#define PCLKA_MHz	(125)
#define PCLKB_MHz	(62)
#define PCLKC_MHz	(125)
#define PCLKD_MHz	(250)
#define PCLKE_MHz	(250)

#define	SYSCLK		(CPUCLK_MHz*1000*1000)	// System clock (Hz)
#define TMCLK_KHz	(CPUCLK_MHz*1000)	// System timer clock input (kHz)
#define TMCLK		(CPUCLK_MHz)		// System timer clock input (MHz)

#endif /* _MTKBSP_TK_SYSDEF_DEPEND_H_ */
