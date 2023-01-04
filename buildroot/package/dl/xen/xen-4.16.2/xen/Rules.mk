#
# See docs/misc/xen-makefiles/makefiles.rst on variables that can be used in
# Makefile and are consumed by Rules.mk
#

-include $(BASEDIR)/include/config/auto.conf

include $(XEN_ROOT)/Config.mk
include $(BASEDIR)/scripts/Kbuild.include


TARGET := $(BASEDIR)/xen

# Note that link order matters!
ALL_OBJS-y               += $(BASEDIR)/common/built_in.o
ALL_OBJS-y               += $(BASEDIR)/drivers/built_in.o
ALL_OBJS-y               += $(BASEDIR)/lib/built_in.o
ALL_OBJS-y               += $(BASEDIR)/xsm/built_in.o
ALL_OBJS-y               += $(BASEDIR)/arch/$(TARGET_ARCH)/built_in.o
ALL_OBJS-$(CONFIG_CRYPTO)   += $(BASEDIR)/crypto/built_in.o

ALL_LIBS-y               := $(BASEDIR)/lib/lib.a

# Initialise some variables
lib-y :=
targets :=
CFLAGS-y :=
AFLAGS-y :=

ALL_OBJS := $(ALL_OBJS-y)
ALL_LIBS := $(ALL_LIBS-y)

SPECIAL_DATA_SECTIONS := rodata $(foreach a,1 2 4 8 16, \
                                            $(foreach w,1 2 4, \
                                                        rodata.str$(w).$(a)) \
                                            rodata.cst$(a)) \
                         $(foreach r,rel rel.ro,data.$(r).local)

include Makefile

# Linking
# ---------------------------------------------------------------------------

quiet_cmd_ld = LD      $@
cmd_ld = $(LD) $(XEN_LDFLAGS) -r -o $@ $(filter-out %.a,$(real-prereqs)) \
               --start-group $(filter %.a,$(real-prereqs)) --end-group

# Archive
# ---------------------------------------------------------------------------

quiet_cmd_ar = AR      $@
cmd_ar = rm -f $@; $(AR) cr $@ $(real-prereqs)

# Objcopy
# ---------------------------------------------------------------------------

quiet_cmd_objcopy = OBJCOPY $@
cmd_objcopy = $(OBJCOPY) $(OBJCOPYFLAGS) $< $@

# binfile
# use e.g. $(call if_changed,binfile,binary-file varname)
quiet_cmd_binfile = BINFILE $@
cmd_binfile = $(SHELL) $(BASEDIR)/tools/binfile $(BINFILE_FLAGS) $@ $(2)

define gendep
    ifneq ($(1),$(subst /,:,$(1)))
        DEPS += $(dir $(1)).$(notdir $(1)).d
    endif
endef
$(foreach o,$(filter-out %/,$(obj-y) $(obj-bin-y) $(extra-y)),$(eval $(call gendep,$(o))))

# Handle objects in subdirs
# ---------------------------------------------------------------------------
# o if we encounter foo/ in $(obj-y), replace it by foo/built_in.o
#   and add the directory to the list of dirs to descend into: $(subdir-y)
subdir-y := $(subdir-y) $(filter %/, $(obj-y))
obj-y    := $(patsubst %/, %/built_in.o, $(obj-y))

# $(subdir-obj-y) is the list of objects in $(obj-y) which uses dir/ to
# tell kbuild to descend
subdir-obj-y := $(filter %/built_in.o, $(obj-y))

# Libraries are always collected in one lib file.
# Filter out objects already built-in
lib-y := $(filter-out $(obj-y), $(sort $(lib-y)))

$(filter %.init.o,$(obj-y) $(obj-bin-y) $(extra-y)): CFLAGS-y += -DINIT_SECTIONS_ONLY

ifeq ($(CONFIG_COVERAGE),y)
ifeq ($(CONFIG_CC_IS_CLANG),y)
    COV_FLAGS := -fprofile-instr-generate -fcoverage-mapping
else
    COV_FLAGS := -fprofile-arcs -ftest-coverage
endif
$(filter-out %.init.o $(nocov-y),$(obj-y) $(obj-bin-y) $(extra-y)): CFLAGS-y += $(COV_FLAGS)
endif

ifeq ($(CONFIG_UBSAN),y)
# Any -fno-sanitize= options need to come after any -fsanitize= options
$(filter-out %.init.o $(noubsan-y),$(obj-y) $(obj-bin-y) $(extra-y)): \
CFLAGS-y += $(filter-out -fno-%,$(CFLAGS_UBSAN)) $(filter -fno-%,$(CFLAGS_UBSAN))
endif

ifeq ($(CONFIG_LTO),y)
# Would like to handle all object files as bitcode, but objects made from
# pure asm are in a different format and have to be collected separately.
# Mirror the directory tree, collecting them as built_in_bin.o.
# If there are no binary objects in a given directory, make a dummy .o
obj-bin-y += $(patsubst %/built_in.o,%/built_in_bin.o,$(filter %/built_in.o,$(obj-y)))
else
# For a non-LTO build, bundle obj-bin targets in with the normal objs.
obj-y += $(obj-bin-y)
obj-bin-y :=
endif

# Always build obj-bin files as binary even if they come from C source. 
$(obj-bin-y): XEN_CFLAGS := $(filter-out -flto,$(XEN_CFLAGS))

# To be use with e.g. $(a_flags) or $(c_flags) to produce CPP flags
cpp_flags = $(filter-out -Wa$(comma)% -flto,$(1))

# Calculation of flags, first the generic flags, then the arch specific flags,
# and last the flags modified for a target or a directory.

c_flags = -MMD -MP -MF $(@D)/.$(@F).d $(XEN_CFLAGS)
a_flags = -MMD -MP -MF $(@D)/.$(@F).d $(XEN_AFLAGS)

include $(BASEDIR)/arch/$(TARGET_ARCH)/Rules.mk

c_flags += $(CFLAGS-y)
a_flags += $(CFLAGS-y) $(AFLAGS-y)

quiet_cmd_cc_builtin = CC      $@
cmd_cc_builtin = \
    $(CC) $(XEN_CFLAGS) -c -x c /dev/null -o $@

quiet_cmd_ld_builtin = LD      $@
ifeq ($(CONFIG_LTO),y)
cmd_ld_builtin = \
    $(LD_LTO) -r -o $@ $(filter $(obj-y),$(real-prereqs))
else
cmd_ld_builtin = \
    $(LD) $(XEN_LDFLAGS) -r -o $@ $(filter $(obj-y),$(real-prereqs))
endif

built_in.o: $(obj-y) $(if $(strip $(lib-y)),lib.a) $(extra-y) FORCE
	$(call if_changed,$(if $(strip $(obj-y)),ld_builtin,cc_builtin))

lib.a: $(lib-y) FORCE
	$(call if_changed,ar)

targets += built_in.o
ifneq ($(strip $(lib-y)),)
targets += lib.a
endif
targets += $(filter-out $(subdir-obj-y), $(obj-y) $(lib-y)) $(extra-y)
targets += $(MAKECMDGOALS)

built_in_bin.o: $(obj-bin-y) $(extra-y)
ifeq ($(strip $(obj-bin-y)),)
	$(CC) $(a_flags) -c -x assembler /dev/null -o $@
else
	$(LD) $(XEN_LDFLAGS) -r -o $@ $(filter $(obj-bin-y),$^)
endif

# Force execution of pattern rules (for which PHONY cannot be directly used).
PHONY += FORCE
FORCE:

%/built_in.o %/lib.a: FORCE
	$(MAKE) -f $(BASEDIR)/Rules.mk -C $* built_in.o

%/built_in_bin.o: FORCE
	$(MAKE) -f $(BASEDIR)/Rules.mk -C $* built_in_bin.o

SRCPATH := $(patsubst $(BASEDIR)/%,%,$(CURDIR))

quiet_cmd_cc_o_c = CC      $@
ifeq ($(CONFIG_ENFORCE_UNIQUE_SYMBOLS),y)
    cmd_cc_o_c = $(CC) $(c_flags) -c $< -o $(dot-target).tmp -MQ $@
    ifeq ($(CONFIG_CC_IS_CLANG),y)
        cmd_objcopy_fix_sym = $(OBJCOPY) --redefine-sym $<=$(SRCPATH)/$< $(dot-target).tmp $@
    else
        cmd_objcopy_fix_sym = $(OBJCOPY) --redefine-sym $(<F)=$(SRCPATH)/$< $(dot-target).tmp $@
    endif
    cmd_objcopy_fix_sym += && rm -f $(dot-target).tmp
else
    cmd_cc_o_c = $(CC) $(c_flags) -c $< -o $@
endif

define rule_cc_o_c
    $(call cmd_and_record,cc_o_c)
    $(call cmd,objcopy_fix_sym)
endef

%.o: %.c FORCE
	$(call if_changed_rule,cc_o_c)

quiet_cmd_cc_o_S = CC      $@
cmd_cc_o_S = $(CC) $(a_flags) -c $< -o $@

%.o: %.S FORCE
	$(call if_changed,cc_o_S)


quiet_cmd_obj_init_o = INIT_O  $@
define cmd_obj_init_o
    $(OBJDUMP) -h $< | while read idx name sz rest; do \
        case "$$name" in \
        .*.local) ;; \
        .text|.text.*|.data|.data.*|.bss|.bss.*) \
            test $$(echo $$sz | sed 's,00*,0,') != 0 || continue; \
            echo "Error: size of $<:$$name is 0x$$sz" >&2; \
            exit $$(expr $$idx + 1);; \
        esac; \
    done || exit $$?; \
    $(OBJCOPY) $(foreach s,$(SPECIAL_DATA_SECTIONS),--rename-section .$(s)=.init.$(s)) $< $@
endef

$(filter %.init.o,$(obj-y) $(obj-bin-y) $(extra-y)): %.init.o: %.o FORCE
	$(call if_changed,obj_init_o)

quiet_cmd_cpp_i_c = CPP     $@
cmd_cpp_i_c = $(CPP) $(call cpp_flags,$(c_flags)) -MQ $@ -o $@ $<

quiet_cmd_cc_s_c = CC      $@
cmd_cc_s_c = $(CC) $(filter-out -Wa$(comma)%,$(c_flags)) -S $< -o $@

quiet_cmd_cpp_s_S = CPP     $@
cmd_cpp_s_S = $(CPP) $(call cpp_flags,$(a_flags)) -MQ $@ -o $@ $<

%.i: %.c FORCE
	$(call if_changed,cpp_i_c)

%.s: %.c FORCE
	$(call if_changed,cc_s_c)

%.s: %.S FORCE
	$(call if_changed,cpp_s_S)

# Add intermediate targets:
# When building objects with specific suffix patterns, add intermediate
# targets that the final targets are derived from.
intermediate_targets = $(foreach sfx, $(2), \
				$(patsubst %$(strip $(1)),%$(sfx), \
					$(filter %$(strip $(1)), $(targets))))
# %.init.o <- %.o
targets += $(call intermediate_targets, .init.o, .o)

-include $(DEPS_INCLUDE)

# Read all saved command lines and dependencies for the $(targets) we
# may be building above, using $(if_changed{,_dep}). As an
# optimization, we don't need to read them if the target does not
# exist, we will rebuild anyway in that case.

existing-targets := $(wildcard $(sort $(targets)))

-include $(foreach f,$(existing-targets),$(dir $(f)).$(notdir $(f)).cmd)

# Declare the contents of the PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
