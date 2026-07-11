BUILD_DIR = build
TARGET = $(BUILD_DIR)/vshbgmX
C_SRCS = vshbgm.c utils/utils.c
S_SRCS = external/systemctrl_stubs.S
OBJS = $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o) $(S_SRCS:.S=.o))

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

INCDIR = . utils external
CFLAGS = -Os -G0 -Wall -fno-builtin-printf
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PRX_EXPORTS = exports.exp
PSP_FW_VERSION = 500

LIBS = -lpspaudio -lpspaudiocodec -lpsputility -lpsppower -lpspkernel

all: | $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak

release: all
	psp-packer $(TARGET).prx
