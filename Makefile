DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
export PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)

CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := $(ARCH) -std=gnu11 -O2 -g -Wall -Wextra -ffunction-sections -D__3DS__ \
	-Iinclude -Ibuild -I$(DEVKITPRO)/libctru/include -MMD -MP
LDFLAGS := $(ARCH) -specs=3dsx.specs -Wl,-Map,build/nfs3ds.map -L$(DEVKITPRO)/libctru/lib
OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c)) build/vshader_bin.o build/scene_shader_bin.o

.PHONY: all assets rebuild-vehicle-assets rebuild-scene-assets validate-scenes test clean

all: nfs3ds.3dsx

assets:
	python3 tools/pack_runtime.py

rebuild-vehicle-assets:
	python3 tools/build_vehicle_assets.py \
		assets/source/vehicles_viv \
		assets/generated/3ds/vehicles
	python3 tools/derive_vehicle_wheel_fit.py assets/generated/3ds/vehicles
	python3 tools/build_vehicle_global_assets.py \
		assets/source/vehicle_global \
		assets/source/vehicles_viv/wheels.viv \
		assets/generated/3ds/vehicle_global

build:
	mkdir -p build

build/vshader.shbin: src/vshader.v.pica | build
	picasso -o $@ $<

build/vshader.s: build/vshader.shbin
	bin2s -H build/vshader_shbin.h $< > $@

build/vshader_shbin.h: build/vshader.s
	@test -f $@

build/vshader_bin.o: build/vshader.s
	$(CC) $(ARCH) -c $< -o $@

build/scene_shader.shbin: src/scene_shader.v.pica | build
	picasso -o $@ $<

build/scene_shader.s: build/scene_shader.shbin
	bin2s -H build/scene_shader_shbin.h $< > $@

build/scene_shader_shbin.h: build/scene_shader.s
	@test -f $@

build/scene_shader_bin.o: build/scene_shader.s
	$(CC) $(ARCH) -c $< -o $@

build/%.o: src/%.c build/vshader_shbin.h build/scene_shader_shbin.h | build
	$(CC) $(CFLAGS) -c $< -o $@

nfs3ds.elf: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -lcitro2d -lcitro3d -lctru -lm -o $@

nfs3ds.smdh: Makefile
	smdhtool --create \
		'Carbon Native' \
		'NFS Carbon Zeebo reconstruction' \
		'homebrew port' \
		$(DEVKITPRO)/libctru/default_icon.png $@

nfs3ds.3dsx: nfs3ds.elf nfs3ds.smdh assets
	3dsxtool $< $@ --smdh=nfs3ds.smdh --romfs=romfs

rebuild-scene-assets:
	python3 tools/build_scene_assets.py

validate-scenes:
	python3 tools/test_scene.py

test:
	python3 tools/test_host.py
	python3 tools/test_scene.py

clean:
	rm -rf build nfs3ds.elf nfs3ds.3dsx nfs3ds.smdh

-include $(wildcard build/*.d)
