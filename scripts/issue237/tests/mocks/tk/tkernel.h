#ifndef ISSUE237_TEST_TKERNEL_H
#define ISSUE237_TEST_TKERNEL_H
#include <stdint.h>
typedef int ID;
typedef int ER;
typedef int INT;
typedef unsigned int UINT;
typedef int TMO;
typedef struct { int32_t hi; uint32_t lo; } SYSTIM;
typedef struct { void *exinf; UINT flgatr; UINT iflgptn; } T_CFLG;
#define E_OK 0
#define E_SYS (-5)
#define E_CTX (-25)
#define E_TMOUT (-50)
#define TA_TFIFO 0
#define TA_WMUL 8
#define TWF_ORW 1
#define TWF_BITCLR 2
#define TMO_FEVR (-1)
ER tk_dis_dsp(void);
ER tk_ena_dsp(void);
ER tk_get_otm(SYSTIM *now);
ID tk_cre_flg(const T_CFLG *cfg);
ER tk_set_flg(ID id, UINT bits);
ER tk_wai_flg(ID id, UINT bits, UINT mode, UINT *pattern, TMO timeout);
ER tk_dly_tsk(UINT ms);
#endif
