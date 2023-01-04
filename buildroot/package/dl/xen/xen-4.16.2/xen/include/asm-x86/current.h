/******************************************************************************
 * current.h
 * 
 * Information structure that lives at the bottom of the per-cpu Xen stack.
 */

#ifndef __X86_CURRENT_H__
#define __X86_CURRENT_H__

#include <xen/percpu.h>
#include <xen/page-size.h>
#include <public/xen.h>

/*
 * Xen's cpu stacks are 8 pages (8-page aligned), arranged as:
 *
 * 7 - Primary stack (with a struct cpu_info at the top)
 * 6 - Primary stack
 * 5 - Primay Shadow Stack (read-only)
 * 4 - #DF IST stack
 * 3 - #DB IST stack
 * 2 - NMI IST stack
 * 1 - #MC IST stack
 * 0 - IST Shadow Stacks (4x 1k, read-only)
 */

/*
 * Identify which stack page the stack pointer is on.  Returns an index
 * as per the comment above.
 */
static inline unsigned int get_stack_page(unsigned long sp)
{
    return (sp & (STACK_SIZE-1)) >> PAGE_SHIFT;
}

struct vcpu;

struct cpu_info {
    struct cpu_user_regs guest_cpu_user_regs;
    unsigned int processor_id;
    unsigned int verw_sel;
    struct vcpu *current_vcpu;
    unsigned long per_cpu_offset;
    unsigned long cr4;
    /*
     * Of the two following fields the latter is being set to the CR3 value
     * to be used on the given pCPU for loading whenever 64-bit PV guest
     * context is being entered. A value of zero indicates no setting of CR3
     * is to be performed.
     * The former is the value to restore when re-entering Xen, if any. IOW
     * its value being zero means there's nothing to restore.
     */
    unsigned long xen_cr3;
    unsigned long pv_cr3;

    /* See asm-x86/spec_ctrl_asm.h for usage. */
    unsigned int shadow_spec_ctrl;
    uint8_t      xen_spec_ctrl;
    uint8_t      last_spec_ctrl;
    uint8_t      spec_ctrl_flags;

    /*
     * The following field controls copying of the L4 page table of 64-bit
     * PV guests to the per-cpu root page table on entering the guest context.
     * If set the L4 page table is being copied to the root page table and
     * the field will be reset.
     */
    bool         root_pgt_changed;

    /*
     * use_pv_cr3 is set in case the value of pv_cr3 is to be written into
     * CR3 when returning from an interrupt. The main use is when returning
     * from a NMI or MCE to hypervisor code where pv_cr3 was active.
     */
    bool         use_pv_cr3;

    /* get_stack_bottom() must be 16-byte aligned */
};

static inline struct cpu_info *get_cpu_info_from_stack(unsigned long sp)
{
    return (struct cpu_info *)((sp | (STACK_SIZE - 1)) + 1) - 1;
}

static inline struct cpu_info *get_cpu_info(void)
{
#ifdef __clang__
    /* Clang complains that sp in the else case is not initialised. */
    unsigned long sp;
    asm ( "mov %%rsp, %0" : "=r" (sp) );
#else
    register unsigned long sp asm("rsp");
#endif

    return get_cpu_info_from_stack(sp);
}

#define get_current()         (get_cpu_info()->current_vcpu)
#define set_current(vcpu)     (get_cpu_info()->current_vcpu = (vcpu))
#define current               (get_current())

#define get_processor_id()    (get_cpu_info()->processor_id)
#define guest_cpu_user_regs() (&get_cpu_info()->guest_cpu_user_regs)

/*
 * Get the bottom-of-stack, as stored in the per-CPU TSS. This actually points
 * into the middle of cpu_info.guest_cpu_user_regs, at the section that
 * precisely corresponds to a CPU trap frame.
 */
#define get_stack_bottom()                      \
    ((unsigned long)&get_cpu_info()->guest_cpu_user_regs.es)

/*
 * Get the reasonable stack bounds for stack traces and stack dumps.  Stack
 * dumps have a slightly larger range to include exception frames in the
 * printed information.  The returned word is inside the interesting range.
 */
unsigned long get_stack_trace_bottom(unsigned long sp);
unsigned long get_stack_dump_bottom (unsigned long sp);

#ifdef CONFIG_LIVEPATCH
# define CHECK_FOR_LIVEPATCH_WORK "call check_for_livepatch_work;"
#elif defined(CONFIG_DEBUG)
/* Mimic the clobbering effect a call has on registers. */
# define CHECK_FOR_LIVEPATCH_WORK \
    "mov $0x1234567890abcdef, %%rax\n\t" \
    "mov %%rax, %%rcx; mov %%rax, %%rdx\n\t" \
    "mov %%rax, %%rsi; mov %%rax, %%rdi\n\t" \
    "mov %%rax, %%r8; mov %%rax, %%r9\n\t" \
    "mov %%rax, %%r10; mov %%rax, %%r11\n\t"
#else
# define CHECK_FOR_LIVEPATCH_WORK ""
#endif

#ifdef CONFIG_XEN_SHSTK
/*
 * We need to unwind the primary shadow stack to its supervisor token, located
 * in the last word of the primary shadow stack.
 *
 * Read the shadow stack pointer, subtract it from supervisor token position,
 * and divide by 8 to get the number of slots needing popping.
 *
 * INCSSPQ can't pop more than 255 entries.  We shouldn't ever need to pop
 * that many entries, and getting this wrong will cause us to #DF later.  Turn
 * it into a BUG() now for fractionally easier debugging.
 */
# define SHADOW_STACK_WORK                                      \
    "mov $1, %[ssp];"                                           \
    "rdsspd %[ssp];"                                            \
    "cmp $1, %[ssp];"                                           \
    "je .L_shstk_done.%=;" /* CET not active?  Skip. */         \
    "mov $%c[skstk_base], %[val];"                              \
    "and $%c[stack_mask], %[ssp];"                              \
    "sub %[ssp], %[val];"                                       \
    "shr $3, %[val];"                                           \
    "cmp $255, %[val];" /* More than 255 entries?  Crash. */    \
    UNLIKELY_START(a, shstk_adjust)                             \
    _ASM_BUGFRAME_TEXT(0)                                       \
    UNLIKELY_END_SECTION ";"                                    \
    "incsspq %q[val];"                                          \
    ".L_shstk_done.%=:"
#else
# define SHADOW_STACK_WORK ""
#endif

#if __GNUC__ >= 9
# define ssaj_has_attr_noreturn(fn) __builtin_has_attribute(fn, __noreturn__)
#else
/* Simply can't check the property with older gcc. */
# define ssaj_has_attr_noreturn(fn) true
#endif

#define switch_stack_and_jump(fn, instr, constr)                        \
    ({                                                                  \
        unsigned int tmp;                                               \
        (void)((fn) == (void (*)(void))NULL);                           \
        BUILD_BUG_ON(!ssaj_has_attr_noreturn(fn));                      \
        __asm__ __volatile__ (                                          \
            SHADOW_STACK_WORK                                           \
            "mov %[stk], %%rsp;"                                        \
            CHECK_FOR_LIVEPATCH_WORK                                    \
            instr "[fun]"                                               \
            : [val] "=&r" (tmp),                                        \
              [ssp] "=&r" (tmp)                                         \
            : [stk] "r" (guest_cpu_user_regs()),                        \
              [fun] constr (fn),                                        \
              [skstk_base] "i"                                          \
              ((PRIMARY_SHSTK_SLOT + 1) * PAGE_SIZE - 8),               \
              [stack_mask] "i" (STACK_SIZE - 1),                        \
              _ASM_BUGFRAME_INFO(BUGFRAME_bug, __LINE__,                \
                                 __FILE__, NULL)                        \
            : "memory" );                                               \
        unreachable();                                                  \
    })

#define reset_stack_and_jump(fn)                                        \
    switch_stack_and_jump(fn, "jmp %c", "i")

/* The constraint may only specify non-call-clobbered registers. */
#define reset_stack_and_jump_ind(fn)                                    \
    switch_stack_and_jump(fn, "INDIRECT_JMP %", "b")

/*
 * Which VCPU's state is currently running on each CPU?
 * This is not necesasrily the same as 'current' as a CPU may be
 * executing a lazy state switch.
 */
DECLARE_PER_CPU(struct vcpu *, curr_vcpu);

#endif /* __X86_CURRENT_H__ */
