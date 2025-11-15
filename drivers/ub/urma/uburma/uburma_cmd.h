/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 *
 * Description: uburma cmd header file
 * Author: Qian Guoxin
 * Create: 2023-2-28
 * Note:
 * History: 2023-2-28: Create file
 */

#ifndef UBURMA_CMD_H
#define UBURMA_CMD_H

#include <uapi/linux/in.h>
#include <uapi/linux/in6.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/socket.h>

#include "ub/urma/ubcore_types.h"
#include "uburma_types.h"

struct uburma_cmd_hdr {
	uint32_t command;
	uint32_t args_len;
	uint64_t args_addr;
};

#define UBURMA_CMD_MAX_ARGS_SIZE 25600
#define UBURMA_CMD_MAX_PORT_CNT 8
#define UBURMA_CMD_TP_ATTR_BYTES 128

/* only for uburma device ioctl */
#define UBURMA_CMD_MAGIC 'U'
#define UBURMA_CMD _IOWR(UBURMA_CMD_MAGIC, 1, struct uburma_cmd_hdr)


/* copy from user_space addr to kernel args */
static inline int uburma_copy_from_user(void *args, const void *args_addr,
					unsigned long args_size)
{
	int ret;

	ret = (int)copy_from_user(args, args_addr, args_size);
	if (ret != 0) {
		uburma_log_err("copy from user failed, ret:%d.\n", ret);
		return -EFAULT;
	}
	return 0;
}

/* copy kernel args to user_space addr */
static inline int uburma_copy_to_user(void *args_addr, const void *args,
				      unsigned long args_size)
{
	int ret;

	ret = (int)copy_to_user(args_addr, args, args_size);
	if (ret != 0) {
		uburma_log_err("copy to user failed ret:%d.\n", ret);
		return -EFAULT;
	}
	return 0;
}

int uburma_unimport_jetty(struct uburma_file *file, bool async,
			  int tjetty_handle);
int uburma_unbind_jetty(struct uburma_file *file, bool async, int jetty_handle,
			int tjetty_handle);

void uburma_cmd_inc(struct uburma_device *ubu_dev);
void uburma_cmd_dec(struct uburma_device *ubu_dev);
void uburma_cmd_flush(struct uburma_device *ubu_dev);

#endif /* UBURMA_CMD_H */
