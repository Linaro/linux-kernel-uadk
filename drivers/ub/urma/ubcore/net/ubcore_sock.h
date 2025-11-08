/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcore sock header
 * Author: Wang Hang
 * Create: 2025-02-18
 * Note:
 * History: 2025-02-18: create file
 */

#ifndef NET_UBCORE_SOCK_H
#define NET_UBCORE_SOCK_H

#include "ubcore_net.h"
#include <ub/urma/ubcore_types.h>

int ubcore_sock_send(struct ubcore_device *dev, struct ubcore_net_msg *msg,
		     void *conn);
int ubcore_sock_send_to(struct ubcore_device *dev, struct ubcore_net_msg *msg,
			union ubcore_eid addr);

int ubcore_sock_init(void);
void ubcore_sock_uninit(void);

#endif
