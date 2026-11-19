#include <SDL2/SDL.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static Uint64 ticks = 100;
static int log_calls;
static Uint64 fake_counter(void) { return ticks; }
static Uint64 fake_frequency(void) { return 1000000; }
static void fake_log(const char *format, ...) { (void)format; log_calls++; }
#define SDL_GetPerformanceCounter fake_counter
#define SDL_GetPerformanceFrequency fake_frequency
#define SDL_Log fake_log
#include "../avprofile.h"

static void audio_callback(void *userdata, Uint8 *stream, int length) {
	int *calls = userdata;
	(*calls)++;
	memset(stream, 0x5a, (size_t)length);
	ticks += 100;
}

int main(void) {
	AVProfile profile;
	AVProfile_init(&profile, 0);
	assert(AVProfile_now(&profile) == 0);
	AVProfile_record(&profile, AV_UPLOAD, 99999);
	AVProfile_frame(&profile);
	ticks += 10000000;
	AVProfile_report(&profile, "Off");
	assert(!log_calls && !profile.metrics[AV_UPLOAD].count && !profile.last_frame);

	AVProfile_init(&profile, 1);
	AVProfile_record(&profile, AV_UPLOAD, 1000);
	AVProfile_record(&profile, AV_UPLOAD, 21000);
	AVProfile_record(&profile, AV_UPLOAD, 500);
	assert(profile.metrics[AV_UPLOAD].count == 3 && profile.metrics[AV_UPLOAD].total == 22500);
	assert(profile.metrics[AV_UPLOAD].maximum == 21000 && profile.metrics[AV_UPLOAD].over_20ms == 1);
	Uint64 started = AVProfile_now(&profile);
	ticks += 2000;
	AVProfile_end(&profile, AV_ALPHA, started);
	assert(profile.metrics[AV_ALPHA].total == 2000);
	AVProfile_frame(&profile);
	ticks += 16700;
	AVProfile_frame(&profile);
	assert(profile.metrics[AV_FRAME].count == 1 && profile.metrics[AV_FRAME].total == 16700);

	int callbacks = 0;
	SDL_AudioSpec spec = {.freq=48000, .format=AUDIO_S16SYS, .channels=2, .samples=1024};
	AVProfile_audioSpec(&profile, &spec);
	assert(profile.audio_rate == 48000 && profile.audio_frame_bytes == 4);
	profile.callback = audio_callback;
	profile.audio_userdata = &callbacks;
	uint32_t read = 0, write = 1024;
	profile.ring_read = &read;
	profile.ring_write = &write;
	Uint8 output[4096];
	ticks += 100000;
	Uint64 first_callback = ticks;
	AVProfile_audioCallback(&profile, output, sizeof(output));
	assert(callbacks == 1 && profile.callbacks == 1 && !profile.audio_late);
	assert(profile.ring_probes == 1 && profile.ring_short == 1 && profile.missing_s16 == 1024);
	for (size_t i=0; i<sizeof(output); i++) assert(output[i] == 0x5a);
	assert(profile.metrics[AV_AUDIO].total == 100);

	// Ring counters wrap modulo 65536; this buffer has enough S16 samples.
	read = 65530; write = 2050;
	ticks = first_callback + 21333;
	AVProfile_audioCallback(&profile, output, sizeof(output));
	assert(profile.ring_probes == 2 && profile.ring_short == 1 && !profile.audio_late);
	assert(profile.metrics[AV_AUDIO_GAP].total == 21333);
	ticks += 40000;
	AVProfile_audioCallback(&profile, output, sizeof(output));
	assert(profile.audio_late == 1 && callbacks == 3);
	// Other binaries retain timing without dereferencing any private ring memory.
	profile.ring_read = profile.ring_write = NULL;
	ticks += 21000;
	AVProfile_audioCallback(&profile, output, sizeof(output));
	assert(profile.ring_probes == 3 && callbacks == 4 && !log_calls);

	ticks = profile.report_at + profile.frequency*5 - 1;
	AVProfile_report(&profile, "Hi-Res");
	assert(!log_calls);
	ticks++;
	AVProfile_report(&profile, "Hi-Res");
	assert(log_calls == 6); // Summary plus upload, alpha, frame, callback and gap.
	assert(!profile.callbacks && !profile.ring_probes && !profile.ring_short && !profile.missing_s16);
	assert(!profile.metrics[AV_UPLOAD].count && !profile.metrics[AV_UPLOAD].maximum);
	int previous_logs = log_calls;
	AVProfile_report(&profile, "Hi-Res");
	assert(log_calls == previous_logs);
	puts("avprofile: disabled mode, timing, maxima, callback passthrough, ring shortages/wrap, late callbacks and deferred reporting passed");
	return 0;
}
