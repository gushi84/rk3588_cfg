#include <xen/init.h>
#include <xen/bitops.h>
#include <xen/mm.h>
#include <xen/param.h>
#include <xen/smp.h>
#include <xen/pci.h>
#include <xen/warning.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/amd.h>
#include <asm/hvm/support.h>
#include <asm/spec_ctrl.h>
#include <asm/acpi.h>
#include <asm/apic.h>

#include "cpu.h"

/*
 * Pre-canned values for overriding the CPUID features 
 * and extended features masks.
 *
 * Currently supported processors:
 * 
 * "fam_0f_rev_c"
 * "fam_0f_rev_d"
 * "fam_0f_rev_e"
 * "fam_0f_rev_f"
 * "fam_0f_rev_g"
 * "fam_10_rev_b"
 * "fam_10_rev_c"
 * "fam_11_rev_b"
 */
static char __initdata opt_famrev[14];
string_param("cpuid_mask_cpu", opt_famrev);

static unsigned int __initdata opt_cpuid_mask_l7s0_eax = ~0u;
integer_param("cpuid_mask_l7s0_eax", opt_cpuid_mask_l7s0_eax);
static unsigned int __initdata opt_cpuid_mask_l7s0_ebx = ~0u;
integer_param("cpuid_mask_l7s0_ebx", opt_cpuid_mask_l7s0_ebx);

static unsigned int __initdata opt_cpuid_mask_thermal_ecx = ~0u;
integer_param("cpuid_mask_thermal_ecx", opt_cpuid_mask_thermal_ecx);

/* 1 = allow, 0 = don't allow guest creation, -1 = don't allow boot */
s8 __read_mostly opt_allow_unsafe;
boolean_param("allow_unsafe", opt_allow_unsafe);

/* Signal whether the ACPI C1E quirk is required. */
bool __read_mostly amd_acpi_c1e_quirk;

static inline int rdmsr_amd_safe(unsigned int msr, unsigned int *lo,
				 unsigned int *hi)
{
	int err;

	asm volatile("1: rdmsr\n2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3: movl %6,%2\n"
		     "   jmp 2b\n"
		     ".previous\n"
		     _ASM_EXTABLE(1b, 3b)
		     : "=a" (*lo), "=d" (*hi), "=r" (err)
		     : "c" (msr), "D" (0x9c5a203a), "2" (0), "i" (-EFAULT));

	return err;
}

static inline int wrmsr_amd_safe(unsigned int msr, unsigned int lo,
				 unsigned int hi)
{
	int err;

	asm volatile("1: wrmsr\n2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3: movl %6,%0\n"
		     "   jmp 2b\n"
		     ".previous\n"
		     _ASM_EXTABLE(1b, 3b)
		     : "=r" (err)
		     : "c" (msr), "a" (lo), "d" (hi), "D" (0x9c5a203a),
		       "0" (0), "i" (-EFAULT));

	return err;
}

static void wrmsr_amd(unsigned int msr, uint64_t val)
{
	asm volatile("wrmsr" ::
		     "c" (msr), "a" ((uint32_t)val),
		     "d" (val >> 32), "D" (0x9c5a203a));
}

static const struct cpuidmask {
	uint16_t fam;
	char rev[2];
	unsigned int ecx, edx, ext_ecx, ext_edx;
} pre_canned[] __initconst = {
#define CAN(fam, id, rev) { \
		fam, #rev, \
		AMD_FEATURES_##id##_REV_##rev##_ECX, \
		AMD_FEATURES_##id##_REV_##rev##_EDX, \
		AMD_EXTFEATURES_##id##_REV_##rev##_ECX, \
		AMD_EXTFEATURES_##id##_REV_##rev##_EDX \
	}
#define CAN_FAM(fam, rev) CAN(0x##fam, FAM##fam##h, rev)
#define CAN_K8(rev)       CAN(0x0f,    K8,          rev)
	CAN_FAM(11, B),
	CAN_FAM(10, C),
	CAN_FAM(10, B),
	CAN_K8(G),
	CAN_K8(F),
	CAN_K8(E),
	CAN_K8(D),
	CAN_K8(C)
#undef CAN
};

static const struct cpuidmask *__init noinline get_cpuidmask(const char *opt)
{
	unsigned long fam;
	char rev;
	unsigned int i;

	if (strncmp(opt, "fam_", 4))
		return NULL;
	fam = simple_strtoul(opt + 4, &opt, 16);
	if (strncmp(opt, "_rev_", 5) || !opt[5] || opt[6])
		return NULL;
	rev = toupper(opt[5]);

	for (i = 0; i < ARRAY_SIZE(pre_canned); ++i)
		if (fam == pre_canned[i].fam && rev == *pre_canned[i].rev)
			return &pre_canned[i];

	return NULL;
}

/*
 * Sets caps in expected_levelling_cap, probes for the specified mask MSR, and
 * set caps in levelling_caps if it is found.  Processors prior to Fam 10h
 * required a 32-bit password for masking MSRs.  Returns the default value.
 */
static uint64_t __init _probe_mask_msr(unsigned int msr, uint64_t caps)
{
	unsigned int hi, lo;

	expected_levelling_cap |= caps;

	if ((rdmsr_amd_safe(msr, &lo, &hi) == 0) &&
	    (wrmsr_amd_safe(msr, lo, hi) == 0))
		levelling_caps |= caps;

	return ((uint64_t)hi << 32) | lo;
}

/*
 * Probe for the existance of the expected masking MSRs.  They might easily
 * not be available if Xen is running virtualised.
 */
static void __init noinline probe_masking_msrs(void)
{
	const struct cpuinfo_x86 *c = &boot_cpu_data;

	/*
	 * First, work out which masking MSRs we should have, based on
	 * revision and cpuid.
	 */

	/* Fam11 doesn't support masking at all. */
	if (c->x86 == 0x11)
		return;

	cpuidmask_defaults._1cd =
		_probe_mask_msr(MSR_K8_FEATURE_MASK, LCAP_1cd);
	cpuidmask_defaults.e1cd =
		_probe_mask_msr(MSR_K8_EXT_FEATURE_MASK, LCAP_e1cd);

	if (c->cpuid_level >= 7)
		cpuidmask_defaults._7ab0 =
			_probe_mask_msr(MSR_AMD_L7S0_FEATURE_MASK, LCAP_7ab0);

	if (c->x86 == 0x15 && c->cpuid_level >= 6 && cpuid_ecx(6))
		cpuidmask_defaults._6c =
			_probe_mask_msr(MSR_AMD_THRM_FEATURE_MASK, LCAP_6c);

	/*
	 * Don't bother warning about a mismatch if virtualised.  These MSRs
	 * are not architectural and almost never virtualised.
	 */
	if ((expected_levelling_cap == levelling_caps) ||
	    cpu_has_hypervisor)
		return;

	printk(XENLOG_WARNING "Mismatch between expected (%#x) "
	       "and real (%#x) levelling caps: missing %#x\n",
	       expected_levelling_cap, levelling_caps,
	       (expected_levelling_cap ^ levelling_caps) & levelling_caps);
	printk(XENLOG_WARNING "Fam %#x, model %#x level %#x\n",
	       c->x86, c->x86_model, c->cpuid_level);
	printk(XENLOG_WARNING
	       "If not running virtualised, please report a bug\n");
}

/*
 * Context switch CPUID masking state to the next domain.  Only called if
 * CPUID Faulting isn't available, but masking MSRs have been detected.  A
 * parameter of NULL is used to context switch to the default host state (by
 * the cpu bringup-code, crash path, etc).
 */
static void amd_ctxt_switch_masking(const struct vcpu *next)
{
	struct cpuidmasks *these_masks = &this_cpu(cpuidmasks);
	const struct domain *nextd = next ? next->domain : NULL;
	const struct cpuidmasks *masks =
		(nextd && is_pv_domain(nextd) && nextd->arch.pv.cpuidmasks)
		? nextd->arch.pv.cpuidmasks : &cpuidmask_defaults;

	if ((levelling_caps & LCAP_1cd) == LCAP_1cd) {
		uint64_t val = masks->_1cd;

		/*
		 * OSXSAVE defaults to 1, which causes fast-forwarding of
		 * Xen's real setting.  Clobber it if disabled by the guest
		 * kernel.
		 */
		if (next && is_pv_vcpu(next) && !is_idle_vcpu(next) &&
		    !(next->arch.pv.ctrlreg[4] & X86_CR4_OSXSAVE))
			val &= ~((uint64_t)cpufeat_mask(X86_FEATURE_OSXSAVE) << 32);

		if (unlikely(these_masks->_1cd != val)) {
			wrmsr_amd(MSR_K8_FEATURE_MASK, val);
			these_masks->_1cd = val;
		}
	}

#define LAZY(cap, msr, field)						\
	({								\
		if (unlikely(these_masks->field != masks->field) &&	\
		    ((levelling_caps & cap) == cap))			\
		{							\
			wrmsr_amd(msr, masks->field);			\
			these_masks->field = masks->field;		\
		}							\
	})

	LAZY(LCAP_e1cd, MSR_K8_EXT_FEATURE_MASK,   e1cd);
	LAZY(LCAP_7ab0, MSR_AMD_L7S0_FEATURE_MASK, _7ab0);
	LAZY(LCAP_6c,   MSR_AMD_THRM_FEATURE_MASK, _6c);

#undef LAZY
}

/*
 * Mask the features and extended features returned by CPUID.  Parameters are
 * set from the boot line via two methods:
 *
 *   1) Specific processor revision string
 *   2) User-defined masks
 *
 * The user-defined masks take precedence.
 *
 * AMD "masking msrs" are actually overrides, making it possible to advertise
 * features which are not supported by the hardware.  Care must be taken to
 * avoid this, as the accidentally-advertised features will not actually
 * function.
 */
static void __init noinline amd_init_levelling(void)
{
	const struct cpuidmask *m = NULL;

	if (probe_cpuid_faulting())
		return;

	probe_masking_msrs();

	if (*opt_famrev != '\0') {
		m = get_cpuidmask(opt_famrev);

		if (!m)
			printk("Invalid processor string: %s\n", opt_famrev);
	}

	if ((levelling_caps & LCAP_1cd) == LCAP_1cd) {
		uint32_t ecx, edx, tmp;

		cpuid(0x00000001, &tmp, &tmp, &ecx, &edx);

		if (~(opt_cpuid_mask_ecx & opt_cpuid_mask_edx)) {
			ecx &= opt_cpuid_mask_ecx;
			edx &= opt_cpuid_mask_edx;
		} else if (m) {
			ecx &= m->ecx;
			edx &= m->edx;
		}

		/* Fast-forward bits - Must be set. */
		if (ecx & cpufeat_mask(X86_FEATURE_XSAVE))
			ecx |= cpufeat_mask(X86_FEATURE_OSXSAVE);
		edx |= cpufeat_mask(X86_FEATURE_APIC);

		cpuidmask_defaults._1cd = ((uint64_t)ecx << 32) | edx;
	}

	if ((levelling_caps & LCAP_e1cd) == LCAP_e1cd) {
		uint32_t ecx, edx, tmp;

		cpuid(0x80000001, &tmp, &tmp, &ecx, &edx);

		if (~(opt_cpuid_mask_ext_ecx & opt_cpuid_mask_ext_edx)) {
			ecx &= opt_cpuid_mask_ext_ecx;
			edx &= opt_cpuid_mask_ext_edx;
		} else if (m) {
			ecx &= m->ext_ecx;
			edx &= m->ext_edx;
		}

		/* Fast-forward bits - Must be set. */
		edx |= cpufeat_mask(X86_FEATURE_APIC);

		cpuidmask_defaults.e1cd = ((uint64_t)ecx << 32) | edx;
	}

	if ((levelling_caps & LCAP_7ab0) == LCAP_7ab0) {
		uint32_t eax, ebx, tmp;

		cpuid(0x00000007, &eax, &ebx, &tmp, &tmp);

		if (~(opt_cpuid_mask_l7s0_eax & opt_cpuid_mask_l7s0_ebx)) {
			eax &= opt_cpuid_mask_l7s0_eax;
			ebx &= opt_cpuid_mask_l7s0_ebx;
		}

		cpuidmask_defaults._7ab0 &= ((uint64_t)eax << 32) | ebx;
	}

	if ((levelling_caps & LCAP_6c) == LCAP_6c) {
		uint32_t ecx = cpuid_ecx(6);

		if (~opt_cpuid_mask_thermal_ecx)
			ecx &= opt_cpuid_mask_thermal_ecx;

		cpuidmask_defaults._6c &= (~0ULL << 32) | ecx;
	}

	if (opt_cpu_info) {
		printk(XENLOG_INFO "Levelling caps: %#x\n", levelling_caps);
		printk(XENLOG_INFO
		       "MSR defaults: 1d 0x%08x, 1c 0x%08x, e1d 0x%08x, "
		       "e1c 0x%08x, 7a0 0x%08x, 7b0 0x%08x, 6c 0x%08x\n",
		       (uint32_t)cpuidmask_defaults._1cd,
		       (uint32_t)(cpuidmask_defaults._1cd >> 32),
		       (uint32_t)cpuidmask_defaults.e1cd,
		       (uint32_t)(cpuidmask_defaults.e1cd >> 32),
		       (uint32_t)(cpuidmask_defaults._7ab0 >> 32),
		       (uint32_t)cpuidmask_defaults._7ab0,
		       (uint32_t)cpuidmask_defaults._6c);
	}

	if (levelling_caps)
		ctxt_switch_masking = amd_ctxt_switch_masking;
}

/*
 * Check for the presence of an AMD erratum. Arguments are defined in amd.h 
 * for each known erratum. Return 1 if erratum is found.
 */
int cpu_has_amd_erratum(const struct cpuinfo_x86 *cpu, int osvw_id, ...)
{
	va_list ap;
	u32 range;
	u32 ms;
	
	if (cpu->x86_vendor != X86_VENDOR_AMD)
		return 0;

	if (osvw_id >= 0 && cpu_has(cpu, X86_FEATURE_OSVW)) {
		u64 osvw_len;

		rdmsrl(MSR_AMD_OSVW_ID_LENGTH, osvw_len);

		if (osvw_id < osvw_len) {
			u64 osvw_bits;

			rdmsrl(MSR_AMD_OSVW_STATUS + (osvw_id >> 6),
			       osvw_bits);

			return (osvw_bits >> (osvw_id & 0x3f)) & 1;
		}
	}

	/* OSVW unavailable or ID unknown, match family-model-stepping range */
	va_start(ap, osvw_id);

	ms = (cpu->x86_model << 4) | cpu->x86_mask;
	while ((range = va_arg(ap, int))) {
		if ((cpu->x86 == AMD_MODEL_RANGE_FAMILY(range)) &&
		    (ms >= AMD_MODEL_RANGE_START(range)) &&
		    (ms <= AMD_MODEL_RANGE_END(range))) {
			va_end(ap);
			return 1;
		}
	}

	va_end(ap);
	return 0;
}

/*
 * Disable C1-Clock ramping if enabled in PMM7.CpuLowPwrEnh on 8th-generation
 * cores only. Assume BIOS has setup all Northbridges equivalently.
 */
static void disable_c1_ramping(void) 
{
	u8 pmm7;
	int node, nr_nodes;

	/* Read the number of nodes from the first Northbridge. */
	nr_nodes = ((pci_conf_read32(PCI_SBDF(0, 0, 0x18, 0), 0x60) >> 4) &
		    0x07) + 1;
	for (node = 0; node < nr_nodes; node++) {
		/* PMM7: bus=0, dev=0x18+node, function=0x3, register=0x87. */
		pmm7 = pci_conf_read8(PCI_SBDF(0, 0, 0x18 + node, 3), 0x87);
		/* Invalid read means we've updated every Northbridge. */
		if (pmm7 == 0xFF)
			break;
		pmm7 &= 0xFC; /* clear pmm7[1:0] */
		pci_conf_write8(PCI_SBDF(0, 0, 0x18 + node, 3), 0x87, pmm7);
		printk ("AMD: Disabling C1 Clock Ramping Node #%x\n", node);
	}
}

static void disable_c1e(void *unused)
{
	uint64_t msr_content;

	/*
	 * Disable C1E mode, as the APIC timer stops in that mode.
	 * The MSR does not exist in all FamilyF CPUs (only Rev F and above),
	 * but we safely catch the #GP in that case.
	 */
	if ((rdmsr_safe(MSR_K8_ENABLE_C1E, msr_content) == 0) &&
	    (msr_content & (3ULL << 27)) &&
	    (wrmsr_safe(MSR_K8_ENABLE_C1E, msr_content & ~(3ULL << 27)) != 0))
		printk(KERN_ERR "Failed to disable C1E on CPU#%u (%16"PRIx64")\n",
		       smp_processor_id(), msr_content);
}

void amd_check_disable_c1e(unsigned int port, u8 value)
{
	/* C1E is sometimes enabled during entry to ACPI mode. */
	if ((port == acpi_smi_cmd) && (value == acpi_enable_value))
		on_each_cpu(disable_c1e, NULL, 1);
}

/*
 * BIOS is expected to clear MtrrFixDramModEn bit. According to AMD BKDG : 
 * "The MtrrFixDramModEn bit should be set to 1 during BIOS initalization of 
 * the fixed MTRRs, then cleared to 0 for operation."
 */
static void check_syscfg_dram_mod_en(void)
{
	uint64_t syscfg;
	static bool_t printed = 0;

	if (!((boot_cpu_data.x86_vendor == X86_VENDOR_AMD) &&
		(boot_cpu_data.x86 >= 0x0f)))
		return;

	rdmsrl(MSR_K8_SYSCFG, syscfg);
	if (!(syscfg & SYSCFG_MTRR_FIX_DRAM_MOD_EN))
		return;

	if (!test_and_set_bool(printed))
		printk(KERN_ERR "MTRR: SYSCFG[MtrrFixDramModEn] not "
			"cleared by BIOS, clearing this bit\n");

	syscfg &= ~SYSCFG_MTRR_FIX_DRAM_MOD_EN;
	wrmsrl(MSR_K8_SYSCFG, syscfg);
}

static void amd_get_topology(struct cpuinfo_x86 *c)
{
        int cpu;
        unsigned bits;

        if (c->x86_max_cores <= 1)
                return;
        /*
         * On a AMD multi core setup the lower bits of the APIC id
         * distingush the cores.
         */
        cpu = smp_processor_id();
        bits = (cpuid_ecx(0x80000008) >> 12) & 0xf;

        if (bits == 0) {
                while ((1 << bits) < c->x86_max_cores)
                        bits++;
        }

        /* Low order bits define the core id */
        c->cpu_core_id = c->phys_proc_id & ((1<<bits)-1);
        /* Convert local APIC ID into the socket ID */
        c->phys_proc_id >>= bits;
        /* Collect compute unit ID if available */
        if (cpu_has(c, X86_FEATURE_TOPOEXT)) {
                u32 eax, ebx, ecx, edx;

                cpuid(0x8000001e, &eax, &ebx, &ecx, &edx);
                c->x86_num_siblings = ((ebx >> 8) & 0xff) + 1;

                if (c->x86 < 0x17)
                        c->compute_unit_id = ebx & 0xFF;
                else {
                        c->cpu_core_id = ebx & 0xFF;
                        c->x86_max_cores /= c->x86_num_siblings;
                }

                /*
                 * In case leaf B is available, use it to derive
                 * topology information.
                 */
                if (detect_extended_topology(c))
                        return;
        }
        
        if (opt_cpu_info)
                printk("CPU %d(%d) -> Processor %d, %s %d\n",
                       cpu, c->x86_max_cores, c->phys_proc_id,
                       c->compute_unit_id != INVALID_CUID ? "Compute Unit"
                                                          : "Core",
                       c->compute_unit_id != INVALID_CUID ? c->compute_unit_id
                                                          : c->cpu_core_id);
}

void amd_log_freq(const struct cpuinfo_x86 *c)
{
	unsigned int idx = 0, h;
	uint64_t hi, lo, val;

	if (c->x86 < 0x10 || c->x86 > 0x19 ||
	    (c != &boot_cpu_data &&
	     (!opt_cpu_info || (c->apicid & (c->x86_num_siblings - 1)))))
		return;

	if (c->x86 < 0x17) {
		unsigned int node = 0;
		uint64_t nbcfg;

		/*
		 * Make an attempt at determining the node ID, but assume
		 * symmetric setup (using node 0) if this fails.
		 */
		if (c->extended_cpuid_level >= 0x8000001e &&
		    cpu_has(c, X86_FEATURE_TOPOEXT)) {
			node = cpuid_ecx(0x8000001e) & 0xff;
			if (node > 7)
				node = 0;
		} else if (cpu_has(c, X86_FEATURE_NODEID_MSR)) {
			rdmsrl(0xC001100C, val);
			node = val & 7;
		}

		/*
		 * Enable (and use) Extended Config Space accesses, as we
		 * can't be certain that MCFG is available here during boot.
		 */
		rdmsrl(MSR_AMD64_NB_CFG, nbcfg);
		wrmsrl(MSR_AMD64_NB_CFG,
		       nbcfg | (1ULL << AMD64_NB_CFG_CF8_EXT_ENABLE_BIT));
#define PCI_ECS_ADDRESS(sbdf, reg) \
    (0x80000000 | ((sbdf).bdf << 8) | ((reg) & 0xfc) | (((reg) & 0xf00) << 16))

		for ( ; ; ) {
			pci_sbdf_t sbdf = PCI_SBDF(0, 0, 0x18 | node, 4);

			switch (pci_conf_read32(sbdf, PCI_VENDOR_ID)) {
			case 0x00000000:
			case 0xffffffff:
				/* No device at this SBDF. */
				if (!node)
					break;
				node = 0;
				continue;

			default:
				/*
				 * Core Performance Boost Control, family
				 * dependent up to 3 bits starting at bit 2.
				 *
				 * Note that boost states operate at a frequency
				 * above the base one, and thus need to be
				 * accounted for in order to correctly fetch the
				 * nominal frequency of the processor.
				 */
				switch (c->x86) {
				case 0x10: idx = 1; break;
				case 0x12: idx = 7; break;
				case 0x14: idx = 7; break;
				case 0x15: idx = 7; break;
				case 0x16: idx = 7; break;
				}
				idx &= pci_conf_read(PCI_ECS_ADDRESS(sbdf,
				                                     0x15c),
				                     0, 4) >> 2;
				break;
			}
			break;
		}

#undef PCI_ECS_ADDRESS
		wrmsrl(MSR_AMD64_NB_CFG, nbcfg);
	}

	lo = 0; /* gcc may not recognize the loop having at least 5 iterations */
	for (h = c->x86 == 0x10 ? 5 : 8; h--; )
		if (!rdmsr_safe(0xC0010064 + h, lo) && (lo >> 63))
			break;
	if (!(lo >> 63))
		return;

#define FREQ(v) (c->x86 < 0x17 ? ((((v) & 0x3f) + 0x10) * 100) >> (((v) >> 6) & 7) \
		                     : (((v) & 0xff) * 25 * 8) / (((v) >> 8) & 0x3f))
	if (idx && idx < h &&
	    !rdmsr_safe(0xC0010064 + idx, val) && (val >> 63) &&
	    !rdmsr_safe(0xC0010064, hi) && (hi >> 63))
		printk("CPU%u: %lu (%lu ... %lu) MHz\n",
		       smp_processor_id(), FREQ(val), FREQ(lo), FREQ(hi));
	else if (h && !rdmsr_safe(0xC0010064, hi) && (hi >> 63))
		printk("CPU%u: %lu ... %lu MHz\n",
		       smp_processor_id(), FREQ(lo), FREQ(hi));
	else
		printk("CPU%u: %lu MHz\n", smp_processor_id(), FREQ(lo));
#undef FREQ
}

void early_init_amd(struct cpuinfo_x86 *c)
{
	if (c == &boot_cpu_data)
		amd_init_levelling();

	ctxt_switch_levelling(NULL);
}

void amd_init_lfence(struct cpuinfo_x86 *c)
{
	uint64_t value;

	/*
	 * Some hardware has LFENCE dispatch serialising always enabled,
	 * nothing to do on that case.
	 */
	if (test_bit(X86_FEATURE_LFENCE_DISPATCH, c->x86_capability))
		return;

	/*
	 * Attempt to set lfence to be Dispatch Serialising.  This MSR almost
	 * certainly isn't virtualised (and Xen at least will leak the real
	 * value in but silently discard writes), as well as being per-core
	 * rather than per-thread, so do a full safe read/write/readback cycle
	 * in the worst case.
	 */
	if (rdmsr_safe(MSR_AMD64_DE_CFG, value))
		/* Unable to read.  Assume the safer default. */
		__clear_bit(X86_FEATURE_LFENCE_DISPATCH,
			    c->x86_capability);
	else if (value & AMD64_DE_CFG_LFENCE_SERIALISE)
		/* Already dispatch serialising. */
		__set_bit(X86_FEATURE_LFENCE_DISPATCH,
			  c->x86_capability);
	else if (wrmsr_safe(MSR_AMD64_DE_CFG,
			    value | AMD64_DE_CFG_LFENCE_SERIALISE) ||
		 rdmsr_safe(MSR_AMD64_DE_CFG, value) ||
		 !(value & AMD64_DE_CFG_LFENCE_SERIALISE))
		/* Attempt to set failed.  Assume the safer default. */
		__clear_bit(X86_FEATURE_LFENCE_DISPATCH,
			    c->x86_capability);
	else
		/* Successfully enabled! */
		__set_bit(X86_FEATURE_LFENCE_DISPATCH,
			  c->x86_capability);
}

/*
 * Refer to the AMD Speculative Store Bypass whitepaper:
 * https://developer.amd.com/wp-content/resources/124441_AMD64_SpeculativeStoreBypassDisable_Whitepaper_final.pdf
 */
void amd_init_ssbd(const struct cpuinfo_x86 *c)
{
	int bit = -1;

	if (cpu_has_ssb_no)
		return;

	if (cpu_has_amd_ssbd) {
		/* Handled by common MSR_SPEC_CTRL logic */
		return;
	}

	if (cpu_has_virt_ssbd) {
		wrmsrl(MSR_VIRT_SPEC_CTRL, opt_ssbd ? SPEC_CTRL_SSBD : 0);
		return;
	}

	switch (c->x86) {
	case 0x15: bit = 54; break;
	case 0x16: bit = 33; break;
	case 0x17:
	case 0x18: bit = 10; break;
	}

	if (bit >= 0) {
		uint64_t val, mask = 1ull << bit;

		if (rdmsr_safe(MSR_AMD64_LS_CFG, val) ||
		    ({
			    val &= ~mask;
			    if (opt_ssbd)
				    val |= mask;
			    false;
		    }) ||
		    wrmsr_safe(MSR_AMD64_LS_CFG, val) ||
		    ({
			    rdmsrl(MSR_AMD64_LS_CFG, val);
			    (val & mask) != (opt_ssbd * mask);
		    }))
			bit = -1;
	}

	if (bit < 0)
		printk_once(XENLOG_ERR "No SSBD controls available\n");
}

/*
 * On Zen2 we offer this chicken (bit) on the altar of Speculation.
 *
 * Refer to the AMD Branch Type Confusion whitepaper:
 * https://XXX
 *
 * Setting this unnamed bit supposedly causes prediction information on
 * non-branch instructions to be ignored.  It is to be set unilaterally in
 * newer microcode.
 *
 * This chickenbit is something unrelated on Zen1, and Zen1 vs Zen2 isn't a
 * simple model number comparison, so use STIBP as a heuristic to separate the
 * two uarches in Fam17h(AMD)/18h(Hygon).
 */
void amd_init_spectral_chicken(void)
{
	uint64_t val, chickenbit = 1 << 1;

	if (cpu_has_hypervisor || !boot_cpu_has(X86_FEATURE_AMD_STIBP))
		return;

	if (rdmsr_safe(MSR_AMD64_DE_CFG2, val) == 0 && !(val & chickenbit))
		wrmsr_safe(MSR_AMD64_DE_CFG2, val | chickenbit);
}

void __init detect_zen2_null_seg_behaviour(void)
{
	uint64_t base;

	wrmsrl(MSR_FS_BASE, 1);
	asm volatile ( "mov %0, %%fs" :: "rm" (0) );
	rdmsrl(MSR_FS_BASE, base);

	if (base == 0)
		setup_force_cpu_cap(X86_FEATURE_NSCB);

}

static void init_amd(struct cpuinfo_x86 *c)
{
	u32 l, h;

	unsigned long long value;

	/* Disable TLB flush filter by setting HWCR.FFDIS on K8
	 * bit 6 of msr C001_0015
	 *
	 * Errata 63 for SH-B3 steppings
	 * Errata 122 for all steppings (F+ have it disabled by default)
	 */
	if (c->x86 == 15) {
		rdmsrl(MSR_K8_HWCR, value);
		value |= 1 << 6;
		wrmsrl(MSR_K8_HWCR, value);
	}

	/*
	 * Some AMD CPUs duplicate the 3DNow bit in base and extended CPUID
	 * leaves.  Unfortunately, this aliases PBE on Intel CPUs. Clobber the
	 * alias, leaving 3DNow in the extended leaf.
	 */
	__clear_bit(X86_FEATURE_PBE, c->x86_capability);
	
	if (c->x86 == 0xf && c->x86_model < 0x14
	    && cpu_has(c, X86_FEATURE_LAHF_LM)) {
		/*
		 * Some BIOSes incorrectly force this feature, but only K8
		 * revision D (model = 0x14) and later actually support it.
		 * (AMD Erratum #110, docId: 25759).
		 */
		__clear_bit(X86_FEATURE_LAHF_LM, c->x86_capability);
		if (!rdmsr_amd_safe(0xc001100d, &l, &h))
			wrmsr_amd_safe(0xc001100d, l, h & ~1);
	}

	/*
	 * Older AMD CPUs don't save/load FOP/FIP/FDP unless an FPU exception
	 * is pending.  Xen works around this at (F)XRSTOR time.
	 */
	if (c == &boot_cpu_data && !cpu_has(c, X86_FEATURE_RSTR_FP_ERR_PTRS))
		setup_force_cpu_cap(X86_BUG_FPU_PTRS);

	if (c->x86 == 0x0f || c->x86 == 0x11)
		/* Always dispatch serialising on this hardare. */
		__set_bit(X86_FEATURE_LFENCE_DISPATCH, c->x86_capability);
	else /* Implicily "== 0x10 || >= 0x12" by being 64bit. */
		amd_init_lfence(c);

	amd_init_ssbd(c);

	if (c->x86 == 0x17)
		amd_init_spectral_chicken();

	/* Probe for NSCB on Zen2 CPUs when not virtualised */
	if (!cpu_has_hypervisor && !cpu_has_nscb && c == &boot_cpu_data &&
	    c->x86 == 0x17)
		detect_zen2_null_seg_behaviour();

	/*
	 * AMD CPUs before Zen2 don't clear segment bases/limits when loading
	 * a NULL selector.
	 */
	if (c == &boot_cpu_data && !cpu_has_nscb)
		setup_force_cpu_cap(X86_BUG_NULL_SEG);

	/* MFENCE stops RDTSC speculation */
	if (!cpu_has_lfence_dispatch)
		__set_bit(X86_FEATURE_MFENCE_RDTSC, c->x86_capability);

	/*
	 * On pre-CLFLUSHOPT AMD CPUs, CLFLUSH is weakly ordered with
	 * everything, including reads and writes to address, and
	 * LFENCE/SFENCE instructions.
	 */
	if (c == &boot_cpu_data && !cpu_has_clflushopt)
		setup_force_cpu_cap(X86_BUG_CLFLUSH_MFENCE);

	switch(c->x86)
	{
	case 0xf ... 0x11:
		disable_c1e(NULL);
		if (acpi_smi_cmd && (acpi_enable_value | acpi_disable_value))
			amd_acpi_c1e_quirk = true;
		break;

	case 0x15: case 0x16:
		/*
		 * There are some Fam15/Fam16 systems where upon resume from S3
		 * firmware fails to re-setup properly functioning RDRAND.
		 * By the time we can spot the problem, it is too late to take
		 * action, and there is nothing Xen can do to repair the problem.
		 * Clear the feature unless force-enabled on the command line.
		 */
		if (c == &boot_cpu_data &&
		    cpu_has(c, X86_FEATURE_RDRAND) &&
		    !is_forced_cpu_cap(X86_FEATURE_RDRAND)) {
			static const char __initconst text[] =
				"RDRAND may cease to work on this hardware upon resume from S3.\n"
				"Please choose an explicit cpuid={no-}rdrand setting.\n";

			setup_clear_cpu_cap(X86_FEATURE_RDRAND);
			warning_add(text);
		}
		break;

	case 0x19:
		/*
		 * Zen3 (Fam19h model < 0x10) parts are not susceptible to
		 * Branch Type Confusion, but predate the allocation of the
		 * BTC_NO bit.  Fill it back in if we're not virtualised.
		 */
		if (!cpu_has_hypervisor && !cpu_has(c, X86_FEATURE_BTC_NO))
			__set_bit(X86_FEATURE_BTC_NO, c->x86_capability);
		break;
	}

	display_cacheinfo(c);

	if (c->extended_cpuid_level >= 0x80000008) {
		c->x86_max_cores = (cpuid_ecx(0x80000008) & 0xff) + 1;
	}

	if (c->extended_cpuid_level >= 0x80000007) {
		if (cpu_has(c, X86_FEATURE_ITSC)) {
			__set_bit(X86_FEATURE_CONSTANT_TSC, c->x86_capability);
			__set_bit(X86_FEATURE_NONSTOP_TSC, c->x86_capability);
			if (c->x86 != 0x11)
				__set_bit(X86_FEATURE_TSC_RELIABLE,
					  c->x86_capability);
		}
	}

	/* re-enable TopologyExtensions if switched off by BIOS */
	if ((c->x86 == 0x15) &&
	    (c->x86_model >= 0x10) && (c->x86_model <= 0x1f) &&
	    !cpu_has(c, X86_FEATURE_TOPOEXT) &&
	    !rdmsr_safe(MSR_K8_EXT_FEATURE_MASK, value)) {
		value |= 1ULL << 54;
		wrmsr_safe(MSR_K8_EXT_FEATURE_MASK, value);
		rdmsrl(MSR_K8_EXT_FEATURE_MASK, value);
		if (value & (1ULL << 54)) {
			__set_bit(X86_FEATURE_TOPOEXT, c->x86_capability);
			printk(KERN_INFO "CPU: Re-enabling disabled "
			       "Topology Extensions Support\n");
		}
	}

	/*
	 * The way access filter has a performance penalty on some workloads.
	 * Disable it on the affected CPUs.
	 */
	if (c->x86 == 0x15 && c->x86_model >= 0x02 && c->x86_model < 0x20 &&
	    !rdmsr_safe(MSR_AMD64_IC_CFG, value) && (value & 0x1e) != 0x1e)
		wrmsr_safe(MSR_AMD64_IC_CFG, value | 0x1e);

        amd_get_topology(c);

	/* Pointless to use MWAIT on Family10 as it does not deep sleep. */
	if (c->x86 == 0x10)
		__clear_bit(X86_FEATURE_MONITOR, c->x86_capability);

	if (!cpu_has_amd_erratum(c, AMD_ERRATUM_121))
		opt_allow_unsafe = 1;
	else if (opt_allow_unsafe < 0)
		panic("Xen will not boot on this CPU for security reasons"
		      "Pass \"allow_unsafe\" if you're trusting all your"
		      " (PV) guest kernels.\n");
	else if (!opt_allow_unsafe && c == &boot_cpu_data)
		printk(KERN_WARNING
		       "*** Xen will not allow creation of DomU-s on"
		       " this CPU for security reasons. ***\n"
		       KERN_WARNING
		       "*** Pass \"allow_unsafe\" if you're trusting"
		       " all your (PV) guest kernels. ***\n");

	if (c->x86 == 0x16 && c->x86_model <= 0xf) {
		if (c == &boot_cpu_data) {
			l = pci_conf_read32(PCI_SBDF(0, 0, 0x18, 3), 0x58);
			h = pci_conf_read32(PCI_SBDF(0, 0, 0x18, 3), 0x5c);
			if ((l & 0x1f) | (h & 0x1))
				printk(KERN_WARNING
				       "Applying workaround for erratum 792: %s%s%s\n",
				       (l & 0x1f) ? "clearing D18F3x58[4:0]" : "",
				       ((l & 0x1f) && (h & 0x1)) ? " and " : "",
				       (h & 0x1) ? "clearing D18F3x5C[0]" : "");

			if (l & 0x1f)
				pci_conf_write32(PCI_SBDF(0, 0, 0x18, 3), 0x58,
						 l & ~0x1f);

			if (h & 0x1)
				pci_conf_write32(PCI_SBDF(0, 0, 0x18, 3), 0x5c,
						 h & ~0x1);
		}

		rdmsrl(MSR_AMD64_LS_CFG, value);
		if (!(value & (1 << 15))) {
			static bool_t warned;

			if (c == &boot_cpu_data || opt_cpu_info ||
			    !test_and_set_bool(warned))
				printk(KERN_WARNING
				       "CPU%u: Applying workaround for erratum 793\n",
				       smp_processor_id());
			wrmsrl(MSR_AMD64_LS_CFG, value | (1 << 15));
		}
	} else if (c->x86 == 0x12) {
		rdmsrl(MSR_AMD64_DE_CFG, value);
		if (!(value & (1U << 31))) {
			static bool warned;

			if (c == &boot_cpu_data || opt_cpu_info ||
			    !test_and_set_bool(warned))
				printk(KERN_WARNING
				       "CPU%u: Applying workaround for erratum 665\n",
				       smp_processor_id());
			wrmsrl(MSR_AMD64_DE_CFG, value | (1U << 31));
		}
	}

	/* AMD CPUs do not support SYSENTER outside of legacy mode. */
	__clear_bit(X86_FEATURE_SEP, c->x86_capability);

	if (c->x86 == 0x10) {
		/* do this for boot cpu */
		if (c == &boot_cpu_data)
			check_enable_amd_mmconf_dmi();

		fam10h_check_enable_mmcfg();

		/*
		 * On family 10h BIOS may not have properly enabled WC+
		 * support, causing it to be converted to CD memtype. This may
		 * result in performance degradation for certain nested-paging
		 * guests. Prevent this conversion by clearing bit 24 in
		 * MSR_F10_BU_CFG2.
		 */
		rdmsrl(MSR_F10_BU_CFG2, value);
		value &= ~(1ULL << 24);
		wrmsrl(MSR_F10_BU_CFG2, value);
	}

	/*
	 * Family 0x12 and above processors have APIC timer
	 * running in deep C states.
	 */
	if ( opt_arat && c->x86 > 0x11 )
		__set_bit(X86_FEATURE_ARAT, c->x86_capability);

	/*
	 * Prior to Family 0x14, perf counters are not reset during warm reboot.
	 * We have to reset them manually.
	 */
	if (nmi_watchdog != NMI_LOCAL_APIC && c->x86 < 0x14) {
		wrmsrl(MSR_K7_PERFCTR0, 0);
		wrmsrl(MSR_K7_PERFCTR1, 0);
		wrmsrl(MSR_K7_PERFCTR2, 0);
		wrmsrl(MSR_K7_PERFCTR3, 0);
	}

	if (cpu_has(c, X86_FEATURE_EFRO)) {
		rdmsr(MSR_K8_HWCR, l, h);
		l |= (1 << 27); /* Enable read-only APERF/MPERF bit */
		wrmsr(MSR_K8_HWCR, l, h);
	}

	/* Prevent TSC drift in non single-processor, single-core platforms. */
	if ((smp_processor_id() == 1) && !cpu_has(c, X86_FEATURE_ITSC))
		disable_c1_ramping();

	check_syscfg_dram_mod_en();

	amd_log_freq(c);
}

const struct cpu_dev amd_cpu_dev = {
	.c_early_init	= early_init_amd,
	.c_init		= init_amd,
};
