/******************************************************************************
 * compat.c
 */

EMIT_FILE;

#include <xen/hypercall.h>
#include <compat/xen.h>
#include <compat/physdev.h>

#define physdev_op                    compat_physdev_op
#define physdev_op_t                  physdev_op_compat_t
#define do_physdev_op                 compat_physdev_op
#define do_physdev_op_compat(x)       compat_physdev_op_compat(_##x)
#define native                        compat

#define COMPAT
#define _XEN_GUEST_HANDLE(t) XEN_GUEST_HANDLE(t)
#define _XEN_GUEST_HANDLE_PARAM(t) XEN_GUEST_HANDLE_PARAM(t)
typedef int ret_t;

#include "../compat.c"

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
