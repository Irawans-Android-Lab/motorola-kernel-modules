DLKM_DIR := motorola/kernel/modules
LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := uxmem.ko
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
LOCAL_ADDITIONAL_DEPENDENCIES := $(KERNEL_MODULES_OUT)/moto_sched.ko
LOCAL_REQUIRED_MODULES := moto_sched.ko
KBUILD_OPTIONS_GKI += GKI_OBJ_MODULE_DIR=gki

include $(DLKM_DIR)/AndroidKernelModule.mk

