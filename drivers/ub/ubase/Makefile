# SPDX-License-Identifier: GPL-2.0+
#
## Makefile for the Hisilicon ubase drivers.
#
##

ccflags-y += -I$(srctree)/$(src)
ccflags-y += -I$(srctree)/drivers/ub/ubase/debugfs

MODULE_NAME := ubase

UBASE_OBJS := ubase_main.o ubase_dev.o ubase_cmd.o \
	debugfs/ubase_debugfs.o ubase_eq.o ubase_mailbox.o ubase_ubus.o \
	debugfs/ubase_ctx_debugfs.o

$(MODULE_NAME)-objs := $(UBASE_OBJS)
obj-$(CONFIG_UB_UBASE) := ubase.o
