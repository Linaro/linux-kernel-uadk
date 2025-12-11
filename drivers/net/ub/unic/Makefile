# SPDX-License-Identifier: GPL-2.0+
#
# Makefile for the HISILICON network device drivers.
#

ccflags-y += -I$(srctree)/$(src)
ccflags-y += -I$(srctree)/drivers/net/ub/unic/debugfs

obj-$(CONFIG_UB_UNIC) += unic.o
unic-objs = unic_main.o unic_ethtool.o unic_hw.o unic_guid.o unic_netdev.o unic_dev.o unic_qos_hw.o unic_event.o unic_crq.o
unic-objs += unic_channel.o debugfs/unic_debugfs.o unic_rx.o unic_tx.o unic_txrx.o unic_comm_addr.o unic_rack_ip.o unic_stats.o
unic-$(CONFIG_UB_UNIC_DCB) += unic_dcbnl.o
