#include <SDL2/SDL.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static Uint64 ticks = 100, cpu_ns = 1000000000;
static int cpu_calls, cpu_failure;
static SDL_threadID thread_id = 1;
static char logs[16000];
static Uint64 fake_counter(void) { return ticks; }
static Uint64 fake_frequency(void) { return 1000000; }
static SDL_threadID fake_thread(void) { return thread_id; }
static int fake_clock(clockid_t id, struct timespec *out) {
	assert(id == CLOCK_THREAD_CPUTIME_ID);
	cpu_calls++;
	if (cpu_failure) { errno = EINVAL; return -1; }
	out->tv_sec = (time_t)(cpu_ns / 1000000000);
	out->tv_nsec = (long)(cpu_ns % 1000000000);
	return 0;
}
static void fake_log(const char *format, ...) {
	size_t length = strlen(logs);
	va_list ap;
	va_start(ap, format);
	int written = vsnprintf(logs + length, sizeof(logs) - length, format, ap);
	va_end(ap);
	assert(written >= 0 && (size_t)written + length + 1 < sizeof(logs));
	strcat(logs, "\n");
}
#define SDL_GetPerformanceCounter fake_counter
#define SDL_GetPerformanceFrequency fake_frequency
#define SDL_ThreadID fake_thread
#define clock_gettime fake_clock
#define SDL_Log fake_log
#include "../avprofile.h"

static void audio(void *userdata, Uint8 *stream, int length) {
	(void)userdata;
	memset(stream, 0x5a, (size_t)length);
	ticks += 100;
}

int main(void) {
	AVProfile p;
	AVProfile_init(&p, 0);
	AVProfile_frame(&p);
	AVProfile_leavePresent(&p);
	AVProfile_delay(&p, 0, 0, 0, ticks);
	assert(!cpu_calls && !p.detail && !AVProfile_report(&p, "Off"));
	AVProfile_init(&p, 1);
	AVProfile_frame(&p);
	AVProfile_leavePresent(&p);
	assert(!cpu_calls && !p.detail);

	errno = EDOM;
	AVProfile_init(&p, 2);
	assert(p.detail && p.cpu_available && errno == EDOM);
	p.executable_base = 0x100000;
	p.known_delay_sites = 1;
	AVProfile_frame(&p);
	ticks = 1100; AVProfile_end(&p, AV_DRAW, 100);
	ticks = 2100; AVProfile_end(&p, AV_PRESENT, 1100);
	cpu_ns += 1000000;
	AVProfile_leavePresent(&p);
	// Two locked image spans overlap, and contain a nested alpha span.
	// Per-screen metrics count both, but the per-frame union counts time once.
	ticks = 6100; AVProfile_end(&p, AV_PIXELS, 4100);
	ticks = 6200; AVProfile_end(&p, AV_ALPHA, 6100);
	ticks = 6600; AVProfile_end(&p, AV_PIXELS, 4600);
	ticks = 6700; AVProfile_end(&p, AV_ALPHA, 6600);
	ticks = 8700; AVProfile_end(&p, AV_UPLOAD, 6700);
	ticks = 9700; AVProfile_end(&p, AV_UPLOAD, 8700);
	ticks = 12000; AVProfile_delay(&p, p.executable_base + 0x88678, 0, 0, 11000);
	ticks = 17100; cpu_ns += 10000000;
	AVProfile_frame(&p);
	assert(p.metrics[AV_FRAME].total == 17000);
	assert(p.metrics[AV_FRAME_CPU].total == 11000 && p.metrics[AV_FRAME_OFFCPU].total == 6000);
	assert(p.metrics[AV_OUTSIDE_PRESENT].total == 15000);
	assert(p.metrics[AV_OUTSIDE_CPU].total == 10000 && p.metrics[AV_OUTSIDE_OFFCPU].total == 5000);
	assert(p.metrics[AV_PIPELINE].total == 7600 && p.metrics[AV_REMAINDER].total == 9400);
	assert(p.metrics[AV_PIXELS].total == 4000 && p.metrics[AV_PIXELS].count == 2);
	assert(p.metrics[AV_UPLOAD].total == 3000);
	assert(p.delay_sites[0].zero_requests == 1 && p.delay_sites[0].ticks == 1000);
	ticks += 1000;
	AVProfile_delay(&p, p.executable_base + 0x8a0e8, 20, 16, ticks - 1000);
	AVProfile_delay(&p, p.executable_base + 0xee38, 0, 0, ticks);
	p.callback = audio;
	Uint8 output[32];
	int previous_cpu_calls = cpu_calls;
	AVProfile_audioCallback(&p, output, sizeof(output));
	assert(cpu_calls == previous_cpu_calls); // No extra CPU-clock work on the audio thread.
	for (size_t i=0; i<sizeof(output); i++) assert(output[i] == 0x5a);
	ticks = p.report_at + 5000000;
	assert(AVProfile_report(&p, "Hi-Res") == 1);
	assert(strstr(logs, "source=audio_sync") && strstr(logs, "source=frame_pacing"));
	assert(strstr(logs, "source=system_frame_sync_late"));
	assert(strstr(logs, "screen_pipeline_per_frame count=1 avg_ms=7.600"));
	assert(!p.delay_sites[0].count && !p.metrics[AV_FRAME_CPU].count);
	assert(AVProfile_report(&p, "Hi-Res") == 0);

	AVProfile_resetFrame(&p);
	assert(!p.last_frame && !p.outside_at && !p.have_cpu);
	AVProfile_frame(&p);
	ticks += 1000; cpu_ns += 2000000;
	AVProfile_frame(&p);
	assert(p.cpu_clamps == 1 && p.metrics[AV_FRAME_OFFCPU].total == 0);
	Uint64 frames = p.metrics[AV_FRAME].count;
	thread_id++;
	ticks += 100000;
	AVProfile_frame(&p);
	assert(p.thread_switches == 1 && p.metrics[AV_FRAME].count == frames);
	for (int i=0; i<33; i++) AVProfile_end(&p, AV_UPLOAD, ticks);
	ticks += 1000;
	AVProfile_frame(&p);
	assert(p.pipeline_overflows == 1);

	cpu_failure = 1;
	AVProfile_init(&p, 2);
	assert(!p.cpu_available && errno == EDOM);
	AVProfile_frame(&p);
	ticks += 1000;
	AVProfile_frame(&p);
	assert(p.metrics[AV_FRAME].count == 1 && !p.metrics[AV_FRAME_CPU].count);
	AVProfile_delay(&p, 10, 0, 0, ticks);
	assert(!p.known_delay_sites);
	puts("avprofile detail: CPU/off-CPU spans, aligned frame accounting, overlapping locks, delay callsites, thread changes, clock failure, overflow and audio passthrough passed");
	return 0;
}
