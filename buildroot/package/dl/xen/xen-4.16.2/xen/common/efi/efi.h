#include <asm/efibind.h>
#include <efi/efidef.h>
#include <efi/efierr.h>
#include <efi/eficon.h>
#include <efi/efidevp.h>
#include <efi/eficapsule.h>
#include <efi/efiapi.h>
#include <xen/efi.h>
#include <xen/mm.h>
#include <xen/spinlock.h>
#include <asm/page.h>

struct efi_pci_rom {
    const struct efi_pci_rom *next;
    u16 vendor, devid, segment;
    u8 bus, devfn;
    unsigned long size;
    unsigned char data[];
};

extern unsigned int efi_num_ct;
extern const EFI_CONFIGURATION_TABLE *efi_ct;

extern unsigned int efi_version, efi_fw_revision;
extern const CHAR16 *efi_fw_vendor;

extern const EFI_RUNTIME_SERVICES *efi_rs;

extern UINTN efi_memmap_size, efi_mdesc_size;
extern void *efi_memmap;

#ifdef CONFIG_X86
extern mfn_t efi_l4_mfn;
#endif

extern const struct efi_pci_rom *efi_pci_roms;

extern UINT64 efi_boot_max_var_store_size, efi_boot_remain_var_store_size,
              efi_boot_max_var_size;

extern UINT64 efi_apple_properties_addr;
extern UINTN efi_apple_properties_len;

void noreturn blexit(const CHAR16 *str);

const CHAR16 *wmemchr(const CHAR16 *s, CHAR16 c, UINTN n);

/* EFI boot allocator. */
void *ebmalloc(size_t size);
void free_ebmalloc_unused_mem(void);

const void *pe_find_section(const void *image_base, const size_t image_size,
                            const CHAR16 *section_name, UINTN *size_out);
