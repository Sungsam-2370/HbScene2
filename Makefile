BINARY      := scene_eshop
 
APP_TITLE   := Scene Eshop
APP_AUTHOR  := Luigi Switch Scene
APP_VERSION := 5.0.0
 
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

# Auto-instalacion de NSP (repo.json: "instalacion") -- solo aplica a Switch,
# ya que depende de NCM/ES (instalacion de titulos) y de libnx. mbedtls hace
# falta para derivar la header key de las NCA (CryptoUtils.cpp); zstd hace
# falta para descomprimir NCAs en formato .ncz (NszDecompressor.cpp) dentro
# de un nsp/nsz.
#
# OJO: a diferencia de SOURCES/CFLAGS (que chesto ya exporta), LIBS no viaja
# solo hacia el sub-make que hace el link final, asi que hay que exportarla
# a mano o el linker nunca la va a ver.
ifeq (switch,$(MAKECMDGOALS))
SOURCES   += libs/get/src/nspinstall
LIBS      += -lmbedtls -lmbedx509 -lmbedcrypto
LIBS      += -lzstd
export LIBS
endif
 
include libs/get/Makefile
include libs/chesto/Makefile

# ------------------------------------------------------------------
# Quita la tabla de simbolos de depuracion del .elf intermedio, DESPUES
# de que "make switch" termino normalmente. No modifica ninguna receta
# existente (switch/.nro/.nso/.pfs0 se generan exactamente igual que
# antes), asi que no puede romper el build actual — esto es un paso
# aparte, aditivo.
#
# Por que: el formato .nro/.nso en si no tiene lugar para una tabla de
# simbolos como la de un ELF de escritorio, asi que lo mas probable es
# que el .nro que se distribuye ya no la lleve. Pero el .elf intermedio
# (ej. scene_eshop_switch.elf) SI la tiene por defecto, y a veces ese
# archivo queda en el repo/artifacts de CI sin querer. Este paso lo deja
# limpio por si eso pasa, sin costo ni riesgo.
#
# Uso en el workflow de GitHub Actions: cambiar el paso de build de
# "make switch" a "make switch-safe" (o dejar "make switch" y agregar
# "make strip-switch-elf" despues).
# ------------------------------------------------------------------
STRIP_TOOL := $(DEVKITA64)/bin/aarch64-none-elf-strip

.PHONY: strip-switch-elf
strip-switch-elf:
	@if [ -f "$(BINARY)_switch.elf" ] && [ -x "$(STRIP_TOOL)" ]; then \
		echo "Quitando simbolos de depuracion de $(BINARY)_switch.elf..."; \
		"$(STRIP_TOOL)" --strip-all "$(BINARY)_switch.elf"; \
	else \
		echo "strip-switch-elf: no se encontro el .elf o la herramienta strip, se omite (no afecta el build)."; \
	fi
