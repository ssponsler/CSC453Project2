#include <stdio.h>
#ifndef RR_H
#define RR_H

typedef struct threadinfo_st *thread;
typedef struct scheduler *scheduler;

void rr_admit(thread new_t);
void rr_remove(thread victim);
thread rr_next(void);
int rr_qlen(void);

extern struct scheduler rr_finish;

#endif
