/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

/* This must be outside ifdef UBASE_TRACE_H_ */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ubase

#if !defined(__UBASE_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __UBASE_TRACE_H__

#include <linux/tracepoint.h>

#define TRACE_TWO_BYTES_BITS		16
#define TRACE_THREE_BYTES_BITS		24
#define TRACE_CMDQ_DESC_SIZE	(sizeof(struct ubase_cmdq_desc) / sizeof(u32))

#define TRACE_DEV_NAME_MAX_LEN		16

DECLARE_EVENT_CLASS(ubase_cmdq_template,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc),

	TP_STRUCT__entry(
		__field(int, idx)
		__field(u32, pi)
		__field(u32, ci)
		__array(u32, data, TRACE_CMDQ_DESC_SIZE)
		__dynamic_array(char, devname, TRACE_DEV_NAME_MAX_LEN)
	),

	TP_fast_assign(
		__entry->idx = idx;
		__entry->pi = pi;
		__entry->ci = ci;
		memcpy(&__entry->data, &desc[idx],
		       sizeof(u32) * TRACE_CMDQ_DESC_SIZE);
		if (dev) {
			snprintf(__get_str(devname), TRACE_DEV_NAME_MAX_LEN,
				 "%s %s", dev_driver_string(dev), dev_name(dev));
		}
		ubase_mask_key_words((struct ubase_cmdq_desc *)&__entry->data,
				     desc[0].opcode, idx);
	),

	TP_printk(
		"%s %d-%u-%u data: %s", __get_str(devname),
		__entry->idx, __entry->pi, __entry->ci,
		__print_array(__entry->data, TRACE_CMDQ_DESC_SIZE, sizeof(u32))
	)
);

DEFINE_EVENT(ubase_cmdq_template, ubase_csq_tx,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc));

DEFINE_EVENT(ubase_cmdq_template, ubase_csq_rx,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc));

TRACE_EVENT(ubase_crq,
	TP_PROTO(const struct device *dev, int idx, u32 pi, u32 ci,
		 struct ubase_cmdq_desc *desc),
	TP_ARGS(dev, idx, pi, ci, desc),

	TP_STRUCT__entry(
		__field(int, idx)
		__field(u32, pi)
		__field(u32, ci)
		__array(u32, data, TRACE_CMDQ_DESC_SIZE)
		__dynamic_array(char, devname, TRACE_DEV_NAME_MAX_LEN)
	),

	TP_fast_assign(
		__entry->idx = idx;
		__entry->pi = pi;
		__entry->ci = ci;
		memcpy(&__entry->data, desc,
		       sizeof(u32) * TRACE_CMDQ_DESC_SIZE);
		if (dev) {
			snprintf(__get_str(devname), TRACE_DEV_NAME_MAX_LEN,
				 "%s %s", dev_driver_string(dev), dev_name(dev));
		}
	),

	TP_printk(
		"%s %d-%u-%u data: %s", __get_str(devname),
		__entry->idx, __entry->pi, __entry->ci,
		__print_array(__entry->data, TRACE_CMDQ_DESC_SIZE, sizeof(u32))
	)
);

TRACE_EVENT(ubase_aeqe,
	TP_PROTO(struct device *dev, struct ubase_aeqe *aeqe,
		 struct ubase_eq *eq),
	TP_ARGS(dev, aeqe, eq),

	TP_STRUCT__entry(
		__field(u32, event_type)
		__field(u32, sub_type)
		__field(u32, owner)
		__field(u32, ci)
		__field(u32, queue_event_num)
		__field(u64, cmd_out_param)
		__field(u16, cmd_seq_num)
		__field(u8, cmd_status)
		__dynamic_array(char, devname, TRACE_DEV_NAME_MAX_LEN)
	),

	TP_fast_assign(
		__entry->event_type = aeqe->event_type;
		__entry->sub_type = aeqe->sub_type;
		__entry->owner = aeqe->owner;
		__entry->ci = eq->cons_index;
		__entry->queue_event_num = aeqe->event.queue_event.num;
		__entry->cmd_out_param = aeqe->event.cmd.out_param;
		__entry->cmd_seq_num = aeqe->event.cmd.seq_num;
		__entry->cmd_status = aeqe->event.cmd.status;
		if (dev) {
			snprintf(__get_str(devname), TRACE_DEV_NAME_MAX_LEN,
				 "%s %s", dev_driver_string(dev), dev_name(dev));
		}
	),

	TP_printk(
		"%s %u-%u-%u-%u-%u-%llu-%u-%u", __get_str(devname),
		__entry->event_type, __entry->sub_type, __entry->owner,
		__entry->ci, __entry->queue_event_num, __entry->cmd_out_param,
		__entry->cmd_seq_num, __entry->cmd_status
	)
);

TRACE_EVENT(ubase_ceqe,
	TP_PROTO(struct device *dev, u32 jfcn, struct ubase_eq *eq),
	TP_ARGS(dev, jfcn, eq),

	TP_STRUCT__entry(
		__field(u32, jfcn)
		__field(u32, ci)
		__dynamic_array(char, devname, TRACE_DEV_NAME_MAX_LEN)
	),

	TP_fast_assign(
		__entry->jfcn = jfcn;
		__entry->ci = eq->cons_index;
		if (dev) {
			snprintf(__get_str(devname), TRACE_DEV_NAME_MAX_LEN,
				 "%s %s", dev_driver_string(dev), dev_name(dev));
		}
	),

	TP_printk(
		"%s %u-%u", __get_str(devname), __entry->jfcn, __entry->ci
	)
);

#endif /* __UBASE_TRACE_H__ */

/* This must be outside ifdef __UBASE_TRACE_H__ */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ubase_trace
#include <trace/define_trace.h>
