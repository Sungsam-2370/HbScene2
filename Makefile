BINARY      := scene_eshop
 
APP_TITLE   := Scene Eshop
APP_AUTHOR  := Luigi Switch Scene
APP_VERSION := 4.0.0
 
SOURCES     += gui console
DEBUG_BUILD := 0
CFLAGS += -Wno-missing-field-initializers \
-Wno-unused-parameter \
-Wno-unused-variable \
-Wno-sign-compare \
-Wno-reorder \
-Wno-parentheses \
-Wno-narrowing
 
# CFLAGS    += -DWII_MOCK=1
 
ifeq (wiiu,$(MAKECMDGOALS))
SOURCES   += libs/librpxloader/source
INCLUDES  += ../libs/librpxloader/include
endif
 
include libs/get/Makefile
include libs/chesto/Makefile
