# Mushoku Tensei 64 -- an unofficial Nintendo 64 fan game built with libdragon.
#
# Requires the libdragon toolchain (N64_INST must point to it) and the
# libdragon "preview" branch, which provides the RSP-accelerated OpenGL
# implementation used for the 3D renderer.

BUILD_DIR = build
# OpenGL is part of libdragon's preview API set.
LIBDRAGON_PREVIEW = 2
include $(N64_INST)/include/n64.mk

ROM = mushoku64.z64

# Extra compiler flags for debug/test builds, e.g.
#   make EXTRA_CFLAGS=-DTEST_STEP=1     (boot straight into a story step)
N64_CFLAGS += $(EXTRA_CFLAGS)

src = $(wildcard src/*.c)

tex_png = $(wildcard assets/tex_*.png)
ui_png  = $(wildcard assets/ui_*.png)

assets_conv  = $(addprefix filesystem/,$(notdir $(tex_png:%.png=%.sprite)))
assets_conv += $(addprefix filesystem/,$(notdir $(ui_png:%.png=%.sprite)))
assets_conv += filesystem/fx_glow.sprite filesystem/logo_kanji.sprite
assets_conv += filesystem/body.font64 filesystem/outline.font64 filesystem/title.font64

all: $(ROM)

# World textures: 32x32 grayscale detail maps, tinted by vertex colour.
# Mipmapped: trilinear filtering avoids shimmering, and with fog enabled the
# RDP runs in 2-cycle mode and samples the second mip tile as well.
filesystem/tex_%.sprite: assets/tex_%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 --mipmap BOX -o "$(dir $@)" "$<"

# HUD discs: intensity+alpha so they can be tinted any colour.
filesystem/ui_%.sprite: assets/ui_%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f IA8 -o "$(dir $@)" "$<"

filesystem/fx_glow.sprite: assets/fx_glow.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f I8 -o "$(dir $@)" "$<"

filesystem/logo_kanji.sprite: assets/logo_kanji.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 -o "$(dir $@)" "$<"

# Fonts. mkfont names its output after the input file, so convert into a
# scratch directory and rename.
define make_font
	@mkdir -p $(dir $@) $(BUILD_DIR)/font_$(1)
	@echo "    [FONT] $@"
	@$(N64_MKFONT) $(2) -o $(BUILD_DIR)/font_$(1) "$<"
	@mv $(BUILD_DIR)/font_$(1)/*.font64 $@
endef

filesystem/body.font64: assets/fonts/DejaVuSans-Bold.ttf
	$(call make_font,body,--size 10)

filesystem/outline.font64: assets/fonts/DejaVuSans-Bold.ttf
	$(call make_font,outline,--size 10 --outline 1)

filesystem/title.font64: assets/fonts/DejaVuSerif-Bold.ttf
	$(call make_font,title,--size 16 --outline 1.5)

$(BUILD_DIR)/mushoku64.dfs: $(assets_conv)
$(BUILD_DIR)/mushoku64.elf: $(src:%.c=$(BUILD_DIR)/%.o)

$(ROM): N64_ROM_TITLE = "MUSHOKU TENSEI 64"
$(ROM): N64_ROM_SAVETYPE = eeprom4k
$(ROM): N64_ROM_REGIONFREE = 1
$(ROM): $(BUILD_DIR)/mushoku64.dfs

clean:
	rm -rf $(BUILD_DIR) filesystem/ $(ROM)

-include $(wildcard $(BUILD_DIR)/src/*.d)

.PHONY: all clean
