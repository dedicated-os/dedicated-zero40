#!/bin/sh

SDCARD_PATH=/mnt/SDCARD
DIR=$SDCARD_PATH/system/drastic
LIB=$SDCARD_PATH/system/lib

cd "$DIR" || exit 1
export HOME="$DIR"

# Keep every launch, including automatic restarts, accessible on the SD card.
LOG_DIR="$SDCARD_PATH/userdata/logs"
LOG_FILE=/tmp/drastic.txt
if mkdir -p "$LOG_DIR"; then
	log_prefix="$LOG_DIR/drastic-$(date +%Y%m%d-%H%M%S)-$$"
	log_candidate="$log_prefix.log"
	log_index=0
	while :; do
		if (set -C; : > "$log_candidate") 2>/dev/null; then
			LOG_FILE="$log_candidate"
			break
		fi
		# An existing file gets a suffix; a write failure keeps the /tmp fallback.
		[ -e "$log_candidate" ] || break
		[ -w "$log_candidate" ] || break
		log_index=$((log_index + 1))
		log_candidate="$log_prefix-$log_index.log"
	done
fi

LD_PRELOAD="$LIB/libhookdrastic.so" ./drastic > "$LOG_FILE" 2>&1
