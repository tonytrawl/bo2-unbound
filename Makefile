#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO. Example: export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
WUMS_ROOT := $(DEVKITPRO)/wums
ifeq ($(DIAGNOSTIC_SD),1)
APP_NAME := Black Ops 2 Unbound HQ - SD Test
APP_SHORTNAME := HQ SD Test
else
APP_NAME := Unbound HQ
APP_SHORTNAME := Unbound HQ
endif
APP_AUTHOR := Black Ops 2 Unbound

# wut_rules evaluates APP_* packaging settings as it is included, so this
# must be declared before the include below.
APP_CONTENT := $(TOPDIR)/.package-content
APP_ICON := $(TOPDIR)/content/meta/icon.png
APP_TV_SPLASH := $(TOPDIR)/content/meta/bootTv.png
APP_DRC_SPLASH := $(TOPDIR)/content/meta/bootDrc.png

include $(DEVKITPRO)/wut/share/wut_rules

# Refresh the package when content changes even if no C++ file was rebuilt.
WUHB_DEPS += $(shell find $(TOPDIR)/content -type f) $(TOPDIR)/tools/prepare_runtime_content.py

ifeq ($(DIAGNOSTIC_SD),1)
TARGET      := BO2-Unbound-HQ-SDTest
BUILD       := build-sd-test
else
TARGET      := Unbound-HQ
BUILD       := build
endif
SOURCES     := source
DATA        := data
INCLUDES    := include source

CFLAGS      := -g -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections $(MACHDEP)
CFLAGS      += $(INCLUDE) -D__WIIU__ -D__WUT__
ifeq ($(DIAGNOSTIC_SD),1)
CFLAGS      += -DUNBOUND_DIAGNOSTIC_SD
endif
CXXFLAGS    := $(CFLAGS) -std=gnu++20 -fno-exceptions -fno-rtti
ASFLAGS     := -g $(ARCH)
LDFLAGS     := -g $(ARCH) $(RPXSPECS) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections
LIBS        := -lmocha -lrpxloader -lzip -lcurl -lbrotlidec -lbrotlicommon -lmbedtls -lmbedx509 -lmbedcrypto -lbz2 -lz -lSDL2_image -lSDL2_ttf -lSDL2 -lharfbuzz -lfreetype -lpng -ljpeg -lwebp -lwut -lm
LIBDIRS     := $(PORTLIBS) $(DEVKITPRO)/portlibs/ppc $(WUT_ROOT)/usr $(WUT_ROOT) $(WUMS_ROOT)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(filter-out aoc_app_xml.bin aoc_cos_xml.bin aoc_meta_xml.bin,$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*))))
export LD := $(CXX)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all test test-curl test-zip runtime-content
all: $(BUILD)
runtime-content:
	@python3 tools/prepare_runtime_content.py
$(BUILD): runtime-content
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).wuhb $(TARGET).rpx $(TARGET).elf
test:
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/quick_update_tests.cpp source/model.cpp -o quick_update_tests
	@./quick_update_tests
	@rm -f quick_update_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Isource tests/supply_run_tests.cpp -o supply_run_tests
	@./supply_run_tests
	@rm -f supply_run_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/app_path_migration_tests.cpp -o app_path_migration_tests
	@./app_path_migration_tests
	@rm -f app_path_migration_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/map_packs_tests.cpp source/model.cpp -o map_packs_tests
	@./map_packs_tests
	@rm -f map_packs_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/custom_maps_tests.cpp source/model.cpp -o custom_maps_tests
	@./custom_maps_tests
	@rm -f custom_maps_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/package_layout_tests.cpp source/model.cpp -o package_layout_tests
	@./package_layout_tests
	@rm -f package_layout_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/network_tests.cpp source/network_schema.cpp $$(pkg-config --cflags --libs sdl2) -o network_tests
	@./network_tests
	@rm -f network_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/touch_scroll_tests.cpp -o touch_scroll_tests
	@./touch_scroll_tests
	@rm -f touch_scroll_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Isource tests/sd_package_tests.cpp -o sd_package_tests
	@./sd_package_tests
	@rm -f sd_package_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/disc_source_tests.cpp source/model.cpp -o disc_source_tests
	@./disc_source_tests
	@rm -f disc_source_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Isource tests/movie_install_path_tests.cpp -o movie_install_path_tests
	@./movie_install_path_tests
	@rm -f movie_install_path_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/movie_destination_policy_tests.cpp source/model.cpp -o movie_destination_policy_tests
	@./movie_destination_policy_tests
	@rm -f movie_destination_policy_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource tests/native_inherited_copy_tests.cpp source/sha256.cpp -o native_inherited_copy_tests
	@./native_inherited_copy_tests
	@rm -f native_inherited_copy_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/inherited_repair_gate_tests.cpp -o inherited_repair_gate_tests
	@./inherited_repair_gate_tests
	@rm -f inherited_repair_gate_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/inherited_content_tests.cpp -o inherited_content_tests
	@./inherited_content_tests
	@rm -f inherited_content_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/title_folder_link_tests.cpp -o title_folder_link_tests
	@./title_folder_link_tests
	@rm -f title_folder_link_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/retained_reference_restore_tests.cpp -o retained_reference_restore_tests
	@./retained_reference_restore_tests
	@rm -f retained_reference_restore_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/install_feedback_tests.cpp -o install_feedback_tests
	@./install_feedback_tests
	@rm -f install_feedback_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/package_verification_policy_tests.cpp -o package_verification_policy_tests
	@./package_verification_policy_tests
	@rm -f package_verification_policy_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/install_flow_integration_tests.cpp source/model.cpp source/sha256.cpp -o install_flow_integration_tests
	@./install_flow_integration_tests
	@rm -f install_flow_integration_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/file_hash_stream_tests.cpp source/sha256.cpp -o file_hash_stream_tests
	@./file_hash_stream_tests
	@rm -f file_hash_stream_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/package_cache_reset_tests.cpp -o package_cache_reset_tests
	@./package_cache_reset_tests
	@rm -f package_cache_reset_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Isource tests/install_activity_tests.cpp -o install_activity_tests
	@./install_activity_tests
	@rm -f install_activity_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude tests/download_animation_tests.cpp -o download_animation_tests
	@./download_animation_tests
	@rm -f download_animation_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/background_music_tests.cpp -o background_music_tests
	@./background_music_tests
	@rm -f background_music_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Itests/fake_audio -Iinclude -Isource tests/background_music_lifecycle_tests.cpp source/background_music.cpp -o background_music_lifecycle_tests
	@./background_music_lifecycle_tests
	@rm -f background_music_lifecycle_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/diagnostic_config_tests.cpp -o diagnostic_config_tests
	@./diagnostic_config_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -DUNBOUND_DIAGNOSTIC_SD -Iinclude tests/diagnostic_config_tests.cpp -o diagnostic_config_tests
	@./diagnostic_config_tests
	@rm -f diagnostic_config_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/archive_limits_tests.cpp -o archive_limits_tests
	@./archive_limits_tests
	@rm -f archive_limits_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/plugin_policy_tests.cpp source/model.cpp -o plugin_policy_tests
	@./plugin_policy_tests
	@rm -f plugin_policy_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/touch_gesture_tests.cpp -o touch_gesture_tests
	@./touch_gesture_tests
	@rm -f touch_gesture_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/touch_routes_tests.cpp -o touch_routes_tests
	@./touch_routes_tests
	@rm -f touch_routes_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/dashboard_navigation_tests.cpp -o dashboard_navigation_tests
	@./dashboard_navigation_tests
	@rm -f dashboard_navigation_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/core_recovery_tests.cpp source/model.cpp -o core_recovery_tests
	@./core_recovery_tests
	@rm -f core_recovery_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/core_recovery_restart_tests.cpp source/model.cpp -o core_recovery_restart_tests
	@./core_recovery_restart_tests
	@rm -f core_recovery_restart_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/navigation_repeat_tests.cpp -o navigation_repeat_tests
	@./navigation_repeat_tests
	@rm -f navigation_repeat_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource tests/download_control_tests.cpp source/sha256.cpp -o download_control_tests
	@./download_control_tests
	@rm -f download_control_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/model_tests.cpp source/model.cpp source/sha256.cpp -o model_tests
	@./model_tests
	@rm -f model_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/io_buffer_tests.cpp source/sha256.cpp -o io_buffer_tests
	@./io_buffer_tests
	@rm -f io_buffer_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/directory_policy_tests.cpp -o directory_policy_tests
	@./directory_policy_tests
	@rm -f directory_policy_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource tests/install_stream_tests.cpp -o install_stream_tests
	@./install_stream_tests
	@rm -f install_stream_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource tests/download_pipe_tests.cpp source/sha256.cpp -o download_pipe_tests
	@./download_pipe_tests
	@rm -f download_pipe_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource tests/background_operation_tests.cpp -o background_operation_tests
	@./background_operation_tests
	@rm -f background_operation_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/download_resume_tests.cpp -o download_resume_tests
	@./download_resume_tests
	@rm -f download_resume_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/diagnostics_tests.cpp source/diagnostics.cpp source/model.cpp -o diagnostics_tests
	@./diagnostics_tests
	@rm -f diagnostics_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/install_commit_tests.cpp -o install_commit_tests
	@./install_commit_tests
	@rm -f install_commit_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/install_preferences_tests.cpp source/model.cpp -o install_preferences_tests
	@./install_preferences_tests
	@rm -f install_preferences_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -Iinclude -Isource tests/install_discovery_tests.cpp source/model.cpp -o install_discovery_tests
	@./install_discovery_tests
	@rm -f install_discovery_tests
test-zip:
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Isource tests/support_archive_tests.cpp source/model.cpp -lzip -o support_archive_tests
	@./support_archive_tests
	@rm -f support_archive_tests
	@c++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource tests/mixed_package_zip_tests.cpp source/model.cpp source/sha256.cpp -lzip -o mixed_package_zip_tests
	@python3 tests/mixed_package_zip_tests.py
	@rm -f mixed_package_zip_tests
	@c++ -std=c++20 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource tests/buffered_zip_tests.cpp source/sha256.cpp -lzip -o buffered_zip_tests
	@./buffered_zip_tests
	@rm -f buffered_zip_tests
test-curl:
	@c++ -std=c++20 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude -Isource -I$(DEVKITPRO)/portlibs/wiiu/include tests/resume_curl_tests.cpp source/sha256.cpp -l:libcurl.so.4 -o resume_curl_tests
	@./resume_curl_tests
	@rm -f resume_curl_tests
else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).wuhb
$(OUTPUT).wuhb: $(OUTPUT).rpx
$(OUTPUT).rpx: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
$(OFILES_SRC): $(HFILES_BIN)
%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)
-include $(DEPENDS)
endif
