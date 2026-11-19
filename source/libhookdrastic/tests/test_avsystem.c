#include <SDL2/SDL.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char logs[20000];
static void fake_log(const char *format, ...) {
	size_t length = strlen(logs);
	va_list ap;
	va_start(ap, format);
	int written = vsnprintf(logs + length, sizeof(logs) - length, format, ap);
	va_end(ap);
	assert(written >= 0 && length + (size_t)written + 1 < sizeof(logs));
	strcat(logs, "\n");
}
#define SDL_Log fake_log
#include "../avsystem.h"

static void make_dir(const char *root, const char *part) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", root, part);
	assert(mkdir(path, 0700) == 0);
}

static void write_file(const char *root, const char *part, const char *text) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", root, part);
	FILE *f = fopen(path, "w");
	assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}

static void thread_stat(char *out, size_t size, int tid, const char *name,
		unsigned long long user, unsigned long long system, unsigned long long start) {
	int written = snprintf(out, size, "%d (%s) R", tid, name);
	assert(written > 0);
	for (int field=4; field<=22; field++) {
		size_t length = strlen(out);
		unsigned long long value = field == 14 ? user : field == 15 ? system : field == 22 ? start : 0;
		written = snprintf(out + length, size - length, " %llu", value);
		assert(written > 0 && length + (size_t)written < size);
	}
}

int main(void) {
	char stat[1024];
	AVSystemThread value;
	thread_stat(stat, sizeof(stat), 101, "render (worker)", 100, 7, 1234);
	assert(AVSystem_parseStat(stat, &value));
	assert(value.tid == 101 && value.ticks == 107 && value.start == 1234);
	assert(strcmp(value.name, "render (worker)") == 0);
	assert(!AVSystem_parseStat("bad", &value));
	assert(!AVSystem_parseStat("101 (broken) R 0", &value));
	assert(!AVSystem_parseStat("0 (bad) R", &value));

	char root[] = "/tmp/drastic-avsystem-test.XXXXXX";
	assert(mkdtemp(root));
	const char *dirs[] = {"task", "task/101", "task/202", "devices", "devices/system", "devices/system/cpu",
		"devices/system/cpu/cpu0", "devices/system/cpu/cpu0/cpufreq", "class", "class/thermal", "class/thermal/thermal_zone0"};
	for (size_t i=0; i<sizeof(dirs)/sizeof(*dirs); i++) make_dir(root, dirs[i]);
	thread_stat(stat, sizeof(stat), 101, "drastic main", 100, 0, 1234);
	write_file(root, "task/101/stat", stat);
	thread_stat(stat, sizeof(stat), 202, "3d worker", 200, 0, 1235);
	write_file(root, "task/202/stat", stat);
	write_file(root, "task/101/schedstat", "1000000000 200000000 50\n");
	write_file(root, "devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "1800000\n");
	write_file(root, "devices/system/cpu/cpu0/cpufreq/scaling_governor", "userspace\n");
	write_file(root, "class/thermal/thermal_zone0/type", "cpu\n");
	write_file(root, "class/thermal/thermal_zone0/temp", "75000\n");
	char tasks[512];
	snprintf(tasks, sizeof(tasks), "%s/task", root);
	AVSystem state = {.main_tid = 101};
	AVSystem_sample(&state, tasks, root, 10, 100);
	assert(state.count == 2 && strstr(logs, "cpu_pct=-1.0"));
	assert(strstr(logs, "cpu0_cur_khz=1800000 governor=userspace"));
	assert(strstr(logs, "temperature_c=75.000"));
	logs[0] = 0;
	thread_stat(stat, sizeof(stat), 101, "drastic main", 190, 10, 1234);
	write_file(root, "task/101/stat", stat);
	thread_stat(stat, sizeof(stat), 202, "3d worker", 400, 0, 1235);
	write_file(root, "task/202/stat", stat);
	write_file(root, "task/101/schedstat", "2000000000 400000000 100\n");
	AVSystem_sample(&state, tasks, root, 15, 100);
	assert(strstr(logs, "role=main name=drastic main cpu_pct=20.0 sched_run_ms=1000.000 runqueue_ms=200.000"));
	assert(strstr(logs, "role=worker name=3d worker cpu_pct=40.0 sched_run_ms=-1.000"));
	logs[0] = 0;
	// Reused TIDs must establish a new baseline, not produce a negative delta.
	thread_stat(stat, sizeof(stat), 202, "replacement", 1, 0, 9999);
	write_file(root, "task/202/stat", stat);
	AVSystem_sample(&state, tasks, root, 20, 100);
	assert(strstr(logs, "name=replacement cpu_pct=-1.0"));
	AVSystem_sample(&state, "/nonexistent/drastic/task", "/nonexistent/drastic/sys", 25, 100);
	assert(strstr(logs, "per-thread CPU statistics unavailable"));
	assert(strstr(logs, "thermal readings unavailable"));
	assert(strstr(logs, "cpu0_cur_khz=unavailable"));
	const char *files[] = {"task/101/stat", "task/101/schedstat", "task/202/stat",
		"devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "devices/system/cpu/cpu0/cpufreq/scaling_governor",
		"class/thermal/thermal_zone0/type", "class/thermal/thermal_zone0/temp"};
	char path[512];
	for (size_t i=0; i<sizeof(files)/sizeof(*files); i++) {
		snprintf(path, sizeof(path), "%s/%s", root, files[i]); assert(unlink(path) == 0);
	}
	for (size_t i=sizeof(dirs)/sizeof(*dirs); i>0; i--) {
		snprintf(path, sizeof(path), "%s/%s", root, dirs[i-1]); assert(rmdir(path) == 0);
	}
	assert(rmdir(root) == 0);
	puts("avsystem: Linux task parsing, CPU/scheduler deltas, TID reuse, frequency/temperature and unavailable files passed");
	return 0;
}
