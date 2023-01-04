override XEN_TARGET_ARCH=x86_32
CFLAGS =
include $(XEN_ROOT)/Config.mk

$(call cc-options-add,CFLAGS,CC,$(EMBEDDED_EXTRA_CFLAGS))

CFLAGS += -Werror -fno-builtin -g0 -msoft-float
CFLAGS += -I$(BASEDIR)/include
CFLAGS := $(filter-out -flto,$(CFLAGS)) 

LDFLAGS_DIRECT-$(shell $(LD) -v --warn-rwx-segments >/dev/null 2>&1 && echo y) := --no-warn-rwx-segments
LDFLAGS_DIRECT += $(LDFLAGS_DIRECT-y)

# NB. awk invocation is a portable alternative to 'head -n -1'
%.S: %.bin
	(od -v -t x $< | tr -s ' ' | awk 'NR > 1 {print s} {s=$$0}' | \
	sed 's/ /,0x/g' | sed 's/,0x$$//' | sed 's/^[0-9]*,/ .long /') >$@

# Drop .got.plt during conversion to plain binary format.
# Please check build32.lds for more details.
%.bin: %.lnk
	$(OBJDUMP) -h $< | sed -n '/[0-9]/{s,00*,0,g;p;}' | \
		while read idx name sz rest; do \
			case "$$name" in \
			.got.plt) \
				test $$sz != 0c || continue; \
				echo "Error: non-empty $$name: 0x$$sz" >&2; \
				exit $$(expr $$idx + 1);; \
			esac; \
		done
	$(OBJCOPY) -O binary -R .got.plt $< $@

%.lnk: %.o build32.lds
	$(LD) $(LDFLAGS_DIRECT) -N -T build32.lds -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -fpic $< -o $@

cmdline.o: cmdline.c $(CMDLINE_DEPS)

reloc.o: reloc.c $(RELOC_DEPS)

.PRECIOUS: %.bin %.lnk
