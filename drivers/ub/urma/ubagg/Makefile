
# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux kernel UB device drivers.
#
ccflags-y += -I$(src)

ubagg-objs := ubagg_bitmap.o \
ubagg_hash_table.o \
ubagg_jetty.o \
ubagg_log.o \
ubagg_main.o

obj-$(CONFIG_UB_URMA) += ubagg.o

