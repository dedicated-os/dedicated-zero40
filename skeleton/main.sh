#!/bin/sh

SDCARD_PATH=/mnt/SDCARD
export PATH=$SDCARD_PATH/system/bin:$PATH
export LD_LIBRARY_PATH=$SDCARD_PATH/system/lib:$LD_LIBRARY_PATH

# update
if [ -f $SDCARD_PATH/system.zip ]; then
	cd $SDCARD_PATH
	rm -rf system
	unzip -q system.zip
	rm -f system.zip
fi

# loop
$SDCARD_PATH/system/loop.sh