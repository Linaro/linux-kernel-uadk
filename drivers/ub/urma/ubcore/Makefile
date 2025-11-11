# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux kernel UB device drivers.
#
ccflags-y += -I$(src)

ubcore-objs := ubcore_tp_table.o \
net/ubcore_sock.o \
ubcore_workqueue.o \
ubcore_main.o \
ubcore_hash_table.o \
ubcore_priv.o \
ubcore_cgroup.o \
net/ubcore_net.o \
ubcore_cdev_file.o \
ubcore_connect_adapter.o \
ubcore_connect_bonding.o \
ubcore_log.o \
net/ubcore_cm.o \
ubcore_topo_info.o \
net/ubcore_session.o \
ubcore_jetty.o \
ubcore_device.o \
ubcore_uvs_cmd.o \
ubcore_cmd_tlv.o

obj-$(CONFIG_UB_URMA) += ubcore.o
