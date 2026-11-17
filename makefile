.PHONY: build package-fonts

BUILD_HASH:=$(shell git rev-parse --short HEAD)
RELEASE_TIME:=$(shell TZ=GMT date +%Y%m%d)
RELEASE_BETA=jp+
RELEASE_BASE=dedicated-zero40-$(RELEASE_BETA)$(RELEASE_TIME)
RELEASE_DOT:=$(shell find ./releases/. -regex ".*/${RELEASE_BASE}-[0-9]+\.zip" 2>/dev/null | wc -l | sed 's/ //g')
RELEASE_NAME=$(RELEASE_BASE)-$(RELEASE_DOT)

all: setup build package done
	
setup:
	# make sure we're running in an input device
	tty -s 
	
	# ready fresh build
	rm -rf ./build
	mkdir -p ./releases
	cp -R ./skeleton ./build
	
	# remove authoring detritus
	cd ./build && find . -type f -name '.keep' -delete

build:
	# build
	cd source/pp && make
	cd source/hangmon && make
	cd source/libhookdrastic && make
	
package-fonts:
	mkdir -p ./build/system/assets
	cp fonts/NotoSansJP-Bold.ttf ./build/system/assets/font.ttf
	cp fonts/OFL.txt ./build/system/assets/OFL.txt

package: package-fonts
	# package
	cp source/pp/pp.elf ./build/system/bin/pp
	cp source/hangmon/hangmon.elf ./build/system/bin/hangmon
	cp source/libhookdrastic/libhookdrastic.so ./build/system/lib/
	@set -e; \
		ttf_libdir="$$(pkg-config --variable=libdir SDL2_ttf)"; \
		ttf_lib="$$(find "$$ttf_libdir" -maxdepth 1 -name 'libSDL2_ttf-2.0.so.0' -print -quit)"; \
		test -n "$$ttf_lib"; \
		cp -L "$$ttf_lib" ./build/system/lib/libSDL2_ttf-2.0.so.0; \
		ldd "$$ttf_lib" | while read -r soname arrow path rest; do \
			[ "$$arrow" = "=>" ] || continue; \
			case "$$path" in /*) ;; *) continue ;; esac; \
			case "$$soname" in libSDL2-2.0.so.0|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libgcc_s.so.*|ld-linux-aarch64.so.*) continue ;; esac; \
			cp -L "$$path" "./build/system/lib/$$soname"; \
		done
	cd ./build/system && echo "$(RELEASE_NAME)\n$(BUILD_HASH)" > version.txt
	cd ./build && find . -type f -name '.DS_Store' -delete
	cd ./build && zip -r system.zip system && rm -rf system
	cd ./build && zip -r ../releases/$(RELEASE_NAME).zip .

done:
	# all done!
	@echo $(RELEASE_NAME)
	say "done" 2>/dev/null || true
