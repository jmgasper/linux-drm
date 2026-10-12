# Builds the amdgpu kernel add-on for Haiku x86_64.
#
#   make -f haiku/build/Makefile -j32 HAIKU_TOP=<haiku tree> \
#       HAIKU_BUILD=<configured haiku build dir> [OUT=<dir>]
#
# HAIKU_TOP provides the kernel headers, HAIKU_BUILD the cross compiler (as
# configured there), kernel.so to link against and the generated headers.
# Targets: all (the add-on), core / amdgpu / compat (object groups),
# check FILE=<file> (compile one Linux file, e.g. FILE=drivers/dma-buf/dma-fence.c).

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
LINUX := $(ROOT)/linux
COMPAT := $(ROOT)/haiku/compat
DRIVER := $(ROOT)/haiku/amdgpu
BUILDDIR := $(ROOT)/haiku/build
OUT ?= $(ROOT)/out

HAIKU_TOP ?= /mnt/HaikuWork/x399/haiku-amdgpu
HAIKU_BUILD ?= /mnt/HaikuWork/build/master-x86_64
CROSS ?= $(shell sed -n 's/^HAIKU_CC_x86_64 ?= \(.*\)gcc ;$$/\1/p' \
	$(HAIKU_BUILD)/build/BuildConfig)
CC := $(CROSS)gcc
CXX := $(CROSS)g++
LD := $(CROSS)ld
GCCLIB := $(dir $(shell $(CC) -print-libgcc-file-name))
HAIKU_OBJ := $(HAIKU_BUILD)/objects/haiku/x86_64/release
GCC_SYSLIBS := $(firstword $(wildcard $(HAIKU_BUILD)/build_packages/gcc_syslibs_devel-*))

ADDON := $(OUT)/amdgpu_drm

# Lab builds: make ... ARMED_ONLY=1 (see driver.cpp)
HAIKU_DEFINES := $(if $(ARMED_ONLY),-DAMDGPU_DRM_ARMED_ONLY)

# --- flags -------------------------------------------------------------------

# Kernel add-on code generation, as Haiku's KernelAddon rule uses it.
KFLAGS := -O2 -fpic -mno-red-zone -fno-omit-frame-pointer -ffreestanding \
	-fno-strict-aliasing -fno-common -fno-delete-null-pointer-checks \
	-fno-strict-overflow -fno-stack-protector -fconserve-stack -g1 -MMD -MP

# Linux sources: Linux's language dialect and warnings, the compat headers
# instead of Linux's include/ and arch/ trees, and our kernel configuration.
LXWARN := -Wall -Wno-unused -Wno-sign-compare -Wno-pointer-sign \
	-Wno-missing-field-initializers -Wno-override-init -Wno-format \
	-Wno-address-of-packed-member -Wno-array-bounds -Wno-stringop-overflow \
	-Wno-dangling-pointer -Wno-frame-address -Wno-main \
	-Wno-unused-but-set-variable -Wno-packed-not-aligned \
	-Werror=implicit-function-declaration -Werror=implicit-int \
	-Werror=incompatible-pointer-types -Werror=int-conversion \
	-Werror=return-type
LXFLAGS := -std=gnu11 -nostdinc -fshort-wchar -funsigned-char $(KFLAGS) \
	$(LXWARN) -D__KERNEL__ -D__HAIKU_LINUX_COMPAT__ \
	-include $(COMPAT)/include/lx/kconfig.h \
	-I$(COMPAT)/include -I$(LINUX)/include -I$(LINUX)/include/uapi \
	-isystem $(dir $(shell $(CC) -print-libgcc-file-name))include

AMD_INCLUDES := $(addprefix -I$(LINUX)/,$(shell cat $(BUILDDIR)/includes-amdgpu.txt))

# Haiku-side C++ (compat implementation and the driver): Haiku's headers.
HAIKU_INCLUDES := -I$(COMPAT)/haiku -I$(DRIVER) \
	-I$(HAIKU_TOP)/build/config_headers -I$(HAIKU_TOP)/build/user_config_headers \
	-I$(HAIKU_TOP)/headers/private -I$(HAIKU_TOP)/headers/private/shared \
	-I$(HAIKU_TOP)/headers/private/kernel \
	-I$(HAIKU_TOP)/headers/private/kernel/arch/x86 \
	-I$(HAIKU_TOP)/headers/private/kernel/arch/x86/64 \
	-I$(HAIKU_TOP)/headers/private/kernel/boot/platform/efi \
	-I$(HAIKU_TOP)/headers/private/system \
	-I$(HAIKU_TOP)/headers/private/system/arch/x86_64 \
	-I$(HAIKU_TOP)/headers/private/drivers \
	-I$(HAIKU_TOP)/headers/private/graphics \
	-I$(HAIKU_TOP)/headers/private/libroot \
	-I$(HAIKU_OBJ)/../common/system/kernel \
	-I$(GCC_SYSLIBS)/develop/headers/c++ \
	-I$(GCC_SYSLIBS)/develop/headers/c++/x86_64-unknown-haiku \
	-I$(GCC_SYSLIBS)/develop/headers/gcc/include \
	-I$(GCC_SYSLIBS)/develop/headers/gcc/include-fixed \
	-I$(HAIKU_TOP)/headers/compatibility/bsd -I$(HAIKU_TOP)/headers/glibc \
	-I$(HAIKU_TOP)/headers/posix -I$(HAIKU_TOP)/headers \
	-I$(HAIKU_TOP)/headers/os -I$(HAIKU_TOP)/headers/os/drivers \
	-I$(HAIKU_TOP)/headers/os/kernel -I$(HAIKU_TOP)/headers/os/support \
	-I$(HAIKU_TOP)/headers/os/device -I$(HAIKU_TOP)/headers/os/storage \
	-I$(HAIKU_TOP)/headers/os/app -I$(HAIKU_TOP)/headers/os/interface \
	-I$(HAIKU_TOP)/headers/os/add-ons/graphics
HXXFLAGS := -std=gnu++20 -nostdinc $(KFLAGS) -fno-exceptions $(HAIKU_DEFINES) \
	-fno-use-cxa-atexit -D_KERNEL_MODE -D_DEFAULT_SOURCE -DARCH_x86_64 \
	-DHAIKU_TARGET_PLATFORM_HAIKU -Wall -Wno-multichar \
	-Wno-missing-field-initializers $(HAIKU_INCLUDES)
HCFLAGS := -std=gnu11 -nostdinc $(KFLAGS) -D_KERNEL_MODE -D_DEFAULT_SOURCE \
	-Wall $(HAIKU_INCLUDES)

# --- sources -----------------------------------------------------------------

CORE_SRCS := $(shell cat $(BUILDDIR)/sources-core.txt) \
	lib/sort.c lib/list_sort.c lib/bsearch.c lib/rbtree.c lib/interval_tree.c \
	lib/kstrtox.c lib/ctype.c lib/hexdump.c lib/math/gcd.c lib/scatterlist.c \
	lib/kfifo.c lib/llist.c lib/refcount.c lib/string_helpers.c lib/uuid.c \
	lib/math/div64.c lib/seq_buf.c lib/bitmap.c lib/find_bit.c drivers/gpu/buddy.c drivers/video/hdmi.c drivers/i2c/algos/i2c-algo-bit.c
AMD_SRCS := $(shell cat $(BUILDDIR)/sources-amdgpu.txt)
AMD_FP_SRCS := $(shell cat $(BUILDDIR)/sources-amdgpu-fp.txt)
LX_COMPAT_SRCS := $(wildcard $(COMPAT)/src/*.c)
GLUE_SRCS := $(wildcard $(DRIVER)/*.c)
HAIKU_SRCS := $(wildcard $(COMPAT)/haiku/*.cpp) $(wildcard $(DRIVER)/*.cpp)

obj = $(patsubst %.c,$(OUT)/obj/linux/%.o,$(1))
CORE_OBJS := $(call obj,$(CORE_SRCS))
AMD_OBJS := $(call obj,$(AMD_SRCS))
AMD_FP_OBJS := $(call obj,$(AMD_FP_SRCS))
LX_COMPAT_OBJS := $(patsubst $(COMPAT)/src/%.c,$(OUT)/obj/compat/%.o,$(LX_COMPAT_SRCS))
GLUE_OBJS := $(patsubst $(DRIVER)/%.c,$(OUT)/obj/glue/%.o,$(GLUE_SRCS))
HAIKU_OBJS := $(patsubst $(ROOT)/haiku/%.cpp,$(OUT)/obj/haiku/%.o,$(HAIKU_SRCS))
ALL_OBJS := $(CORE_OBJS) $(AMD_OBJS) $(AMD_FP_OBJS) $(LX_COMPAT_OBJS) $(GLUE_OBJS) \
	$(HAIKU_OBJS)

# per-group flags; every Linux file also sees its own directory, like kbuild
$(AMD_OBJS) $(AMD_FP_OBJS): GROUP_FLAGS := $(AMD_INCLUDES) -DBUILD_FEATURE_TIMING_SYNC=0
$(AMD_FP_OBJS): GROUP_FLAGS += -msse -msse2 -mhard-float -D_LINUX_FPU_COMPILATION_UNIT
$(CORE_OBJS): GROUP_FLAGS :=

.PHONY: all core amdgpu compat haiku clean
all: $(ADDON)
core: $(CORE_OBJS)
amdgpu: $(AMD_OBJS) $(AMD_FP_OBJS)
compat: $(LX_COMPAT_OBJS)
haiku: $(HAIKU_OBJS)

$(OUT)/obj/linux/%.o: $(LINUX)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   $*.c"
	@$(CC) $(LXFLAGS) $(GROUP_FLAGS) -I$(dir $<) \
		-DKBUILD_MODNAME='"amdgpu"' -DKBUILD_BASENAME='"$(notdir $*)"' \
		-c $< -o $@

$(OUT)/obj/compat/%.o: $(COMPAT)/src/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   compat/$*.c"
	@$(CC) $(LXFLAGS) -DKBUILD_MODNAME='"amdgpu"' -c $< -o $@

$(OUT)/obj/glue/%.o: $(DRIVER)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   glue/$*.c"
	@$(CC) $(LXFLAGS) $(AMD_INCLUDES) -DKBUILD_MODNAME='"amdgpu"' -c $< -o $@

$(OUT)/obj/haiku/%.o: $(ROOT)/haiku/%.cpp
	@mkdir -p $(dir $@)
	@echo "  C++  $*.cpp"
	@$(CXX) $(HXXFLAGS) -c $< -o $@

check:
	@mkdir -p $(OUT)/check
	$(CC) $(LXFLAGS) $(if $(findstring /amd/,$(FILE)),$(AMD_INCLUDES) -DBUILD_FEATURE_TIMING_SYNC=0) \
		-I$(dir $(LINUX)/$(FILE)) -DKBUILD_MODNAME='"amdgpu"' -c $(LINUX)/$(FILE) -o $(OUT)/check/x.o

$(ADDON): $(ALL_OBJS)
	@echo "  LD   $@"
	@$(LD) -o $@ -z max-page-size=0x1000 -shared --no-undefined -Bsymbolic \
		$(GCCLIB)crtbeginS.o $(ALL_OBJS) $(HAIKU_OBJ)/system/kernel/kernel.so \
		$(HAIKU_OBJ)/system/glue/haiku_version_glue.o $(GCCLIB)libgcc.a \
		$(GCCLIB)crtendS.o

clean:
	rm -rf $(OUT)

-include $(ALL_OBJS:.o=.d)
