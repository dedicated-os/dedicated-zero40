#ifndef DRASTIC_AVSYSTEM_H
#define DRASTIC_AVSYSTEM_H

#include <SDL2/SDL.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#define AV_SYSTEM_THREADS 32

typedef struct {
	long tid;
	char name[32];
	Uint64 ticks, start, run_ns, runqueue_ns;
	int have_schedstat;
} AVSystemThread;

typedef struct {
	AVSystemThread threads[AV_SYSTEM_THREADS];
	int count;
	double sampled_at;
	long main_tid;
} AVSystem;

// comm may contain spaces and parentheses; numeric field 3 follows the last ')'.
static inline int AVSystem_parseStat(const char *line, AVSystemThread *out) {
	const char *open = strchr(line, '('), *close = strrchr(line, ')');
	if (!open || !close || close <= open || close[1] != ' ') return 0;
	AVSystemThread value = {0};
	char *end;
	value.tid = strtol(line, &end, 10);
	if (end == line || value.tid <= 0 || value.tid > INT_MAX || end > open) return 0;
	size_t length = (size_t)(close - open - 1);
	if (length >= sizeof(value.name)) length = sizeof(value.name) - 1;
	memcpy(value.name, open + 1, length);
	for (size_t i=0; i<length; i++)
		if ((unsigned char)value.name[i] < 32 || value.name[i] == 127) value.name[i] = '?';
	const char *at = close + 2;
	Uint64 user = 0, system = 0;
	for (int field=3; field<=22; field++) {
		while (*at == ' ') at++;
		if (!*at || *at == '\n') return 0;
		if (field == 14 || field == 15 || field == 22) {
			if (*at < '0' || *at > '9') return 0;
			Uint64 number = strtoull(at, &end, 10);
			if (end == at || (*end && *end != ' ' && *end != '\n')) return 0;
			if (field == 14) user = number;
			else if (field == 15) system = number;
			else value.start = number;
		}
		while (*at && *at != ' ' && *at != '\n') at++;
	}
	if (user > ~(Uint64)0 - system) return 0;
	value.ticks = user + system;
	*out = value;
	return 1;
}

static inline int AVSystem_readLine(const char *path, char *out, size_t size) {
	FILE *file = fopen(path, "r");
	if (!file) return 0;
	int ok = fgets(out, (int)size, file) != NULL;
	fclose(file);
	if (ok) out[strcspn(out, "\r\n")] = 0;
	return ok;
}

// Called only by the render thread at the five-second reporting boundary.
// Paths are parameters so the Linux parsers/collector can be tested on a host.
static inline void AVSystem_sample(AVSystem *state, const char *task_dir, const char *sys_dir,
		double now, long ticks_per_second) {
	AVSystemThread current[AV_SYSTEM_THREADS];
	int count = 0, truncated = 0;
	double seconds = now - state->sampled_at;
	DIR *dir = opendir(task_dir);
	char path[512], line[1024];
	if (dir) {
		struct dirent *entry;
		while ((entry = readdir(dir))) {
			char *end;
			long tid = strtol(entry->d_name, &end, 10);
			if (!*entry->d_name || *end || tid <= 0 || tid > INT_MAX) continue;
			if (count == AV_SYSTEM_THREADS) { truncated++; continue; }
			snprintf(path, sizeof(path), "%s/%ld/stat", task_dir, tid);
			AVSystemThread value;
			if (!AVSystem_readLine(path, line, sizeof(line)) || !AVSystem_parseStat(line, &value)) continue;
			snprintf(path, sizeof(path), "%s/%ld/schedstat", task_dir, tid);
			if (AVSystem_readLine(path, line, sizeof(line))) {
				unsigned long long run, wait;
				if (sscanf(line, "%llu %llu", &run, &wait) == 2) {
					value.run_ns = run; value.runqueue_ns = wait; value.have_schedstat = 1;
				}
			}
			const AVSystemThread *previous = NULL;
			for (int i=0; i<state->count; i++) {
				if (state->threads[i].tid == value.tid && state->threads[i].start == value.start) {
					previous = &state->threads[i]; break;
				}
			}
			double cpu = -1, run_ms = -1, queue_ms = -1;
			if (previous && seconds > 0 && ticks_per_second > 0 && value.ticks >= previous->ticks)
				cpu = (double)(value.ticks - previous->ticks) * 100 / ticks_per_second / seconds;
			if (previous && seconds > 0 && value.have_schedstat && previous->have_schedstat &&
				value.run_ns >= previous->run_ns && value.runqueue_ns >= previous->runqueue_ns) {
				run_ms = (double)(value.run_ns - previous->run_ns) / 1000000;
				queue_ms = (double)(value.runqueue_ns - previous->runqueue_ns) / 1000000;
			}
			SDL_Log("AV system: thread tid=%ld role=%s name=%s cpu_pct=%.1f sched_run_ms=%.3f runqueue_ms=%.3f sample_s=%.3f",
				value.tid, value.tid == state->main_tid ? "main" : "worker", value.name,
				cpu, run_ms, queue_ms, state->sampled_at ? seconds : 0);
			current[count++] = value;
		}
		closedir(dir);
		memcpy(state->threads, current, (size_t)count * sizeof(*current));
		state->count = count;
		state->sampled_at = now;
		SDL_Log("AV system: threads=%d truncated=%d; cpu_pct=100 means one core; negative values mean unavailable/baseline", count, truncated);
	} else SDL_Log("AV system: per-thread CPU statistics unavailable");

	char frequency[32] = "unavailable", governor[32] = "unavailable";
	snprintf(path, sizeof(path), "%s/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", sys_dir);
	AVSystem_readLine(path, frequency, sizeof(frequency));
	snprintf(path, sizeof(path), "%s/devices/system/cpu/cpu0/cpufreq/scaling_governor", sys_dir);
	AVSystem_readLine(path, governor, sizeof(governor));
	SDL_Log("AV system: cpu0_cur_khz=%s governor=%s (instantaneous sample)", frequency, governor);
	int temperatures = 0;
	for (int i=0; i<16; i++) {
		snprintf(path, sizeof(path), "%s/class/thermal/thermal_zone%d/temp", sys_dir, i);
		if (!AVSystem_readLine(path, line, sizeof(line))) continue;
		char *end;
		long temperature = strtol(line, &end, 10);
		if (end == line || *end || temperature < -100000 || temperature > 250000) continue;
		char type[64] = "unknown";
		snprintf(path, sizeof(path), "%s/class/thermal/thermal_zone%d/type", sys_dir, i);
		AVSystem_readLine(path, type, sizeof(type));
		SDL_Log("AV system: thermal_zone=%d type=%s temperature_c=%.3f", i, type, temperature / 1000.0);
		temperatures++;
	}
	if (!temperatures) SDL_Log("AV system: thermal readings unavailable");
}

static inline void AVSystem_setMainThread(AVSystem *state) {
#if defined(__linux__) && defined(SYS_gettid)
	state->main_tid = syscall(SYS_gettid);
#else
	(void)state;
#endif
}

static inline void AVSystem_report(AVSystem *state) {
#if defined(__linux__)
	double now = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
	AVSystem_sample(state, "/proc/self/task", "/sys", now, sysconf(_SC_CLK_TCK));
#else
	(void)state;
	SDL_Log("AV system: Linux /proc and sysfs statistics unavailable on this platform");
#endif
}

#endif
