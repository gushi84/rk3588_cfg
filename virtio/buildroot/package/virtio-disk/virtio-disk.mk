################################################################################
#
# Virtio-disk
#
################################################################################

VIRTIO_DISK_VERSION:= 1.0.0
VIRTIO_DISK_SITE:= /home/test/work/RK3588_SDK/RK3588_linux/rk3588_linux_domu/buildroot/dl/virtio-disk
VIRTIO_DISK_SITE_METHOD:=local
VIRTIO_DISK_INSTALL_TARGET:=YES

define VIRTIO_DISK_BUILD_CMDS
	$(MAKE) CC="$(TARGET_CC)" LD="$(TARGET_LD)" -C $(@D) all
endef

define VIRTIO_DISK_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/virtio-disk $(TARGET_DIR)/bin
endef

define VIRTIO_DISK_PERMISSIONS
	/bin/virtio-disk f 4755 0 0 - - - - -
endef

$(eval $(generic-package))
