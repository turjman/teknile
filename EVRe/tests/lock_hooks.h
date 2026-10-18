/* SPDX-License-Identifier: Apache-2.0 */
/*
 * lock_hooks.h: EVRE_LOCK and EVRE_UNLOCK for the lock build of lib_test.cpp.
 *
 * Given to the compiler with -include, for EVRe.cpp and the test alike, the
 * way a device gives its own (EVRe.h, EVRE_LOCK). They count, so the test sees
 * where the library takes the lock and that it never nests it.
 */

#ifndef LOCK_HOOKS_H
#define LOCK_HOOKS_H

#define EVRE_TEST_LOCK_HOOKS 1

void evre_test_lock(void);
void evre_test_unlock(void);

#define EVRE_LOCK() evre_test_lock()
#define EVRE_UNLOCK() evre_test_unlock()

#endif
