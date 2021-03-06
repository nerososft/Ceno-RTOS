/***************************************************
* Ceno Real-time Operating System  (CenoRTOS)
* version 0.1
* author neroyang
* email nerosoft@outlook.com
* time 2018-11-2 
* 
* Copyright (C) 2018 CenoCloud. All Rights Reserved 
*
* Contract Information：
* https://www.cenocloud.com
****************************************************/

#include "thread.h"
#include <stdint.h>

OSThread * volatile OS_curr; /* pointer to the current thread */
OSThread * volatile OS_next; /* pointer to the next thread to run */

OSThread *OS_thread[32+1];
uint32_t OS_readySet;/*bit mask of thread that are ready to run */
uint32_t OS_delayedSet;/*bit mask of thread that are delayed */

#define LOG2(x)(32U - __clz(x))/*00000000 11000000 11000000 11000000 = (32-8) = 24*/

extern void __disable_irq(void);
extern void __enable_irq(void);
extern void Q_REQUIRE(void);
extern void Q_ASSERT(void);
extern void Q_ERROR(void);

OSThread idleThread;
void main_idleThread() {
    while (1) {
        OS_onIdle();
    }
}

void OS_init(void *stkSto, uint32_t stkSize) {
    /* set the PendSV interrupt priority to the lowest level 0xFF */
    *(uint32_t volatile *)0xE000ED20 |= (0xFFU << 16);
	
	OSThread_start(&idleThread,
					0U,
                   &main_idleThread,
                   stkSto, stkSize);
}

void OS_sched(void) {
    /* OS_next = ... */
		if(OS_readySet ==0U){
			OS_next = OS_thread[0]; /*the idle thread*/
		}else{
			OS_next = OS_thread[LOG2(OS_readySet)];
			Q_ASSERT(OS_next != (OSThread*)0);
		}

    if (OS_next != OS_curr) {
        *(uint32_t volatile *)0xE000ED04 = (1U << 28);
    }
}

void OS_delay(uint32_t ticks) {
	uint32_t bit;
   __disable_irq();
	
	/*never call OS_delay from idle thread */
	Q_REQUIRE(OS_curr!=OS_thread[0]);

	OS_curr->timeout = ticks;
	bit = (1U << (OS_curr->prio-1U));
	OS_readySet &= ~bit;
	OS_delayedSet |= bit;
	
	OS_sched();
	__enable_irq();
}


void OS_run(void){
	OS_onStartup();
	
	__disable_irq();
    OS_sched();
    __enable_irq();
	
	Q_ERROR();
}


void OS_tick(void){
	uint32_t workingSet = OS_delayedSet;
	while(workingSet!=0U){
		OSThread* t = OS_thread[LOG2(workingSet)];
		uint32_t bit;
		Q_ASSERT((t!=(OSThread*)0) && (t->timeout!=0U));
		
		bit = (1U<<(t->prio-1U));
		
		--t->timeout;
		if(t->timeout ==0U){
			OS_readySet |= bit; /* insert into ready set */
			OS_delayedSet &= ~bit; /* remove from delayed set */
		}
		workingSet &= ~bit; /* remove from  working set*/
	}
}

void OSThread_start(
    OSThread *me,
		uint8_t prio,
    OSThreadHandler threadHandler,
    void *stkSto, uint32_t stkSize)
{
    /* round down the stack top to the 8-byte boundary
    * NOTE: ARM Cortex-M stack grows down from hi -> low memory
    */
    uint32_t *sp = (uint32_t *)((((uint32_t)stkSto + stkSize) / 8) * 8);
    uint32_t *stk_limit;
	
	Q_REQUIRE((prio<Q_DIM(OS_thread)) 
		   && (OS_thread[prio] == (OSThread*)0));
    
    *(--sp) = (1U << 24);  /* xPSR */
    *(--sp) = (uint32_t)threadHandler; /* PC */
    *(--sp) = 0x0000000EU; /* LR  */
    *(--sp) = 0x0000000CU; /* R12 */
    *(--sp) = 0x00000003U; /* R3  */
    *(--sp) = 0x00000002U; /* R2  */
    *(--sp) = 0x00000001U; /* R1  */
    *(--sp) = 0x00000000U; /* R0  */
    /* additionally, fake registers R4-R11 */
    *(--sp) = 0x0000000BU; /* R11 */
    *(--sp) = 0x0000000AU; /* R10 */
    *(--sp) = 0x00000009U; /* R9 */
    *(--sp) = 0x00000008U; /* R8 */
    *(--sp) = 0x00000007U; /* R7 */
    *(--sp) = 0x00000006U; /* R6 */
    *(--sp) = 0x00000005U; /* R5 */
    *(--sp) = 0x00000004U; /* R4 */

    /* save the top of the stack in the thread's attibute */
    me->sp = sp;
    
    /* round up the bottom of the stack to the 8-byte boundary */
    stk_limit = (uint32_t *)(((((uint32_t)stkSto - 1U) / 8) + 1U) * 8);

    /* pre-fill the unused part of the stack with 0xDEADBEEF */
    for (sp = sp - 1U; sp >= stk_limit; --sp) {
        *sp = 0xDEADBEEFU;
    }
		

		/* 将线程放到线程数组里*/
		OS_thread[prio] = me;
		me->prio = prio;
		if(prio >0U ){
			OS_readySet |= (1U<<(prio-1U));
		}
}

__asm
void PendSV_Handler(void) {
    IMPORT  OS_curr  /* extern variable */
    IMPORT  OS_next  /* extern variable */
   
    /* __disable_irq(); */
    CPSID         I

    /* if (OS_curr != (OSThread *)0) { */ 
    LDR           r1,=OS_curr
    LDR           r1,[r1,#0x00]
    CBZ           r1,PendSV_restore

    /*     push registers r4-r11 on the stack */
    PUSH          {r4-r11}    

    /*     OS_curr->sp = sp; */ 
    LDR           r1,=OS_curr
    LDR           r1,[r1,#0x00]
    STR           sp,[r1,#0x00]
    /* } */

PendSV_restore    
    /* sp = OS_next->sp; */
    LDR           r1,=OS_next
    LDR           r1,[r1,#0x00]
    LDR           sp,[r1,#0x00]

    /* OS_curr = OS_next; */
    LDR           r1,=OS_next
    LDR           r1,[r1,#0x00]
    LDR           r2,=OS_curr
    STR           r1,[r2,#0x00]

    /* pop registers r4-r11 */ 
    POP           {r4-r11}    

    /* __enable_irq(); */
    CPSIE         I

    /* return to the next thread */
    BX            lr    
}