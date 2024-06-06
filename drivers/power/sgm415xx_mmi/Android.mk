DLKM_DIR := motorola/kernel/modules
LOCAL_PATH := $(call my-dir)

ifeq ($(CONFIG_MMI_SGM41543D_CHARGER),true)
	KERNEL_CFLAGS += CONFIG_MMI_SGM41543D_CHARGER=y
endif

include $(CLEAR_VARS)
LOCAL_MODULE := sgm415xx_mmi.ko
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)

include $(DLKM_DIR)/AndroidKernelModule.mk
