#include <assert.h>
#include <stdio.h>
#include "../powerstatus.h"

static int reads, battery = 75, charging;
static int read_int(int fd) {
	reads++;
	if (fd < 0) return 0; // Existing getInt behavior for unavailable descriptors.
	assert(fd == 101 || fd == 102);
	return fd == 101 ? battery : charging;
}

int main(void) {
	PowerStatus status = {0};
	assert(PowerStatus_due(&status, 0));
	assert(PowerStatus_poll(&status, 0, 101, 102, read_int));
	assert(reads == 2 && status.battery == 75 && !status.charging);
	battery = 9; charging = 1;
	// Even a very high game frame rate must not keep reading the devices.
	for (Uint32 now=0; now<1000; now++)
		assert(!PowerStatus_poll(&status, now, 101, 102, read_int));
	assert(reads == 2 && status.battery == 75 && !status.charging);
	assert(PowerStatus_poll(&status, 1000, 101, 102, read_int));
	assert(reads == 4 && status.battery == 9 && status.charging);
	charging = 0;
	assert(!PowerStatus_poll(&status, 1999, 101, 102, read_int));
	assert(PowerStatus_poll(&status, 2000, 101, 102, read_int));
	assert(status.battery <= 10 && !status.charging); // Low-battery warning updates.
	assert(reads == 6);
	assert(PowerStatus_poll(&status, 50000, 101, 102, read_int));
	assert(reads == 8); // Resuming after a long pause polls once, without catch-up reads.
	status = (PowerStatus){0};
	assert(PowerStatus_poll(&status, (Uint32)-1 - 500, 101, 102, read_int));
	assert(!PowerStatus_poll(&status, 498, 101, 102, read_int));
	assert(PowerStatus_poll(&status, 499, 101, 102, read_int));
	assert(reads == 12); // SDL's 32-bit millisecond counter can wrap.
	status = (PowerStatus){0};
	assert(PowerStatus_poll(&status, 0, -1, -2, read_int));
	assert(!status.battery && !status.charging && status.valid);
	assert(!PowerStatus_poll(&status, 1, -1, -2, read_int));
	assert(reads == 14);
	puts("powerstatus: initial read, per-second polling, charge/low-battery changes, resume, counter wrap and unavailable descriptors passed");
	return 0;
}
