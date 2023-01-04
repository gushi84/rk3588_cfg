########################################
# x86-specific definitions

export XEN_IMG_OFFSET := 0x200000

CFLAGS += -I$(BASEDIR)/include
CFLAGS += -I$(BASEDIR)/include/asm-x86/mach-generic
CFLAGS += -I$(BASEDIR)/include/asm-x86/mach-default
CFLAGS += -DXEN_IMG_OFFSET=$(XEN_IMG_OFFSET)

# Prevent floating-point variables from creeping into Xen.
CFLAGS += -msoft-float

$(call cc-options-add,CFLAGS,CC,$(EMBEDDED_EXTRA_CFLAGS))
$(call cc-option-add,CFLAGS,CC,-Wnested-externs)
$(call as-option-add,CFLAGS,CC,"vmcall",-DHAVE_AS_VMX)
$(call as-option-add,CFLAGS,CC,"crc32 %eax$$(comma)%eax",-DHAVE_AS_SSE4_2)
$(call as-option-add,CFLAGS,CC,"invept (%rax)$$(comma)%rax",-DHAVE_AS_EPT)
$(call as-option-add,CFLAGS,CC,"rdrand %eax",-DHAVE_AS_RDRAND)
$(call as-option-add,CFLAGS,CC,"rdfsbase %rax",-DHAVE_AS_FSGSBASE)
$(call as-option-add,CFLAGS,CC,"xsaveopt (%rax)",-DHAVE_AS_XSAVEOPT)
$(call as-option-add,CFLAGS,CC,"rdseed %eax",-DHAVE_AS_RDSEED)
$(call as-option-add,CFLAGS,CC,"clac",-DHAVE_AS_CLAC_STAC)
$(call as-option-add,CFLAGS,CC,"clwb (%rax)",-DHAVE_AS_CLWB)
$(call as-option-add,CFLAGS,CC,".equ \"x\"$$(comma)1",-DHAVE_AS_QUOTED_SYM)
$(call as-option-add,CFLAGS,CC,"invpcid (%rax)$$(comma)%rax",-DHAVE_AS_INVPCID)
$(call as-option-add,CFLAGS,CC,"movdiri %rax$$(comma)(%rax)",-DHAVE_AS_MOVDIR)
$(call as-option-add,CFLAGS,CC,"enqcmd (%rax)$$(comma)%rax",-DHAVE_AS_ENQCMD)

# GAS's idea of true is -1.  Clang's idea is 1
$(call as-option-add,CFLAGS,CC,\
    ".if ((1 > 0) < 0); .error \"\";.endif",,-DHAVE_AS_NEGATIVE_TRUE)

# Check to see whether the assmbler supports the .nop directive.
$(call as-option-add,CFLAGS,CC,\
    ".L1: .L2: .nops (.L2 - .L1)$$(comma)9",-DHAVE_AS_NOPS_DIRECTIVE)

CFLAGS += -mno-red-zone -fpic

# Xen doesn't use MMX or SSE interally.  If the compiler supports it, also skip
# the SSE setup for variadic function calls.
CFLAGS += -mno-mmx -mno-sse $(call cc-option,$(CC),-mskip-rax-setup)

ifeq ($(CONFIG_INDIRECT_THUNK),y)
# Compile with gcc thunk-extern, indirect-branch-register if available.
CFLAGS-$(CONFIG_CC_IS_GCC) += -mindirect-branch=thunk-extern
CFLAGS-$(CONFIG_CC_IS_GCC) += -mindirect-branch-register
CFLAGS-$(CONFIG_CC_IS_GCC) += -fno-jump-tables

# Enable clang retpoline support if available.
CFLAGS-$(CONFIG_CC_IS_CLANG) += -mretpoline-external-thunk
endif

ifdef CONFIG_XEN_IBT
# Force -fno-jump-tables to work around
#   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104816
#   https://github.com/llvm/llvm-project/issues/54247
CFLAGS += -fcf-protection=branch -fno-jump-tables
else
$(call cc-option-add,CFLAGS,CC,-fcf-protection=none)
endif

# If supported by the compiler, reduce stack alignment to 8 bytes. But allow
# this to be overridden elsewhere.
$(call cc-option-add,CFLAGS_stack_boundary,CC,-mpreferred-stack-boundary=3)
export CFLAGS_stack_boundary

ifeq ($(CONFIG_UBSAN),y)
# Don't enable alignment sanitisation.  x86 has efficient unaligned accesses,
# and various things (ACPI tables, hypercall pages, stubs, etc) are wont-fix.
# It also causes an as-yet-unidentified crash on native boot before the
# console starts.
$(call cc-option-add,CFLAGS_UBSAN,CC,-fno-sanitize=alignment)
endif

# Set up the assembler include path properly for older toolchains.
CFLAGS += -Wa,-I$(BASEDIR)/include
