################################################################################
# following setup taken from environment variables
################################################################################

ifndef MIOS32_PATH # if no mios path is specified, we assume a local toolchain
	MIOS32_PATH = mios32/trunk
	MIOS32_BIN_PATH = $(MIOS32_PATH)/bin
	MIOS32_GCC_PREFIX="$(shell pwd)/mios32_toolchain/bin/arm-none-eabi"
	MIOS32_FAMILY=STM32F4xx
	MIOS32_PROCESSOR=STM32F407VG
	MIOS32_BOARD=MBHP_CORE_STM32F4
	MIOS32_LCD=universal
endif

PROCESSOR = 	$(MIOS32_PROCESSOR)
FAMILY    =     $(MIOS32_FAMILY)
BOARD     =     $(MIOS32_BOARD)
LCD       =     $(MIOS32_LCD)

################################################################################
# Source Files, include paths and libraries
################################################################################

THUMB_SOURCE    = app.c

# (following source stubs not relevant for Cortex M3 derivatives)
THUMB_AS_SOURCE =
ARM_SOURCE      =
ARM_AS_SOURCE   =

C_INCLUDE =     -I .
A_INCLUDE =     -I .

LIBS =


################################################################################
# Remaining variables
################################################################################

LD_FILE   =     $(MIOS32_PATH)/etc/ld/$(FAMILY)/$(PROCESSOR).ld
PROJECT   =     RytmController

DEBUG     = -g
OPTIMIZE  = -Os

CFLAGS =    $(DEBUG) $(OPTIMIZE)


################################################################################
# Include source modules via additional makefiles
################################################################################

# sources of programming model
include $(MIOS32_PATH)/programming_models/traditional/programming_model.mk

# application specific LCD driver (selected via makefile variable)
include $(MIOS32_PATH)/modules/app_lcd/$(LCD)/app_lcd.mk

# EEPROM emulation
include $(MIOS32_PATH)/modules/eeprom/eeprom.mk

# common make rules
# Please keep this include statement at the end of this Makefile. Add new modules above.
include $(MIOS32_PATH)/include/makefile/common.mk
