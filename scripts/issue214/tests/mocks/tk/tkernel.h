#ifndef TEST_TKERNEL_H
#define TEST_TKERNEL_H
#include <stdint.h>
typedef int ID;
typedef int ER;
typedef int INT;
typedef unsigned int UINT;
typedef int TMO;
typedef struct { int32_t hi; uint32_t lo; } SYSTIM;
typedef struct { void *exinf; UINT flgatr; UINT iflgptn; } T_CFLG;
typedef struct {
    void *exinf; UINT tskatr; void (*task)(INT, void *);
    INT itskpri; INT stksz; void *bufptr;
} T_CTSK;
#define E_OK 0
#define TA_TFIFO 0
#define TA_HLNG 1
#define TA_RNG3 2
#define TWF_ORW 1
#define TWF_BITCLR 2
ER tk_dis_dsp(void);
ER tk_ena_dsp(void);
ER tk_get_otm(SYSTIM *now);
ID tk_cre_flg(const T_CFLG *cfg);
ID tk_cre_tsk(const T_CTSK *cfg);
ER tk_sta_tsk(ID id, INT code);
ER tk_del_flg(ID id);
ER tk_del_tsk(ID id);
ER tk_set_flg(ID id, UINT bits);
ER tk_wai_flg(ID id, UINT bits, UINT mode, UINT *pattern, TMO timeout);
#endif
