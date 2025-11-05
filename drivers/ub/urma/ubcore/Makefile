
# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux kernel UB device drivers.
#
ccflags-y += -I$(src)

ubcore-objs := ubcore_main.o \
ubcore_workqueue.o \
ubcore_topo_info.o \
ubcore_hash_table.o \
ubcore_log.o

obj-$(CONFIG_UB_URMA) += ubcore.o

