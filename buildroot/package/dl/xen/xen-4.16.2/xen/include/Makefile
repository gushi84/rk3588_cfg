ifneq ($(CONFIG_COMPAT),)

compat-arch-$(CONFIG_X86) := x86_32

headers-y := \
    compat/arch-$(compat-arch-y).h \
    compat/elfnote.h \
    compat/event_channel.h \
    compat/features.h \
    compat/memory.h \
    compat/nmi.h \
    compat/physdev.h \
    compat/platform.h \
    compat/pmu.h \
    compat/sched.h \
    compat/vcpu.h \
    compat/version.h \
    compat/xen.h \
    compat/xlat.h
headers-$(CONFIG_X86)     += compat/arch-x86/pmu.h
headers-$(CONFIG_X86)     += compat/arch-x86/xen-mca.h
headers-$(CONFIG_X86)     += compat/arch-x86/xen.h
headers-$(CONFIG_X86)     += compat/arch-x86/xen-$(compat-arch-y).h
headers-$(CONFIG_ARGO)    += compat/argo.h
headers-$(CONFIG_PV)      += compat/callback.h
headers-$(CONFIG_GRANT_TABLE) += compat/grant_table.h
headers-$(CONFIG_PV_SHIM) += compat/grant_table.h
headers-$(CONFIG_HVM)     += compat/hvm/dm_op.h
headers-$(CONFIG_HVM)     += compat/hvm/hvm_op.h
headers-$(CONFIG_HVM)     += compat/hvm/hvm_vcpu.h
headers-$(CONFIG_HYPFS)   += compat/hypfs.h
headers-$(CONFIG_KEXEC)   += compat/kexec.h
headers-$(CONFIG_TRACEBUFFER) += compat/trace.h
headers-$(CONFIG_XENOPROF) += compat/xenoprof.h
headers-$(CONFIG_XSM_FLASK) += compat/xsm/flask_op.h

cppflags-y                := -include public/xen-compat.h -DXEN_GENERATING_COMPAT_HEADERS
cppflags-$(CONFIG_X86)    += -m32

endif

public-$(CONFIG_X86) := $(wildcard public/arch-x86/*.h public/arch-x86/*/*.h)
public-$(CONFIG_ARM) := $(wildcard public/arch-arm/*.h public/arch-arm/*/*.h)

.PHONY: all
all: $(headers-y)

compat/%.h: compat/%.i Makefile $(BASEDIR)/tools/compat-build-header.py
	$(PYTHON) $(BASEDIR)/tools/compat-build-header.py <$< $@ >>$@.new; \
	mv -f $@.new $@

compat/%.i: compat/%.c Makefile
	$(CPP) $(filter-out -Wa$(comma)% -include %/include/xen/config.h,$(XEN_CFLAGS)) $(cppflags-y) -o $@ $<

compat/%.c: public/%.h xlat.lst Makefile $(BASEDIR)/tools/compat-build-source.py
	mkdir -p $(@D)
	$(PYTHON) $(BASEDIR)/tools/compat-build-source.py xlat.lst <$< >$@.new
	mv -f $@.new $@

compat/.xlat/%.h: compat/%.h compat/.xlat/%.lst $(BASEDIR)/tools/get-fields.sh Makefile
	export PYTHON=$(PYTHON); \
	while read what name; do \
		$(SHELL) $(BASEDIR)/tools/get-fields.sh "$$what" compat_$$name $< || exit $$?; \
	done <$(patsubst compat/%,compat/.xlat/%,$(basename $<)).lst >$@.new
	mv -f $@.new $@

.PRECIOUS: compat/.xlat/%.lst
compat/.xlat/%.lst: xlat.lst Makefile
	mkdir -p $(@D)
	grep -v '^[[:blank:]]*#' $< | sed -ne 's,@arch@,$(compat-arch-y),g' -re 's,[[:blank:]]+$*\.h[[:blank:]]*$$,,p' >$@.new
	$(call move-if-changed,$@.new,$@)

xlat-y := $(shell sed -ne 's,@arch@,$(compat-arch-y),g' -re 's,^[?!][[:blank:]]+[^[:blank:]]+[[:blank:]]+,,p' xlat.lst | uniq)
xlat-y := $(filter $(patsubst compat/%,%,$(headers-y)),$(xlat-y))

compat/xlat.h: $(addprefix compat/.xlat/,$(xlat-y)) config/auto.conf Makefile
	cat $(filter %.h,$^) >$@.new
	mv -f $@.new $@

ifeq ($(XEN_TARGET_ARCH),$(XEN_COMPILE_ARCH))

all: headers.chk headers99.chk headers++.chk

PUBLIC_HEADERS := $(filter-out public/arch-% public/dom0_ops.h, $(wildcard public/*.h public/*/*.h) $(public-y))

PUBLIC_C99_HEADERS := public/io/9pfs.h public/io/pvcalls.h
PUBLIC_ANSI_HEADERS := $(filter-out public/%ctl.h public/xsm/% public/%hvm/save.h $(PUBLIC_C99_HEADERS), $(PUBLIC_HEADERS))

public/io/9pfs.h-prereq := string
public/io/pvcalls.h-prereq := string

headers.chk: $(PUBLIC_ANSI_HEADERS) Makefile
	for i in $(filter %.h,$^); do \
	    $(CC) -x c -ansi -Wall -Werror -include stdint.h \
	          -S -o /dev/null $$i || exit 1; \
	    echo $$i; \
	done >$@.new
	mv $@.new $@

headers99.chk: $(PUBLIC_C99_HEADERS) Makefile
	rm -f $@.new
	$(foreach i, $(filter %.h,$^),                                        \
	    echo "#include "\"$(i)\"                                          \
	    | $(CC) -x c -std=c99 -Wall -Werror                               \
	      -include stdint.h $(foreach j, $($(i)-prereq), -include $(j).h) \
	      -S -o /dev/null -                                               \
	    || exit $$?; echo $(i) >> $@.new;)
	mv $@.new $@

headers++.chk: $(PUBLIC_HEADERS) Makefile
	rm -f $@.new
	if ! $(CXX) -v >/dev/null 2>&1; then                                  \
	    touch $@.new;                                                     \
	    exit 0;                                                           \
	fi;                                                                   \
	$(foreach i, $(filter %.h,$^),                                        \
	    echo "#include "\"$(i)\"                                          \
	    | $(CXX) -x c++ -std=gnu++98 -Wall -Werror -D__XEN_TOOLS__        \
	      -include stdint.h -include public/xen.h                         \
	      $(foreach j, $($(i)-prereq), -include c$(j)) -S -o /dev/null -  \
	    || exit $$?; echo $(i) >> $@.new;)
	mv $@.new $@

endif

ifeq ($(XEN_TARGET_ARCH),x86_64)
.PHONY: lib-x86-all
lib-x86-all:
	$(MAKE) -C xen/lib/x86 all

all: lib-x86-all
endif

clean::
	rm -rf compat config generated headers*.chk
	rm -f $(BASEDIR)/include/xen/lib/x86/cpuid-autogen.h
