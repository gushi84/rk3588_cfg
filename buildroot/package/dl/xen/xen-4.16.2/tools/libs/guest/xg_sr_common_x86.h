#ifndef __COMMON_X86__H
#define __COMMON_X86__H

#include "xg_sr_common.h"

/*
 * Obtains a domains TSC information from Xen and writes a X86_TSC_INFO record
 * into the stream.
 */
int write_x86_tsc_info(struct xc_sr_context *ctx);

/*
 * Parses a X86_TSC_INFO record and applies the result to the domain.
 */
int handle_x86_tsc_info(struct xc_sr_context *ctx, struct xc_sr_record *rec);

/*
 * Obtains a domains CPU Policy from Xen, and writes X86_{CPUID,MSR}_POLICY
 * records into the stream.
 */
int write_x86_cpu_policy_records(struct xc_sr_context *ctx);

/*
 * Parses an X86_CPUID_POLICY record and stashes the content for application
 * when a STATIC_DATA_END record is encountered.
 */
int handle_x86_cpuid_policy(struct xc_sr_context *ctx,
                            struct xc_sr_record *rec);

/*
 * Parses an X86_MSR_POLICY record and stashes the content for application
 * when a STATIC_DATA_END record is encountered.
 */
int handle_x86_msr_policy(struct xc_sr_context *ctx,
                          struct xc_sr_record *rec);

/*
 * Perform common x86 actions required after the static data has arrived.
 */
int x86_static_data_complete(struct xc_sr_context *ctx, unsigned int *missing);

#endif
/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
