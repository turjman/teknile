/* SPDX-License-Identifier: Apache-2.0 */
/*
 * malloc_lock.c
 *
 *  Created on: Sep 30, 2025
 *      Author: Ragab
 */

#define MALLOC_LOCK_ENABLE
#ifdef MALLOC_LOCK_ENABLE

// malloc_lock.c — protect newlib/newlib-nano malloc/free on Cortex-M7
#include <stdint.h>
#include <sys/reent.h>
#include "cmsis_gcc.h"   // or core_cm7.h for __get_BASEPRI, __set_BASEPRI_MAX
#include "stm32h7xx.h"   // for __NVIC_PRIO_BITS if you prefer

// Choose a mask level: block ANY IRQ that might call malloc/free.
// On H7, __NVIC_PRIO_BITS is typically 4 -> priorities 0..15 (0 = highest).
// Example: mask priorities 0..5 (allow 6..15 to run during allocation).

#ifndef MALLOC_MASK_PREEMPT_PRIORITY
#define MALLOC_MASK_PREEMPT_PRIORITY  1  // tune to your app
#endif

// Convert preempt priority to BASEPRI value (left-justified in 8-bit field)
#ifndef MALLOC_BASEPRI_MASK
#define MALLOC_BASEPRI_MASK  ((MALLOC_MASK_PREEMPT_PRIORITY) << (8 - __NVIC_PRIO_BITS))
#endif

static uint32_t g_prev_basepri;

static inline uint32_t enter_malloc_crit(void) {
	uint32_t old = __get_BASEPRI();
	__set_BASEPRI_MAX(MALLOC_BASEPRI_MASK);  // mask all IRQs with prio <= chosen level
	__DSB();
	__ISB();
	return old;
}

static inline void exit_malloc_crit(uint32_t old) {
	__set_BASEPRI(old);
	__DSB();
	__ISB();
}

// Called by newlib/newlib-nano around heap ops
void __malloc_lock(struct _reent *r) {
	(void) r;
	g_prev_basepri = enter_malloc_crit();
}
void __malloc_unlock(struct _reent *r) {
	(void) r;
	exit_malloc_crit(g_prev_basepri);
}

#endif
