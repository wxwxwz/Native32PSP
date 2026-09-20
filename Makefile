TARGET = Native32PSP
BUILD_PRX = 1
ENCRYPT = 1
OBJS = \
	src/main.o \
	src/platform/logo_rgba.o \
	src/platform/menu_font.o \
	src/platform/psp_log.o \
	src/platform/psp_settings.o \
	src/platform/psp_app.o \
	src/platform/game_presentation.o \
	src/platform/pause_menu.o \
	src/platform/screenshots.o \
	src/platform/system_info.o \
	src/core/actions.o \
	src/core/action_vm.o \
	src/core/archive_loader.o \
	src/core/audio_engine.o \
	src/core/mp3_software.o \
	src/core/cheats.o \
	src/core/cheat_file.o \
	src/core/content_loader.o \
	src/core/dat_loader.o \
	src/core/des_constants.o \
	src/core/emulator.o \
	src/core/file_browser.o \
	src/core/frame_player.o \
	src/core/header_decryptor.o \
	src/core/image_decoder.o \
	src/core/input_handler.o \
	src/core/native32_reader.o \
	src/core/renderer.o \
	src/core/save_manager.o \
	src/core/screenshot_store.o \
	src/core/save_state.o \
	src/core/mpeg/audio.o \
	src/core/mpeg/buffer.o \
	src/core/mpeg/demux.o \
	src/core/mpeg/player.o \
	src/core/mpeg/video.o \
	src/core/sprite_system.o

INCDIR = src
# The PSP software MPEG path and image compositor are CPU-bound. Keep small
# data objects out of the small-data area while enabling GCC's highest safe
# optimizations; fast-math is intentionally avoided because it changes video
# colour conversion and audio resampling results.
CFLAGS = -O3 -G0 -Wall -DPSP -MMD -MP
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS = -lpspkubridge -lpspgum -lpspgu -lpspctrl -lpspdisplay -lpspge -lpsppower -lpspaudio -lpspmp3 -lz -lstdc++

EXTRA_TARGETS = EBOOT.PBP
PSP_APP_VERSION := $(shell sed -n 's/^\#define N32_APP_VERSION "\(.*\)"/\1/p' src/platform/version.h)
PSP_EBOOT_TITLE = Native32PSP
PSP_EBOOT_ICON = assets/ICON0.png
# Keep PSPSDK's previously used homebrew SFO defaults. Version is displayed
# inside the app; XMB only needs the plain title and icon.

PSPSDK ?= $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

$(PSP_EBOOT_SFO): Makefile src/platform/version.h
$(PSP_EBOOT): $(PSP_EBOOT_ICON)
src/platform/psp_settings.o: src/platform/version.h

src/core/mp3_software.o: src/third_party/minimp3.h src/core/mp3_software.h

# Shared class layouts change in headers. Every consuming translation unit must
# be rebuilt, otherwise a signed package can still contain incompatible objects.
$(OBJS): Makefile
-include $(OBJS:.o=.d)
