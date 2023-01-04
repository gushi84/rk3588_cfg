#ifndef _UTILS_H
#define _UTILS_H
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <xen-tools/libs.h>

#include "xenstore_lib.h"

/* Header of the node record in tdb. */
struct xs_tdb_record_hdr {
	uint64_t generation;
	uint32_t num_perms;
	uint32_t datalen;
	uint32_t childlen;
	struct xs_permissions perms[0];
};

/* Is A == B ? */
#define streq(a,b) (strcmp((a),(b)) == 0)

/* Does A start with B ? */
#define strstarts(a,b) (strncmp((a),(b),strlen(b)) == 0)

/* Does A end in B ? */
static inline bool strends(const char *a, const char *b)
{
	if (strlen(a) < strlen(b))
		return false;

	return streq(a + strlen(a) - strlen(b), b);
}

/*
 * Write NUL bytes for aligning state data to 8 bytes.
 */
const char *dump_state_align(FILE *fp);

#define PRINTF_ATTRIBUTE(a1, a2) __attribute__((format (printf, a1, a2)))

#define __noreturn __attribute__((noreturn))

void barf(const char *fmt, ...) __noreturn PRINTF_ATTRIBUTE(1, 2);
void barf_perror(const char *fmt, ...) __noreturn PRINTF_ATTRIBUTE(1, 2);

/* Function pointer as xprintf() can be configured at runtime. */
extern void (*xprintf)(const char *fmt, ...) PRINTF_ATTRIBUTE(1, 2);

#define eprintf(_fmt, _args...) xprintf("[ERR] %s" _fmt, __FUNCTION__, ##_args)

/*
 * Mux errno values onto returned pointers.
 */

static inline void *ERR_PTR(long error)
{
	return (void *)error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long)ptr;
}

static inline long IS_ERR(const void *ptr)
{
	return ((unsigned long)ptr > (unsigned long)-1000L);
}


#endif /* _UTILS_H */

/*
 * Local variables:
 *  mode: C
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
