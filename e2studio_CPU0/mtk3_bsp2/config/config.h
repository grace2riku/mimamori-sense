/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2024/02.
 *
 *----------------------------------------------------------------------
 */

#ifndef _MTKBSP_TK_CONFIG_
#define _MTKBSP_TK_CONFIG_
/*
 *	config.h
 *	User Configuration Definition
 */
/*---------------------------------------------------------------------- */
/* SYSCONF : micro T-Kernel system configuration
 */

#define	CNF_SYSTEMAREA_TOP	0	/* 0: Use system default address */
/* [mimamori-sense R-003] System memory pool upper bound = CPU0 RAM end.
 * BSP2 の ra8p1/sysdef.h は INTERNAL_RAM_END = 0x221D4000（SRAM 全 1872KB を単一コアが
 * 占有する前提）だが、本機はマルチコアで FSP が CPU0 の RAM 区画を 0x22000000..0x221B0000
 * （1792KB, memory_regions.lld: RAM_START+RAM_LENGTH=0x1b0000）に制限している。
 * 既定（CNF_SYSTEMAREA_END=0）だと Imalloc プールが INTERNAL_RAM_END まで広がり、
 * knl_init_Imalloc() が高位側のエリア境界（AreaQue 終端）を CPU0 が所有しない
 * 0x221B0000..0x221D4000（CPU1 区画）へ書き込んでプールが壊れ、初期タスクのスタック確保が
 * E_NOMEM で失敗する（"!ERROR! Initial Task can not creat"）。
 * プール上限を CPU0 RAM 末尾に固定して回避する（プールは約585KB確保される）。
 * 注意: RAM 区画を変更したらこの値も追従させること（→ migration guide 7.1）。 */
#define CNF_SYSTEMAREA_END	0x221B0000	/* CPU0 RAM end (RAM_START 0x22000000 + RAM_LENGTH 0x1b0000) */

#define	CNF_MAX_TSKPRI		32	/* Task Max priority */

#define CNF_TIMER_PERIOD	10	/* System timer period */

/* Maximum number of kernel objects */
#define CNF_MAX_TSKID		32	/* Task */
#define CNF_MAX_SEMID		16	/* Semaphore */
#define CNF_MAX_FLGID		16	/* Event flag */
#define CNF_MAX_MBXID		8	/* Mailbox*/
/* [mimamori-sense R-006] Mutex max count 4 -> 16.
 * LVGL OSAL（src/lv_os_mtkernel.c）が general(lv_lock) + builtin mem +
 * Dave2D HW 排他(xd2Semaphore) + 画像/フォントキャッシュ群で 6 個以上の
 * mutex を生成するため、既定値 4 では tk_cre_mtx が E_LIMIT で失敗する。
 * 注意: BSP2 を再 vendoring すると既定値 4 に戻る（→ migration guide 6.2）。 */
#define CNF_MAX_MTXID		16	/* Mutex */
#define CNF_MAX_MBFID		8	/* Message buffer */
#define CNF_MAX_MPLID		4	/* Memory pool */
#define CNF_MAX_MPFID		8	/* Fixed size memory pool */
#define CNF_MAX_CYCID		4	/* Cyclic handler */
#define CNF_MAX_ALMID		8	/* Alarm handler */

/* Device configuration */
#define CNF_MAX_REGDEV		(8)	/* Max registered device */
#define CNF_MAX_OPNDEV		(16)	/* Max open device */
#define CNF_MAX_REQDEV		(16)	/* Max request device */
#define CNF_DEVT_MBFSZ0		(-1)	/* message buffer size for event notification */
#define CNF_DEVT_MBFSZ1		(-1)	/* message max size for event notification */

/* Version Number */
#define CNF_VER_MAKER		0
#define CNF_VER_PRID		0
#define CNF_VER_PRVER		3
#define CNF_VER_PRNO1		0
#define CNF_VER_PRNO2		0
#define CNF_VER_PRNO3		0
#define CNF_VER_PRNO4		0


/*---------------------------------------------------------------------- */
/* Backwards compatible api support 
 *      micro T-Kernel2.0 API support (Rendezvous)
 */
#define USE_LEGACY_API		(0)	/* 1: Valid  0: Invalid */
#define CNF_MAX_PORID		(0)	/* Maximum number of Rendezvous */


/*---------------------------------------------------------------------- */
/* Stack size definition
 */
#define CNF_EXC_STACK_SIZE	(0)	/* Exception stack size */
#define	CNF_TMP_STACK_SIZE	(256)	/* Temporary stack size */


/*---------------------------------------------------------------------- */
/* System function selection
 *  1: Use function.  0: No use function.
 */
#define USE_NOINIT		(0)	/* Use zero-clear bss section */
#define USE_IMALLOC		(1)	/* Use dynamic memory allocation */
#define USE_SHUTDOWN		(1)	/* Use System shutdown */
#define USE_STATIC_IVT		(0)	/* Use static interrupt vector table */


/*---------------------------------------------------------------------- */
/* Check API parameter
 *   1: Check parameter  0: Do not check parameter
 */
#define CHK_NOSPT		(1)	/* Check unsupported function (E_NOSPT) */
#define CHK_RSATR		(1)	/* Check reservation attribute error (E_RSATR) */
#define CHK_PAR			(1)	/* Check parameter (E_PAR) */
#define CHK_ID			(1)	/* Check object ID range (E_ID) */
#define CHK_OACV		(1)	/* Check Object Access Violation (E_OACV) */
#define CHK_CTX			(1)	/* Check whether task-independent part is running (E_CTX) */
#define CHK_CTX1		(1)	/* Check dispatch disable part */
#define CHK_CTX2		(1)	/* Check task independent part */
#define CHK_SELF		(1)	/* Check if its own task is specified (E_OBJ) */

#define	CHK_TKERNEL_CONST	(1)	/* Check const-type parameter */

/*---------------------------------------------------------------------- */
/* User initialization program (UserInit)
 *
 */
#define	USE_USERINIT		(0)	/*  1: Use UserInit  0: Do not use UserInit */
#define RI_USERINIT		(0)	/* UserInit start address */


/*---------------------------------------------------------------------- */
/* Debugger support function
 *   1: Valid  0: Invalid
 */
#define USE_DBGSPT		(0)	/* Use mT-Kernel/DS */
#define USE_OBJECT_NAME		(0)	/* Use DS object name */

#define OBJECT_NAME_LENGTH	(8)	/* DS Object name length */

/*---------------------------------------------------------------------- */
/* Use T-Monitor Compatible API Library  & Message to terminal.
 *  1: Valid  0: Invalid
 */
#define	USE_TMONITOR		(1)	/* T-Monitor API */
#define USE_SYSTEM_MESSAGE	(1)	/* System Message */
#define USE_EXCEPTION_DBG_MSG	(1)	/* Excepttion debug message */
#define USE_TASK_DBG_MSG	(0)	/* Tsak debug message */

/*---------------------------------------------------------------------- */
/* Use Co-Processor.
 *  1: Valid  0: Invalid
 */
#define	USE_FPU			(1)	/* Use FPU */
#define	USE_DSP			(0)	/* Use DSP */

#define	ALWAYS_FPU_ATR		(1)	/* Always set the TA_FPU attribute on all tasks */

/*---------------------------------------------------------------------- */
/* Use Physical timer.
 *  1: Valid  0: Invalid
 */
#define USE_PTMR		(0)	/* Use Physical timer */

/*---------------------------------------------------------------------- */
/* Use Sample device driver.
 *  1: Valid  0: Invalid
 */
#define USE_SDEV_DRV		(0)	/* Use Sample device driver */

/*---------------------------------------------------------------------- */
/*
 *	Use Standard C include file
 */
#define USE_STDINC_STDDEF	(1)	/* Use <stddef.h> */

#define USE_STDINC_STDINT	(1)	/* Use <stdint.h> */

/*---------------------------------------------------------------------- */
/*
 *	BSP depended Definition
 */
#include "config_bsp.h"

/*---------------------------------------------------------------------- */
/*
 *	Use function Definition
 */

#include <mtkernel/config/config_func.h>

#endif /* _MTKBSP_TK_CONFIG_ */
