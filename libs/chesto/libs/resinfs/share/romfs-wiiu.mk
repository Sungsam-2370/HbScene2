ifneq ($(strip $(V)), 1)
	Q ?= @
endif

ifneq (, $(shell which gtar))
	Q += g
endif

ifneq ($(strip $(RAMFS_DIR)),)

TOPDIR		?=	.

ROMFS_TARGET	:=	app.romfs.o

%.romfs.o: $(TOPDIR)/$(RAMFS_DIR)
	@echo ROMFS $(notdir $@)
	$(Q)tar -H ustar -cvf romfs.tar -C $(TOPDIR)/$(RAMFS_DIR) .
	$(PREFIX)ld -r -b binary romfs.tar -o $@
	@rm -f romfs.tar

else

ROMFS_TARGET	:=

endif
