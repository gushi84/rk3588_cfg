obj-y += avc.o
obj-y += hooks.o
obj-y += flask_op.o

obj-y += ss/

CFLAGS-y += -I./include

AWK = awk

FLASK_H_DEPEND = policy/security_classes policy/initial_sids
AV_H_DEPEND = policy/access_vectors

FLASK_H_FILES = include/flask.h include/class_to_string.h include/initial_sid_to_string.h
AV_H_FILES = include/av_perm_to_string.h include/av_permissions.h
ALL_H_FILES = $(FLASK_H_FILES) $(AV_H_FILES)

$(obj-y) ss/built_in.o: $(ALL_H_FILES)
extra-y += $(ALL_H_FILES)

mkflask := policy/mkflask.sh
quiet_cmd_mkflask = MKFLASK $@
cmd_mkflask = $(SHELL) $(mkflask) $(AWK) include $(FLASK_H_DEPEND)

$(subst include/,%/,$(FLASK_H_FILES)): $(FLASK_H_DEPEND) $(mkflask) FORCE
	$(call if_changed,mkflask)

mkaccess := policy/mkaccess_vector.sh
quiet_cmd_mkaccess = MKACCESS VECTOR $@
cmd_mkaccess = $(SHELL) $(mkaccess) $(AWK) $(AV_H_DEPEND)

$(subst include/,%/,$(AV_H_FILES)): $(AV_H_DEPEND) $(mkaccess) FORCE
	$(call if_changed,mkaccess)

obj-bin-$(CONFIG_XSM_FLASK_POLICY) += flask-policy.o
flask-policy.o: policy.bin

flask-policy.S: BINFILE_FLAGS := -i
flask-policy.S: $(BASEDIR)/tools/binfile FORCE
	$(call if_changed,binfile,policy.bin xsm_flask_init_policy)
targets += flask-policy.S

FLASK_BUILD_DIR := $(CURDIR)
POLICY_SRC := $(FLASK_BUILD_DIR)/xenpolicy-$(XEN_FULLVERSION)

policy.bin: FORCE
	$(MAKE) -f $(XEN_ROOT)/tools/flask/policy/Makefile.common \
	        -C $(XEN_ROOT)/tools/flask/policy \
	        FLASK_BUILD_DIR=$(FLASK_BUILD_DIR) POLICY_FILENAME=$(POLICY_SRC)
	cmp -s $(POLICY_SRC) $@ || cp $(POLICY_SRC) $@

.PHONY: clean
clean::
	rm -f $(ALL_H_FILES) *.o $(DEPS_RM) policy.* $(POLICY_SRC) flask-policy.S
