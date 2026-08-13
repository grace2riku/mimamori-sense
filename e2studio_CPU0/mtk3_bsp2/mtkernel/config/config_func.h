/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.00.01
 *
 *    Copyright (C) 2006-2020 by Ken Sakamura.
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2020/05/29.
 *
 *----------------------------------------------------------------------
 */

/*
 *	config_func.h
 *	User Configuration Definition for function
 */

#ifndef _CONFIG_FUNC_H_
#define _CONFIG_FUNC_H_

/* [mimamori-sense R-003] FreeRTOS->uT-Kernel 3.0 migration footprint trim.
 * During migration BOTH RTOSes + the whole app (FreeRTOS/LVGL/camera/AI) stay
 * linked via ra_gen/main.c, and once knl_start_mtkernel() is actually called
 * the uT-Kernel objects are pulled in too, tightening CPU0's code-flash use.
 * Each USE_* switch is gated the same way in tkinit.c knl_init_object() and in
 * the subsystem's own .c file, so turning it 0 lets gc-sections drop that code.
 *
 * Only the subsystems the planned migration NEVER uses are disabled here
 * (the API replacement table in doc/migration/mtk3-migration-guide.md ch.5 uses
 * task / eventflag / semaphore / mutex / cyclic-alarm only):
 *   MAILBOX / MESSAGEBUFFER / RENDEZVOUS / FIX_MEMORYPOOL / DEVICE.
 * SEMAPHORE / EVENTFLAG / MUTEX / CYCLICHANDLER / ALARMHANDLER are kept ENABLED
 * (used in R-004..R-007) so no per-step re-enabling is required.
 *
 * NOTE: this trim alone does not fit R-003 in the old 960KB CPU0 partition; the
 * structural fix is rebalancing flash from the idle CPU1 (uses ~11KB of 64KB)
 * to CPU0 in solution.xml -> Memories. See guide 7.1 "code-flash overflow".
 *
 * [mimamori-sense #186] USE_MEMORYPOOL was 0 in R-003 and is RE-ENABLED here:
 * the D/AVE 2D heap (d1_malloc/d1_free, src/d1_heap_mtkernel.c) is built on a
 * variable size memory pool (tk_cre_mpl/tk_get_mpl/tk_rel_mpl) so that the D2
 * heap no longer comes from the FreeRTOS heap (ucHeap, 262,144 B) and is
 * mutually excluded by the uT-Kernel critical section (Issue #178).
 * See doc/migration/mtk3-migration-guide.md ch.10. */
#define USE_SEMAPHORE		(1)
#define	USE_MUTEX		(1)
#define	USE_EVENTFLAG		(1)
#define	USE_MAILBOX		(0)
#define	USE_MESSAGEBUFFER	(0)
#define USE_RENDEZVOUS		(0)
#define USE_MEMORYPOOL		(1)
#define	USE_FIX_MEMORYPOOL	(0)
#define	USE_TIMEMANAGEMENT	(1)
#define	USE_CYCLICHANDLER	(1)
#define USE_ALARMHANDLER	(1)
#define USE_DEVICE		(0)
#define USE_FAST_LOCK		(1)
#define USE_MULTI_LOCK		(1)

/* Task management */
#define USE_FUNC_TK_DEL_TSK
#define USE_FUNC_TK_EXT_TSK
#define USE_FUNC_TK_EXD_TSK
#define USE_FUNC_TK_TER_TSK
#define USE_FUNC_TK_CHG_PRI
#define USE_FUNC_TK_REL_WAI
#define USE_FUNC_TK_GET_REG
#define USE_FUNC_TK_SET_REG
#define USE_FUNC_TK_GET_CPR
#define USE_FUNC_TK_SET_CPR
#define USE_FUNC_TK_REF_TSK
#define USE_FUNC_TK_SUS_TSK
#define USE_FUNC_TK_RSM_TSK
#define USE_FUNC_TK_FRSM_TSK
#define USE_FUNC_TK_SLP_TSK
#define USE_FUNC_TK_WUP_TSK
#define USE_FUNC_TK_CAN_WUP
#define USE_FUNC_TK_DLY_TSK
#define USE_FUNC_TD_LST_TSK
#define USE_FUNC_TD_REF_TSK
#define USE_FUNC_TD_INF_TSK
#define USE_FUNC_TD_GET_REG
#define USE_FUNC_TD_SET_REG

/* Semaphore management API */
#define USE_FUNC_TK_DEL_SEM
#define USE_FUNC_TK_REF_SEM
#define USE_FUNC_TD_LST_SEM
#define USE_FUNC_TD_REF_SEM
#define USE_FUNC_TD_SEM_QUE

/* Mutex management API */
#define USE_FUNC_TK_DEL_MTX
#define USE_FUNC_TK_REF_MTX
#define USE_FUNC_TD_LST_MTX
#define USE_FUNC_TD_REF_MTX
#define USE_FUNC_TD_MTX_QUE

/* Event flag management API */
#define USE_FUNC_TK_DEL_FLG
#define USE_FUNC_TK_REF_FLG
#define USE_FUNC_TD_LST_FLG
#define USE_FUNC_TD_REF_FLG
#define USE_FUNC_TD_FLG_QUE

/* Mailbox management API */
#define USE_FUNC_TK_DEL_MBX
#define USE_FUNC_TK_REF_MBX
#define USE_FUNC_TD_LST_MBX
#define USE_FUNC_TD_REF_MBX
#define USE_FUNC_TD_MBX_QUE

/* Messagebuffer management API */
#define USE_FUNC_TK_DEL_MBF
#define USE_FUNC_TK_REF_MBF
#define USE_FUNC_TD_LST_MBF
#define USE_FUNC_TD_REF_MBF
#define USE_FUNC_TD_SMBF_QUE
#define USE_FUNC_TD_RMBF_QUE

/* Rendezvous management API (Legacy API) */
#define USE_FUNC_TK_DEL_POR
#define USE_FUNC_TK_FWD_POR
#define USE_FUNC_TK_REF_POR
#define USE_FUNC_TD_LST_POR
#define USE_FUNC_TD_REF_POR
#define USE_FUNC_TD_CAL_QUE
#define USE_FUNC_TD_ACP_QUE

/* Memory pool management API */
#define USE_FUNC_TK_DEL_MPL
#define USE_FUNC_TK_REF_MPL
#define USE_FUNC_TD_LST_MPL
#define USE_FUNC_TD_REF_MPL
#define USE_FUNC_TD_MPL_QUE

/* Fix-Memory Pool management API */
#define USE_FUNC_TK_DEL_MPF
#define USE_FUNC_TK_REF_MPF
#define USE_FUNC_TD_LST_MPF
#define USE_FUNC_TD_REF_MPF
#define USE_FUNC_TD_MPF_QUE

/* Time management API */
#define USE_FUNC_TK_SET_UTC
#define USE_FUNC_TK_GET_UTC
#define USE_FUNC_TK_SET_TIM
#define USE_FUNC_TK_GET_TIM
#define USE_FUNC_TK_GET_OTM
#define USE_FUNC_TD_GET_TIM
#define USE_FUNC_TD_GET_OTM

/* Cyclic handler management API */
#define USE_FUNC_TK_DEL_CYC
#define USE_FUNC_TK_STA_CYC
#define USE_FUNC_TK_STP_CYC
#define USE_FUNC_TK_REF_CYC
#define USE_FUNC_TD_LST_CYC
#define USE_FUNC_TD_REF_CYC

/* Alarm handler management API */
#define USE_FUNC_TK_DEL_ALM
#define USE_FUNC_TK_STP_ALM
#define USE_FUNC_TK_REF_ALM
#define USE_FUNC_TD_LST_ALM
#define USE_FUNC_TD_REF_ALM

/* System status management API */
#define USE_FUNC_TK_ROT_RDQ
#define USE_FUNC_TK_GET_TID
#define USE_FUNC_TK_DIS_DSP
#define USE_FUNC_TK_ENA_DSP
#define USE_FUNC_TK_REF_SYS
#define USE_FUNC_TK_REF_VER
#define USE_FUNC_TD_REF_SYS
#define USE_FUNC_TD_RDY_QUE

#endif /* _CONFIG_FUNC_H_ */
