
tests all: build


ifneq ($(XEN_TARGET_ARCH),x86_32)
# Xen 32-bit x86 hypervisor no longer supported, so has no test livepatches
subdir-y += livepatch
endif

install build subtree-force-update uninstall: %:
	set -e; for s in $(subdir-y); do \
		$(MAKE) -f $(BASEDIR)/Rules.mk -C $$s $*; \
	done
