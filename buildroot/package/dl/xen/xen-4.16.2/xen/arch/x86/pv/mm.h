#ifndef __PV_MM_H__
#define __PV_MM_H__

l1_pgentry_t *map_guest_l1e(unsigned long linear, mfn_t *gl1mfn);

int new_guest_cr3(mfn_t mfn);

/*
 * Read the guest's l1e that maps this address, from the kernel-mode
 * page tables.
 */
static inline l1_pgentry_t guest_get_eff_kern_l1e(unsigned long linear)
{
    struct vcpu *curr = current;
    bool user_mode = !(curr->arch.flags & TF_kernel_mode);
    l1_pgentry_t l1e;

    ASSERT(!paging_mode_translate(curr->domain));
    ASSERT(!paging_mode_external(curr->domain));

    if ( user_mode )
        toggle_guest_pt(curr);

    if ( unlikely(!__addr_ok(linear)) ||
         get_unsafe(l1e, &__linear_l1_table[l1_linear_offset(linear)]) )
        l1e = l1e_empty();

    if ( user_mode )
        toggle_guest_pt(curr);

    return l1e;
}

/*
 * PTE updates can be done with ordinary writes except:
 *  1. Debug builds get extra checking by using CMPXCHG[8B].
 */
#ifndef NDEBUG
#define PTE_UPDATE_WITH_CMPXCHG
#else
#undef PTE_UPDATE_WITH_CMPXCHG
#endif

/*
 * How to write an entry to the guest pagetables.
 * Returns false for failure (pointer not valid), true for success.
 */
static inline bool update_intpte(intpte_t *p, intpte_t old, intpte_t new,
                                 mfn_t mfn, struct vcpu *v, bool preserve_ad)
{
    bool rv = true;

#ifndef PTE_UPDATE_WITH_CMPXCHG
    if ( !preserve_ad )
        paging_write_guest_entry(v, p, new, mfn);
    else
#endif
    {
        for ( ; ; )
        {
            intpte_t _new = new, t;

            if ( preserve_ad )
                _new |= old & (_PAGE_ACCESSED | _PAGE_DIRTY);

            t = paging_cmpxchg_guest_entry(v, p, old, _new, mfn);

            if ( t == old )
                break;

            /* Allowed to change in Accessed/Dirty flags only. */
            BUG_ON((t ^ old) & ~(intpte_t)(_PAGE_ACCESSED|_PAGE_DIRTY));

            old = t;
        }
    }
    return rv;
}

/*
 * Macro that wraps the appropriate type-changes around update_intpte().
 * Arguments are: type, ptr, old, new, mfn, vcpu
 */
#define UPDATE_ENTRY(_t,_p,_o,_n,_m,_v,_ad)                         \
    update_intpte(&_t ## e_get_intpte(*(_p)),                       \
                  _t ## e_get_intpte(_o), _t ## e_get_intpte(_n),   \
                  (_m), (_v), (_ad))

static always_inline l1_pgentry_t adjust_guest_l1e(l1_pgentry_t l1e,
                                                   const struct domain *d)
{
    if ( likely(l1e_get_flags(l1e) & _PAGE_PRESENT) &&
         likely(!is_pv_32bit_domain(d)) )
    {
        /* _PAGE_GUEST_KERNEL page cannot have the Global bit set. */
        if ( (l1e_get_flags(l1e) & (_PAGE_GUEST_KERNEL | _PAGE_GLOBAL)) ==
             (_PAGE_GUEST_KERNEL | _PAGE_GLOBAL) )
            gdprintk(XENLOG_WARNING, "Global bit is set in kernel page %lx\n",
                     l1e_get_pfn(l1e));

        if ( !(l1e_get_flags(l1e) & _PAGE_USER) )
            l1e_add_flags(l1e, (_PAGE_GUEST_KERNEL | _PAGE_USER));

        if ( !(l1e_get_flags(l1e) & _PAGE_GUEST_KERNEL) )
            l1e_add_flags(l1e, (_PAGE_GLOBAL | _PAGE_USER));
    }

    return l1e;
}

static always_inline l2_pgentry_t adjust_guest_l2e(l2_pgentry_t l2e,
                                                   const struct domain *d)
{
    if ( likely(l2e_get_flags(l2e) & _PAGE_PRESENT) &&
         likely(!is_pv_32bit_domain(d)) )
        l2e_add_flags(l2e, _PAGE_USER);

    return l2e;
}

static always_inline l3_pgentry_t adjust_guest_l3e(l3_pgentry_t l3e,
                                                   const struct domain *d)
{
    if ( likely(l3e_get_flags(l3e) & _PAGE_PRESENT) )
        l3e_add_flags(l3e, (likely(!is_pv_32bit_domain(d))
                            ? _PAGE_USER : _PAGE_USER | _PAGE_RW));

    return l3e;
}

static always_inline l3_pgentry_t unadjust_guest_l3e(l3_pgentry_t l3e,
                                                     const struct domain *d)
{
    if ( unlikely(is_pv_32bit_domain(d)) &&
         likely(l3e_get_flags(l3e) & _PAGE_PRESENT) )
        l3e_remove_flags(l3e, _PAGE_USER | _PAGE_RW | _PAGE_ACCESSED);

    return l3e;
}

static always_inline l4_pgentry_t adjust_guest_l4e(l4_pgentry_t l4e,
                                                   const struct domain *d)
{
    /*
     * When shadowing an L4 behind the guests back (e.g. for per-pcpu
     * purposes), we cannot efficiently sync access bit updates from hardware
     * (on the shadow tables) back into the guest view.
     *
     * We therefore unconditionally set _PAGE_ACCESSED even in the guests
     * view.  This will appear to the guest as a CPU which proactively pulls
     * all valid L4e's into its TLB, which is compatible with the x86 ABI.
     *
     * At the time of writing, all PV guests set the access bit anyway, so
     * this is no actual change in their behaviour.
     */
    if ( likely(l4e_get_flags(l4e) & _PAGE_PRESENT) )
        l4e_add_flags(l4e, (_PAGE_ACCESSED |
                            (is_pv_32bit_domain(d) ? 0 : _PAGE_USER)));

    return l4e;
}

#endif /* __PV_MM_H__ */
