/* SPDX-License-Identifier: Apache-2.0 */
/*
 * evre_guard.h: EVRe Guard, a layer above the EVRe protocol.
 *
 * EVRe moves bytes and never asks what they mean. EVRe Guard is where a device
 * decides who may read and write them. It is NOT part of the protocol: a
 * device uses it or not, and the protocol is the same either way. It plugs
 * into the library's two hooks, READ_HANDLER and WRITE_HANDLER (EVRe 1.1).
 *
 * This first part is the login:
 *
 *   - Before a login only DEVICE_ID and STATUS (0xA000..0xA003) and the spans
 *     in open_reads can be read, and only the login register written.
 *   - A login is a write of exactly login_size bytes to login_addr. The token
 *     is compared in constant time and never stored: a read of the login
 *     register returns what its memory holds, never the token. Log in with a
 *     WRITE_ACK: only its answer tells the host how the login went. A write of
 *     no bytes is no attempt. Give each device a token of its own: a token
 *     seen on the link then opens one device, not all of them.
 *   - A login never comes by broadcast. With ACCEPT_BROADCAST_D000 = 0 (the
 *     default) the library refuses a broadcast into the device bank before
 *     the guard is asked. With 1 the guard skips a broadcast write that
 *     touches the login register: it logs nobody in, the right token neither,
 *     counts no failure, starts no lockout, ends no session, counts as no
 *     activity, and its bytes are not stored (PERMISSION_DENIED, never
 *     answered, as no broadcast is). A broadcast is never answered and can
 *     never log in, so it is no channel to guess a token by; and one frame
 *     cannot lock out, or log out, every device on the link.
 *   - Every other broadcast write that reaches the guard needs an open
 *     session, as a unicast one does (LOGIN_REQUIRED, never answered): a
 *     broadcast never goes past the guard. With ACCEPT_BROADCAST_D000 = 0, one
 *     into the device bank never gets that far (PERMISSION_DENIED, from the
 *     library), session or not.
 *   - Brute force: after free_attempts wrong tokens every attempt is refused,
 *     the right token too, for lockout_ms; each further failure doubles it, up
 *     to lockout_max_ms. A good login clears the count. The lockout lives in
 *     RAM: a device that must survive a reset attack keeps `failures` in its
 *     own non-volatile memory and gives it back with evre_guard_restore().
 *   - A session ends with evre_guard_logout(), a login attempt that fails, or
 *     idle_logout_ms without a read or a write. In a session, a write that runs
 *     over the login register without being a login (a block write of the
 *     settings next to it) is refused, and the session goes on.
 *
 * Two answers for a refusal, so a host can tell "log in again" from "no such
 * register" and still learns nothing about a token:
 *
 *   - LOGIN_REQUIRED (13): no session is open. Log in and retry.
 *   - PERMISSION_DENIED (3): a login that failed, whatever the reason (a wrong
 *     token, a partial one, an attempt during a lockout); a write over the
 *     login register in a session; a login by broadcast; every request while
 *     the config is bad.
 *
 * The session belongs to the link, not to a host: while a session is open, a
 * broadcast from anyone on the link reaches the device, as a unicast write
 * would. Behind a gateway that puts several clients on one link, the gateway
 * authenticates its own clients.
 *
 * Time is counted, not compared. At every read, every write it takes up (not a
 * skipped broadcast login) and evre_guard_logged_in() the guard looks at the
 * clock: the time since its last look comes off the lockout and goes onto the
 * session's idle time. The clock has 64 bits, so it never wraps: the looks may
 * be any time apart, and any uint32_t limit works, 0xFFFFFFFF too. A value not
 * newer than the newest one the guard has seen counts as no time: a late read
 * (the main loop read the clock, then the decoder's interrupt ran the handlers
 * before it went on), or a clock set back, and then time stands still until
 * the clock catches up.
 *
 * The clock is the device's: now_ms returns milliseconds that count up and
 * never go back. On a 32-bit MCU a timer tick adds to a 64-bit counter, and
 * now_ms reads it without tearing: twice, until both reads agree, or with
 * interrupts masked. The value must be whole from the main loop and from the
 * decoder's context alike, so no reader may run in the middle of the tick's
 * add: give the tick a priority above the decoder's, or mask interrupts
 * around the add. now_ms is also called under EVRE_LOCK (by
 * evre_guard_logged_in): if it masks interrupts, it saves and restores the
 * mask, never a plain enable, and it takes no lock of its own. With a SysTick
 * hook:
 *
 *   static volatile uint64_t ms64;
 *   void tick_1ms(void) { ms64 = ms64 + 1; }           (called by the 1 ms SysTick, above the decoder's priority)
 *   static uint64_t now64(void) {
 *       uint64_t a, b;
 *       do { a = ms64; b = ms64; } while (a != b);     (a tick between two halves: read again)
 *       return a;
 *   }
 *
 * The handlers run where decodePacket runs (often an interrupt); everything
 * here is a few comparisons. evre_guard_logged_in(), evre_guard_logout() and
 * evre_guard_restore() change the handlers' state, so they follow addMsg's
 * rule (EVRe.h, EVRE_LOCK): call them where the decoder runs, or from a
 * context it preempts, such as the main loop, with EVRE_LOCK() and
 * EVRE_UNLOCK() defined for the build. They take that lock around the clock
 * read and the update, because an update changes several fields, and a
 * handler that ran in the middle of one would leave them out of step.
 *
 * Wiring, in the device:
 *
 *   static evre_guard_t guard;
 *   static uint8_t onRead(evre_base_t *d, uint16_t off, uint16_t cnt) { (void) d; return evre_guard_read(&guard, off, cnt); }
 *   static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
 *       return evre_guard_write(&guard, d, off, data, cnt); }       (d as the handler got it)
 *   ...
 *   if (evre_guard_init(&guard, &guard_config) != NO_ERROR) { a bad config: the guard refuses everything }
 *   evre_guard_restore(&guard, saved_failures, now64());   (optional)
 *   dev.READ_HANDLER = onRead;
 *   dev.WRITE_HANDLER = onWrite;
 *
 * The login register must lie in a writable range of the device bank (the
 * library checks that before it asks the guard), in a range of its own is
 * best; its memory never receives the token.
 */

#ifndef EVRE_GUARD_H
#define EVRE_GUARD_H

#include <stdint.h>

#include "EVRe.h"

#define EVRE_GUARD_TOKEN_MAX (32U)

typedef struct {
	uint16_t start;
	uint16_t len;
} evre_guard_span_t;

typedef struct {
	uint16_t login_addr;              /* the login register */
	uint8_t login_size;               /* its size, 1..EVRE_GUARD_TOKEN_MAX */
	const uint8_t *token;             /* login_size bytes (a shorter token zero-padded) */

	const evre_guard_span_t *open_reads; /* readable before a login, besides 0xA000..0xA003 */
	uint8_t n_open_reads;

	uint8_t free_attempts;            /* wrong tokens before the lockout starts, e.g. 3 */
	uint32_t lockout_ms;              /* the first lockout, e.g. 1000 */
	uint32_t lockout_max_ms;          /* the longest one, e.g. 60000; any uint32_t */
	uint32_t idle_logout_ms;          /* this long without a read or a write ends a session; 0: never; any uint32_t */

	uint64_t (*now_ms)(void);         /* milliseconds that count up and never go back; 64 bits, so it never wraps */
} evre_guard_config_t;

typedef struct {
	const evre_guard_config_t *cfg; /* nullptr after a bad config: every request refused */
	uint8_t logged_in;
	uint8_t failures;       /* wrong tokens since the last good login */
	uint8_t locked;         /* 1 while a lockout runs */
	uint32_t lock_left;     /* ms of it still to run */
	uint32_t idle_ms;       /* ms since the session's last read or write (0xFFFFFFFF at most) */
	uint64_t seen_ms;       /* the newest now_ms() the guard has seen; never goes back */
	uint32_t refused;       /* diagnostics: login attempts refused (wrong or locked out) */
} evre_guard_t;

/* NO_ERROR, or PERMISSION_DENIED for a bad config: a null cfg, token or
 * now_ms, login_size 0 or above EVRE_GUARD_TOKEN_MAX, or open_reads null with
 * n_open_reads above 0. A guard with a bad config fails closed: it refuses
 * every request with PERMISSION_DENIED. */
uint8_t evre_guard_init(evre_guard_t *guard, const evre_guard_config_t *cfg);

/* After evre_guard_init(), for a device that keeps the count of wrong tokens
 * in non-volatile memory: sets it, and starts the lockout that count implies,
 * so a reset hands out no free attempts. It starts at `now` (a now_ms()
 * value), or at the newest value the guard has seen, if that is later. */
void evre_guard_restore(evre_guard_t *guard, uint8_t failures, uint64_t now);

/* The two handlers' bodies: NO_ERROR, EVRE_HANDLED (a login, not stored),
 * LOGIN_REQUIRED or PERMISSION_DENIED. evre_guard_write takes the device as
 * the write handler got it: its RX_SLAVE_ID tells a broadcast. A null device
 * is refused (PERMISSION_DENIED). */
uint8_t evre_guard_read(evre_guard_t *guard, uint16_t offset, uint16_t count);
uint8_t evre_guard_write(evre_guard_t *guard, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count);

void evre_guard_logout(evre_guard_t *guard);
uint8_t evre_guard_logged_in(evre_guard_t *guard); /* 1 if a session is open (and not idle) */

#endif
