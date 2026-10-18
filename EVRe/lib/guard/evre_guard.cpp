/* SPDX-License-Identifier: Apache-2.0 */
/*
 * evre_guard.cpp: EVRe Guard, the login. See evre_guard.h.
 */

#include "evre_guard.h"

static const uint32_t MS_MAX = 0xFFFFFFFFUL;

static bool inside(uint32_t offset, uint32_t count, uint32_t start, uint32_t len) {
	return offset >= start && offset + count <= start + len;
}

/* A guard that can be used: its config passed evre_guard_init(). */
static bool usable(const evre_guard_t *guard) {
	return guard != nullptr && guard->cfg != nullptr;
}

/* lockout_ms, doubled for every failure past free_attempts, lockout_max_ms at
 * most. Saturated, not multiplied: doubling a uint32_t wraps to 0 after 32
 * failures. */
static uint32_t lockoutFor(const evre_guard_config_t *cfg, uint8_t failures) {
	const uint8_t doublings = (uint8_t) (failures - cfg->free_attempts);
	if (doublings >= 32U || cfg->lockout_ms > (cfg->lockout_max_ms >> doublings)) {
		return cfg->lockout_max_ms;
	}
	return cfg->lockout_ms << doublings;
}

static void startLockout(evre_guard_t *guard) {
	guard->lock_left = lockoutFor(guard->cfg, guard->failures);
	guard->locked = 1;
}

/* Every read, every write the guard takes up, evre_guard_logged_in() and
 * evre_guard_restore() look at the clock here. The step since the newest value
 * seen comes off the lockout and goes onto the idle time, both saturated. A
 * value not newer is a step of 0, so nothing a late read or a clock set back
 * hands in can end a limit early; a step of 0 still ends a lockout of 0 ms. */
static void look(evre_guard_t *guard, uint64_t now) {
	uint64_t step = 0;
	if (now > guard->seen_ms) {
		step = now - guard->seen_ms;
		guard->seen_ms = now;
	}
	if (guard->locked) {
		if (step >= guard->lock_left) {
			guard->locked = 0;
			guard->lock_left = 0;
		} else {
			guard->lock_left -= (uint32_t) step;
		}
	}
	if (guard->logged_in) {
		guard->idle_ms = (step >= MS_MAX - guard->idle_ms) ? MS_MAX : guard->idle_ms + (uint32_t) step;
		if (guard->cfg->idle_logout_ms != 0 && guard->idle_ms >= guard->cfg->idle_logout_ms) {
			guard->logged_in = 0;
		}
	}
}

uint8_t evre_guard_init(evre_guard_t *guard, const evre_guard_config_t *cfg) {
	if (guard == nullptr) {
		return PERMISSION_DENIED;
	}
	guard->cfg = nullptr;
	guard->logged_in = 0;
	guard->failures = 0;
	guard->locked = 0;
	guard->lock_left = 0;
	guard->idle_ms = 0;
	guard->seen_ms = 0;
	guard->refused = 0;
	/* Each of these would crash in the decoder's interrupt, read past the
	 * token, or let a write with no token log in: refuse the config instead. */
	const bool valid = cfg != nullptr && cfg->token != nullptr && cfg->now_ms != nullptr && cfg->login_size != 0
			&& cfg->login_size <= EVRE_GUARD_TOKEN_MAX && (cfg->n_open_reads == 0 || cfg->open_reads != nullptr);
	if (!valid) {
		return PERMISSION_DENIED; /* cfg stays nullptr: fails closed */
	}
	guard->cfg = cfg;
	return NO_ERROR;
}

/* The three calls below may come from the main loop. The lock keeps the
 * decoder's handlers out from the clock read to the last update. */

void evre_guard_restore(evre_guard_t *guard, uint8_t failures, uint64_t now) {
	if (!usable(guard)) {
		return;
	}
	EVRE_LOCK();
	look(guard, now);
	guard->failures = failures;
	guard->locked = 0;
	guard->lock_left = 0;
	if (failures != 0 && failures >= guard->cfg->free_attempts) {
		startLockout(guard);
	}
	EVRE_UNLOCK();
}

void evre_guard_logout(evre_guard_t *guard) {
	if (guard == nullptr) {
		return;
	}
	EVRE_LOCK();
	guard->logged_in = 0;
	EVRE_UNLOCK();
}

uint8_t evre_guard_logged_in(evre_guard_t *guard) {
	if (!usable(guard)) {
		return 0;
	}
	EVRE_LOCK();
	look(guard, guard->cfg->now_ms());
	const uint8_t in = guard->logged_in;
	EVRE_UNLOCK();
	return in;
}

uint8_t evre_guard_read(evre_guard_t *guard, uint16_t offset, uint16_t count) {
	if (!usable(guard)) {
		return PERMISSION_DENIED;
	}
	const evre_guard_config_t *cfg = guard->cfg;
	look(guard, cfg->now_ms());
	if (guard->logged_in) {
		guard->idle_ms = 0;
		return NO_ERROR;
	}
	if (inside(offset, count, DEVICE_ID_BASE_ADDR, 4)) {
		return NO_ERROR; /* DEVICE_ID and STATUS: a host must know what it talks to */
	}
	for (uint8_t ind = 0; ind < cfg->n_open_reads; ++ind) {
		if (inside(offset, count, cfg->open_reads[ind].start, cfg->open_reads[ind].len)) {
			return NO_ERROR;
		}
	}
	return LOGIN_REQUIRED;
}

/* A login attempt: the lockout first, then the token, in constant time.
 * exact: login_size bytes at login_addr; anything else is a wrong token. */
static uint8_t login(evre_guard_t *guard, const uint8_t *data, bool exact) {
	const evre_guard_config_t *cfg = guard->cfg;
	guard->logged_in = 0; /* any attempt ends the session it is made in */
	if (guard->locked) {
		++guard->refused;
		return PERMISSION_DENIED; /* not even compared */
	}
	uint8_t diff = exact ? 0U : 1U;
	for (uint16_t ind = 0; ind < cfg->login_size; ++ind) {
		diff |= (uint8_t) ((exact ? data[ind] : 0U) ^ cfg->token[ind]);
	}
	if (diff == 0) {
		guard->failures = 0;
		guard->logged_in = 1;
		guard->idle_ms = 0;
		return EVRE_HANDLED; /* accepted, and the token is not stored */
	}
	++guard->refused;
	if (guard->failures < 0xFF) {
		++guard->failures;
	}
	if (guard->failures >= cfg->free_attempts) {
		startLockout(guard);
	}
	return PERMISSION_DENIED;
}

uint8_t evre_guard_write(evre_guard_t *guard, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count) {
	if (!usable(guard) || device == nullptr) {
		return PERMISSION_DENIED;
	}
	const evre_guard_config_t *cfg = guard->cfg;
	const uint32_t end = (uint32_t) offset + count;
	/* A write of no bytes touches no register: no attempt, wherever it points. */
	const bool overLogin = count != 0 && offset < (uint32_t) cfg->login_addr + cfg->login_size && end > cfg->login_addr;
	/* Every slave heard it, and none answers: a login by broadcast would be a
	 * guess nobody sees fail, or one frame that locks out or logs out every
	 * device. Skipped before the guard looks at anything, so it changes
	 * nothing at all. */
	if (overLogin && device->RX_SLAVE_ID == BROADCAST_ID) {
		return PERMISSION_DENIED;
	}
	look(guard, cfg->now_ms());
	if (overLogin) {
		const bool exact = offset == cfg->login_addr && count == cfg->login_size;
		/* In a session, a write that only runs over the login register is no
		 * attempt: refused, with the session and the count left alone. Without
		 * a session it is one, and a partial token fails. */
		if (guard->logged_in && !exact) {
			guard->idle_ms = 0;
			return PERMISSION_DENIED;
		}
		return login(guard, data, exact);
	}
	if (guard->logged_in) {
		guard->idle_ms = 0;
		return NO_ERROR;
	}
	return LOGIN_REQUIRED;
}
