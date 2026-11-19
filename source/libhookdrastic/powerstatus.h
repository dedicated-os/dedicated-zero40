#ifndef DRASTIC_POWERSTATUS_H
#define DRASTIC_POWERSTATUS_H

#include <SDL2/SDL_stdinc.h>

#define POWER_STATUS_POLL_MS 1000u

typedef struct {
	int battery, charging, valid;
	Uint32 sampled_at;
} PowerStatus;

static inline int PowerStatus_due(const PowerStatus *status, Uint32 now) {
	return !status->valid || (Uint32)(now - status->sampled_at) >= POWER_STATUS_POLL_MS;
}

static inline int PowerStatus_poll(PowerStatus *status, Uint32 now,
		int battery_fd, int usb_fd, int (*read_int)(int)) {
	if (!PowerStatus_due(status, now)) return 0;
	status->battery = read_int(battery_fd);
	status->charging = read_int(usb_fd) != 0;
	status->sampled_at = now;
	status->valid = 1;
	return 1;
}

#endif
