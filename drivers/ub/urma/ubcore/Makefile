# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux kernel UB device drivers.
#
ccflags-y += -I$(src)

ubcore-objs := ubcore_hash_table.o \
ubcore_main.o \
net/ubcore_sock.o \
ubcore_log.o \
ubcore_tp_table.o \
ubcore_priv.o \
net/ubcore_cm.o \
net/ubcore_net.o \
ubcore_workqueue.o \
net/ubcore_session.o \
ubcore_topo_info.o

obj-$(CONFIG_UB_URMA) += ubcore.o
