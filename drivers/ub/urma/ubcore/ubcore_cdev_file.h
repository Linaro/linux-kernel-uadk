/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Description: ubcore device file ops file
 * Author: Qian Guoxin
 * Create: 2024-02-05
 * Note:
 * History: 2024-02-05: Create file
 */

#ifndef UBCORE_CDEV_FILE_H
#define UBCORE_CDEV_FILE_H

#include "ub/urma/ubcore_types.h"

struct ubcore_port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct ubcore_port_kobj *p,
			struct ubcore_port_attribute *attr, char *buf);
	ssize_t (*store)(struct ubcore_port_kobj *p,
			 struct ubcore_port_attribute *attr, const char *buf,
			 size_t count);
};

#define PORT_ATTR(_name, _mode, _show, _store)           \
	struct ubcore_port_attribute port_attr_##_name = \
		__ATTR(_name, _mode, _show, _store)

#define PORT_ATTR_RO(_name) \
	struct ubcore_port_attribute port_attr_##_name = __ATTR_RO(_name)

int ubcore_fill_logic_device_attr(struct ubcore_logic_device *ldev,
				  struct ubcore_device *dev);
void ubcore_unfill_logic_device_attr(struct ubcore_logic_device *ldev,
				     struct ubcore_device *dev);

#endif /* UBCORE_CDEV_FILE_H */
