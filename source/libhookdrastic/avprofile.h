#ifndef DRASTIC_AVPROFILE_H
#define DRASTIC_AVPROFILE_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>

enum {
	AV_FRAME, AV_PREPARE, AV_ALPHA, AV_SHRINK, AV_UPLOAD, AV_DRAW, AV_PRESENT,
	AV_AUDIO, AV_AUDIO_GAP, AV_FRAME_CPU, AV_FRAME_OFFCPU,
	AV_OUTSIDE_PRESENT, AV_OUTSIDE_CPU, AV_OUTSIDE_OFFCPU,
	AV_PIXELS, AV_OVERLAY, AV_DEVICE, AV_INPUT, AV_DELAY, AV_REPORT_IO,
	AV_PIPELINE, AV_REMAINDER, AV_POWER_POLL, AV_METRIC_COUNT
};

typedef struct {
	Uint64 count, total, maximum, over_20ms;
} AVMetric;

typedef struct {
	uintptr_t caller;
	Uint64 count, ticks, maximum, zero_requests, requested_ms, effective_ms;
} AVDelaySite;

typedef struct { Uint64 start, end; } AVSpan;

typedef struct {
	int enabled, detail;
	Uint64 frequency, report_at, last_frame;
	SDL_threadID frame_thread;
	Uint64 last_cpu_ns, outside_at, outside_cpu_ns;
	AVSpan pipeline_spans[32];
	int pipeline_count;
	int have_cpu, outside_have_cpu, cpu_available;
	Uint64 thread_switches, cpu_clamps, pipeline_overflows;
	uintptr_t executable_base;
	int known_delay_sites;
	AVDelaySite delay_sites[8];
	AVMetric metrics[AV_METRIC_COUNT];
	SDL_AudioCallback callback;
	void *audio_userdata;
	int audio_rate, audio_frame_bytes;
	Uint64 last_audio; // Only the audio thread writes this field.
	const uint32_t *ring_read, *ring_write; // Validated bundled-binary layout only.
	Uint64 callbacks, ring_probes, ring_short, missing_s16, audio_late;
} AVProfile;

static inline int AVProfile_cpuNow(Uint64 *ns) {
#ifdef CLOCK_THREAD_CPUTIME_ID
	struct timespec value;
	int saved_errno = errno;
	int result = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
	errno = saved_errno;
	if (result == 0) {
		*ns = (Uint64)value.tv_sec * 1000000000u + value.tv_nsec;
		return 1;
	}
#else
	(void)ns;
#endif
	return 0;
}

static inline void AVProfile_init(AVProfile *profile, int enabled) {
	memset(profile, 0, sizeof(*profile));
	profile->enabled = enabled;
	profile->detail = enabled >= 2;
	if (enabled) {
		profile->frequency = SDL_GetPerformanceFrequency();
		profile->report_at = SDL_GetPerformanceCounter();
		if (profile->detail) {
			Uint64 ns;
			profile->cpu_available = AVProfile_cpuNow(&ns);
		}
	}
}

static inline Uint64 AVProfile_now(const AVProfile *profile) {
	return profile->enabled ? SDL_GetPerformanceCounter() : 0;
}

static inline int AVProfile_isFrameThread(const AVProfile *profile) {
	return profile->detail && __atomic_load_n(&profile->frame_thread, __ATOMIC_RELAXED) == SDL_ThreadID();
}

static inline void AVProfile_max(Uint64 *destination, Uint64 value) {
	Uint64 previous = __atomic_load_n(destination, __ATOMIC_RELAXED);
	while (previous < value && !__atomic_compare_exchange_n(destination, &previous,
		value, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

static inline void AVProfile_record(AVProfile *profile, int metric, Uint64 elapsed) {
	if (!profile->enabled) return;
	AVMetric *m = &profile->metrics[metric];
	__atomic_fetch_add(&m->count, 1, __ATOMIC_RELAXED);
	__atomic_fetch_add(&m->total, elapsed, __ATOMIC_RELAXED);
	AVProfile_max(&m->maximum, elapsed);
	if (elapsed > profile->frequency / 50)
		__atomic_fetch_add(&m->over_20ms, 1, __ATOMIC_RELAXED);
}

static inline void AVProfile_end(AVProfile *profile, int metric, Uint64 started) {
	if (!profile->enabled) return;
	Uint64 now = SDL_GetPerformanceCounter();
	AVProfile_record(profile, metric, now - started);
	// Keep spans, rather than adding screen averages: two simultaneously locked
	// images or nested rendering operations must not be counted twice.
	if (profile->detail && ((metric >= AV_PREPARE && metric <= AV_PRESENT) || metric == AV_PIXELS) &&
		AVProfile_isFrameThread(profile) && profile->last_frame) {
		if (profile->pipeline_count < 32)
			profile->pipeline_spans[profile->pipeline_count++] = (AVSpan){started, now};
		else profile->pipeline_count = 33;
	}
}

static inline void AVProfile_resetFrame(AVProfile *profile) {
	profile->last_frame = profile->outside_at = 0;
	profile->pipeline_count = 0;
	profile->have_cpu = profile->outside_have_cpu = 0;
}

static inline void AVProfile_cpuSpan(AVProfile *profile, int cpu_metric, int offcpu_metric,
		Uint64 wall, Uint64 ns) {
	Uint64 cpu = ns / 1000000000u * profile->frequency +
		(ns % 1000000000u) * profile->frequency / 1000000000u;
	// Separate clock reads and clock resolution can put CPU slightly above wall.
	if (cpu > wall) { cpu = wall; profile->cpu_clamps++; }
	AVProfile_record(profile, cpu_metric, cpu);
	AVProfile_record(profile, offcpu_metric, wall - cpu);
}

static inline void AVProfile_frame(AVProfile *profile) {
	if (!profile->enabled) return;
	Uint64 now = SDL_GetPerformanceCounter();
	Uint64 cpu_ns = 0;
	int have_cpu = 0;
	if (profile->detail) {
		SDL_threadID thread = SDL_ThreadID();
		SDL_threadID previous = __atomic_load_n(&profile->frame_thread, __ATOMIC_RELAXED);
		if (previous && previous != thread) {
			AVProfile_resetFrame(profile);
			profile->thread_switches++;
		}
		__atomic_store_n(&profile->frame_thread, thread, __ATOMIC_RELAXED);
		have_cpu = AVProfile_cpuNow(&cpu_ns);
		profile->cpu_available = have_cpu;
		if (profile->last_frame) {
			Uint64 wall = now - profile->last_frame;
			if (have_cpu && profile->have_cpu && cpu_ns >= profile->last_cpu_ns)
				AVProfile_cpuSpan(profile, AV_FRAME_CPU, AV_FRAME_OFFCPU, wall, cpu_ns - profile->last_cpu_ns);
			if (profile->pipeline_count <= 32) {
				for (int i=1; i<profile->pipeline_count; i++) {
					AVSpan span = profile->pipeline_spans[i];
					int j = i;
					while (j && profile->pipeline_spans[j-1].start > span.start) {
						profile->pipeline_spans[j] = profile->pipeline_spans[j-1]; j--;
					}
					profile->pipeline_spans[j] = span;
				}
				Uint64 total = 0, end = profile->last_frame;
				for (int i=0; i<profile->pipeline_count; i++) {
					AVSpan span = profile->pipeline_spans[i];
					if (span.start < end) span.start = end;
					if (span.end > now) span.end = now;
					if (span.end > span.start) { total += span.end - span.start; end = span.end; }
				}
				AVProfile_record(profile, AV_PIPELINE, total);
				AVProfile_record(profile, AV_REMAINDER, wall - total);
			} else profile->pipeline_overflows++;
		}
		if (profile->outside_at) {
			Uint64 wall = now - profile->outside_at;
			AVProfile_record(profile, AV_OUTSIDE_PRESENT, wall);
			if (have_cpu && profile->outside_have_cpu && cpu_ns >= profile->outside_cpu_ns)
				AVProfile_cpuSpan(profile, AV_OUTSIDE_CPU, AV_OUTSIDE_OFFCPU, wall, cpu_ns - profile->outside_cpu_ns);
		}
		profile->pipeline_count = 0;
		profile->outside_at = 0;
		profile->last_cpu_ns = cpu_ns;
		profile->have_cpu = have_cpu;
	}
	if (profile->last_frame) AVProfile_record(profile, AV_FRAME, now - profile->last_frame);
	profile->last_frame = now;
}

static inline void AVProfile_leavePresent(AVProfile *profile) {
	if (!AVProfile_isFrameThread(profile) || !profile->last_frame) return;
	profile->outside_at = SDL_GetPerformanceCounter();
	profile->outside_have_cpu = AVProfile_cpuNow(&profile->outside_cpu_ns);
}

static inline void AVProfile_delay(AVProfile *profile, uintptr_t caller,
		Uint32 requested, Uint32 effective, Uint64 started) {
	if (!AVProfile_isFrameThread(profile) || !profile->last_frame) return;
	Uint64 elapsed = SDL_GetPerformanceCounter() - started;
	AVProfile_record(profile, AV_DELAY, elapsed);
	uintptr_t offset = caller >= profile->executable_base ? caller - profile->executable_base : UINTPTR_MAX;
	if (offset >= 0x100000) offset = UINTPTR_MAX; // SDL/driver callers, grouped separately.
	AVDelaySite *site = &profile->delay_sites[7]; // Last slot aggregates overflow sites.
	for (int i=0; i<7; i++) {
		if (!profile->delay_sites[i].count || profile->delay_sites[i].caller == offset) {
			site = &profile->delay_sites[i]; site->caller = offset; break;
		}
	}
	site->count++;
	site->ticks += elapsed;
	if (elapsed > site->maximum) site->maximum = elapsed;
	site->zero_requests += requested == 0;
	site->requested_ms += requested;
	site->effective_ms += effective;
}

static inline void SDLCALL AVProfile_audioCallback(void *userdata, Uint8 *stream, int length) {
	AVProfile *profile = userdata;
	Uint64 started = SDL_GetPerformanceCounter();
	if (profile->last_audio) {
		Uint64 gap = started - profile->last_audio;
		AVProfile_record(profile, AV_AUDIO_GAP, gap);
		if (profile->audio_rate > 0 && profile->audio_frame_bytes > 0 && length > 0) {
			Uint64 expected = (Uint64)length * profile->frequency /
				((Uint64)profile->audio_rate * profile->audio_frame_bytes);
			if (gap > expected + expected/2)
				__atomic_fetch_add(&profile->audio_late, 1, __ATOMIC_RELAXED);
		}
	}
	profile->last_audio = started;
	__atomic_fetch_add(&profile->callbacks, 1, __ATOMIC_RELAXED);
	if (profile->ring_read && profile->ring_write && length > 0) {
		uint32_t read = __atomic_load_n(profile->ring_read, __ATOMIC_RELAXED);
		uint32_t write = __atomic_load_n(profile->ring_write, __ATOMIC_RELAXED);
		uint32_t available = (write - read) & 0xffffu;
		uint32_t requested = (uint32_t)length / sizeof(Sint16);
		__atomic_fetch_add(&profile->ring_probes, 1, __ATOMIC_RELAXED);
		if (available < requested) {
			__atomic_fetch_add(&profile->ring_short, 1, __ATOMIC_RELAXED);
			__atomic_fetch_add(&profile->missing_s16, requested - available, __ATOMIC_RELAXED);
		}
	}
	// No allocations, file I/O, logging or SDL audio locks on the callback thread.
	profile->callback(profile->audio_userdata, stream, length);
	AVProfile_end(profile, AV_AUDIO, started);
}

static inline void AVProfile_audioSpec(AVProfile *profile, const SDL_AudioSpec *spec) {
	profile->audio_rate = spec->freq;
	profile->audio_frame_bytes = (SDL_AUDIO_BITSIZE(spec->format) / 8) * spec->channels;
}

static inline Uint64 AVProfile_take(Uint64 *value) {
	return __atomic_exchange_n(value, 0, __ATOMIC_RELAXED);
}

static inline int AVProfile_report(AVProfile *profile, const char *mode) {
	if (!profile->enabled) return 0;
	Uint64 now = SDL_GetPerformanceCounter();
	if (now - profile->report_at < profile->frequency * 5) return 0;
	double seconds = (double)(now - profile->report_at) / profile->frequency;
	profile->report_at = now;
	SDL_Log("AV profile: mode=%s window=%.2fs callbacks=%llu ring_probes=%llu ring_short=%llu missing_s16=%llu late_callbacks=%llu",
		mode, seconds, (unsigned long long)AVProfile_take(&profile->callbacks),
		(unsigned long long)AVProfile_take(&profile->ring_probes),
		(unsigned long long)AVProfile_take(&profile->ring_short),
		(unsigned long long)AVProfile_take(&profile->missing_s16),
		(unsigned long long)AVProfile_take(&profile->audio_late));
	static const char *const names[AV_METRIC_COUNT] = {
		"frame_interval", "prepare", "alpha", "shrink", "upload", "draw", "present",
		"audio_callback", "audio_gap", "frame_cpu", "frame_offcpu",
		"outside_present", "outside_present_cpu", "outside_present_offcpu",
		"game_pixels", "overlay", "device_tick", "input", "sdl_delay", "profile_io",
		"screen_pipeline_per_frame", "outside_pipeline_per_frame", "power_poll"
	};
	for (int i=0; i<AV_METRIC_COUNT; i++) {
		AVMetric *m = &profile->metrics[i];
		Uint64 count = AVProfile_take(&m->count), total = AVProfile_take(&m->total);
		Uint64 maximum = AVProfile_take(&m->maximum), slow = AVProfile_take(&m->over_20ms);
		if (count) SDL_Log("AV profile: %s count=%llu avg_ms=%.3f max_ms=%.3f over_20ms=%llu",
			names[i], (unsigned long long)count, (double)total * 1000 / profile->frequency / count,
			(double)maximum * 1000 / profile->frequency, (unsigned long long)slow);
	}
	if (profile->detail) {
		SDL_Log("AV detail: cpu_clock=%s thread_switches=%llu cpu_clamps=%llu pipeline_overflows=%llu; alpha/upload/game_pixels are per screen; *_per_frame are per frame",
			profile->cpu_available ? "available" : "unavailable",
			(unsigned long long)profile->thread_switches, (unsigned long long)profile->cpu_clamps,
			(unsigned long long)profile->pipeline_overflows);
		profile->thread_switches = profile->cpu_clamps = profile->pipeline_overflows = 0;
		for (int i=0; i<8; i++) {
			AVDelaySite *site = &profile->delay_sites[i];
			if (!site->count) continue;
			const char *label = i == 7 ? "overflow" : site->caller == UINTPTR_MAX ? "external" : "unclassified";
			if (i != 7 && profile->known_delay_sites) {
				if (site->caller == 0x88678) label = "audio_sync";
				else if (site->caller == 0x8a0e8) label = "frame_pacing";
				else if (site->caller == 0xee38) label = "system_frame_sync_late";
			}
			SDL_Log("AV detail: SDL_Delay caller=0x%llx source=%s count=%llu zero_requests=%llu requested_ms=%llu effective_ms=%llu total_ms=%.3f max_ms=%.3f",
				(unsigned long long)site->caller, label, (unsigned long long)site->count,
				(unsigned long long)site->zero_requests, (unsigned long long)site->requested_ms,
				(unsigned long long)site->effective_ms, (double)site->ticks * 1000 / profile->frequency,
				(double)site->maximum * 1000 / profile->frequency);
			memset(site, 0, sizeof(*site));
		}
	}
	return 1;
}

#endif
