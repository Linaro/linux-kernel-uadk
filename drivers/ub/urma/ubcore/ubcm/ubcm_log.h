/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * Description: ubcm log head file
 * Author: Qian Guoxin
 * Create: 2025-01-10
 * Note:
 * History: 2025-01-10: Create file
 */

#ifndef UBCM_LOG_H
#define UBCM_LOG_H

#include <linux/types.h>
#include <linux/printk.h>

enum ubcm_log_level {
	UBCM_LOG_LEVEL_EMERG = 0,
	UBCM_LOG_LEVEL_ALERT = 1,
	UBCM_LOG_LEVEL_CRIT = 2,
	UBCM_LOG_LEVEL_ERR = 3,
	UBCM_LOG_LEVEL_WARNING = 4,
	UBCM_LOG_LEVEL_NOTICE = 5,
	UBCM_LOG_LEVEL_INFO = 6,
	UBCM_LOG_LEVEL_DEBUG = 7,
	UBCM_LOG_LEVEL_MAX = 8
};

/* add log head info, "LogTag_UBCM|function|[line]| */
#define UBCM_LOG_TAG "LogTag_UBCM"
#define ubcm_log(l, format, args...) \
	pr_##l("%s|%s:[%d]|" format, UBCM_LOG_TAG, __func__, __LINE__, ##args)

#define UBCM_RATELIMIT_INTERVAL (5 * HZ)
#define UBCM_RATELIMIT_BURST 100

extern uint32_t g_ubcm_log_level;

#define ubcm_log_info(...)                                   \
	do {                                                     \
		if (g_ubcm_log_level >= UBCM_LOG_LEVEL_INFO)         \
			ubcm_log(info, __VA_ARGS__);                     \
	} while (0)

#define ubcm_log_err(...)                                   \
	do {                                                    \
		if (g_ubcm_log_level >= UBCM_LOG_LEVEL_ERR)         \
			ubcm_log(err, __VA_ARGS__);                     \
	} while (0)

#define ubcm_log_warn(...)                                      \
	do {                                                        \
		if (g_ubcm_log_level >= UBCM_LOG_LEVEL_WARNING)         \
			ubcm_log(warn, __VA_ARGS__);                        \
	} while (0)

/* No need to record debug log by printk_ratelimited */
#define ubcm_log_debug(...)                                   \
	do {                                                      \
		if (g_ubcm_log_level >= UBCM_LOG_LEVEL_DEBUG)         \
			ubcm_log(debug, __VA_ARGS__);                     \
	} while (0)

/* Rate Limited log to avoid soft lockup crash by quantities of printk */
/* Current limit is 100 log every 5 seconds */
#define ubcm_log_info_rl(...)                                       \
	do {                                                            \
		static DEFINE_RATELIMIT_STATE(_rs, UBCM_RATELIMIT_INTERVAL, \
					      UBCM_RATELIMIT_BURST);                    \
		if ((__ratelimit(&_rs)) &&                                  \
		    (g_ubcm_log_level >= UBCM_LOG_LEVEL_INFO))              \
			ubcm_log(info, __VA_ARGS__);                            \
	} while (0)

#define ubcm_log_err_rl(...)                                        \
	do {                                                            \
		static DEFINE_RATELIMIT_STATE(_rs, UBCM_RATELIMIT_INTERVAL, \
					      UBCM_RATELIMIT_BURST);                    \
		if ((__ratelimit(&_rs)) &&                                  \
		    (g_ubcm_log_level >= UBCM_LOG_LEVEL_ERR))               \
			ubcm_log(err, __VA_ARGS__);                             \
	} while (0)

#define ubcm_log_warn_rl(...)                                       \
	do {                                                            \
		static DEFINE_RATELIMIT_STATE(_rs, UBCM_RATELIMIT_INTERVAL, \
					      UBCM_RATELIMIT_BURST);                    \
		if ((__ratelimit(&_rs)) &&                                  \
		    (g_ubcm_log_level >= UBCM_LOG_LEVEL_WARNING))           \
			ubcm_log(warn, __VA_ARGS__);                            \
	} while (0)

#endif /* UBCM_LOG_H */
