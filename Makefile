#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET      := bilibili-3ds
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := source
ROMFS       := romfs

ifeq ($(DEBUG),1)
BUILD       := build-debug
endif

#---------------------------------------------------------------------------------
ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS      := -g -Wall -O2 -mword-relocations \
               -fomit-frame-pointer -ffunction-sections \
               $(ARCH)

CFLAGS      += $(INCLUDE) -D__3DS__

CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions

ifeq ($(DEBUG),1)
CFLAGS      += -O0 -DENABLE_DEBUG_LOG
CXXFLAGS    += -O0 -DENABLE_DEBUG_LOG
endif

ASFLAGS     := -g $(ARCH)
LDFLAGS      = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS        := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS     := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD       := $(CXX)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)

export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# CIA 相关变量
BANNER      := meta/banner.png
ICON        := meta/icon.png
RSF         := meta/bilibili-3ds.rsf
SMDH        := $(TARGET).smdh
BANNER_BIN  := $(TARGET)-banner.bnr
ICON_BIN    := $(TARGET)-icon.icn

# devkitPro 工具绝对路径（不依赖 PATH）
BANNERTOOL  := $(DEVKITPRO)/tools/bin/bannertool
MAKEROM     := $(DEVKITPRO)/tools/bin/makerom

.PHONY: $(BUILD) clean all 3dsx cia

#---------------------------------------------------------------------------------
all: 3dsx

3dsx: $(BUILD)
	@echo "built ... $(TARGET).3dsx"

cia: $(BUILD) $(TARGET).cia
	@echo "built ... $(TARGET).cia"

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
# 生成 .cia：需要 makerom 与 bannertool（devkitPro 自带）
#---------------------------------------------------------------------------------
$(TARGET).cia: $(TARGET).elf $(BANNER) $(ICON) $(RSF)
	@echo "building cia ..."
	$(BANNERTOOL) makebanner -i $(BANNER) -o $(BANNER_BIN)
	$(BANNERTOOL) makesmdh -s "3dbili" -l "Bilibili 3DS Client" \
	    -p "AI generated" -i $(ICON) -o $(ICON_BIN)
	$(MAKEROM) -f cia -o $(TARGET).cia -target t -exefslogo \
	    -elf $(TARGET).elf -icon $(ICON_BIN) -banner $(BANNER_BIN) -rsf $(RSF)

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr build build-debug \
	       $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia \
	       $(BANNER_BIN) $(ICON_BIN)

#---------------------------------------------------------------------------------
else
.PHONY: $(BUILD) clean all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).3dsx $(OUTPUT).smdh

$(OUTPUT).3dsx : $(OUTPUT).elf
$(OUTPUT).elf  : $(OFILES)

$(OFILES_SRC)  : $(HFILES_BIN)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
