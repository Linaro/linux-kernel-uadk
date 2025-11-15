// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Description: uburma cmd tlv parse implement
 * Author: Wang Hang
 * Create: 2024-08-27
 * Note:
 * History: 2024-08-27: Create file
 */

#include "uburma_log.h"

#include "uburma_cmd_tlv.h"

#define UBURMA_CMD_TLV_MAX_LEN \
	(sizeof(struct uburma_cmd_attr) * UBURMA_CMD_OUT_TYPE_INIT)

struct uburma_tlv_handler {
	void (*fill_spec_in)(void *arg, struct uburma_cmd_spec *s);
	size_t spec_in_len;
	void (*fill_spec_out)(void *arg, struct uburma_cmd_spec *s);
	size_t spec_out_len;
};

static inline void fill_spec(struct uburma_cmd_spec *spec,
			     uint16_t type, uint16_t field_size,
			     uint16_t el_num, uint16_t el_size,
			     uintptr_t data)
{
	*spec = (struct uburma_cmd_spec) {
		.type = type,
		.flag.bs = { .mandatory = 1 },
		.field_size = field_size,
		.attr_data.bs = { .el_num = el_num,
				  .el_size = el_size },
		.data = data,
	};
}

/**
 * Fill spec with a field, which is a value or an array taken as a whole.
 * @param v Full path of field, e.g. `arg->out.attr.dev_cap.feature`
 */
#define SPEC(spec, type, v) \
	fill_spec(spec, type, sizeof(v), 1, 0, (uintptr_t)(&(v)))

/**
 * Fill spec with a field, which belongs to an array of structs.
 * @param v1 Full path of struct array, e.g. `arg->out.attr.port_attr`
 * @param v2 Path relative to struct in array, e.g. `active_speed`
 */
#define SPEC_ARRAY(spec, type, v1, v2)                          \
	fill_spec(spec, type, sizeof((v1)->v2), ARRAY_SIZE(v1), \
		  sizeof((v1)[0]), (uintptr_t)(&((v1)->v2)))

static void
uburma_create_ctx_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_ctx *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_CTX_IN_EID, arg->in.eid);
	SPEC(s++, CREATE_CTX_IN_EID_INDEX, arg->in.eid_index);
	SPEC(s++, CREATE_CTX_IN_UDATA, arg->udata);
}

static void
uburma_create_ctx_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_ctx *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_CTX_OUT_ASYNC_FD, arg->out.async_fd);
	SPEC(s++, CREATE_CTX_OUT_UDATA, arg->udata);
}

static void
uburma_register_seg_fill_spec_in(void *arg_addr,
				 struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_register_seg *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, REGISTER_SEG_IN_VA, arg->in.va);
	SPEC(s++, REGISTER_SEG_IN_LEN, arg->in.len);
	SPEC(s++, REGISTER_SEG_IN_TOKEN_ID, arg->in.token_id);
	SPEC(s++, REGISTER_SEG_IN_TOKEN_ID_HANDLE,
	     arg->in.token_id_handle);
	SPEC(s++, REGISTER_SEG_IN_TOKEN, arg->in.token);
	SPEC(s++, REGISTER_SEG_IN_FLAG, arg->in.flag);
	SPEC(s++, REGISTER_SEG_IN_UDATA, arg->udata);
}

static void
uburma_register_seg_fill_spec_out(void *arg_addr,
				  struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_register_seg *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, REGISTER_SEG_OUT_TOKEN_ID, arg->out.token_id);
	SPEC(s++, REGISTER_SEG_OUT_HANDLE, arg->out.handle);
	SPEC(s++, REGISTER_SEG_OUT_UDATA, arg->udata);
}

static void
uburma_unregister_seg_fill_spec_in(void *arg_addr,
				   struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_unregister_seg *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, UNREGISTER_SEG_IN_HANDLE, arg->in.handle);
}

static void
uburma_create_jfs_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JFS_IN_DEPTH, arg->in.depth);
	SPEC(s++, CREATE_JFS_IN_FLAG, arg->in.flag);
	SPEC(s++, CREATE_JFS_IN_TRANS_MODE, arg->in.trans_mode);
	SPEC(s++, CREATE_JFS_IN_PRIORITY, arg->in.priority);
	SPEC(s++, CREATE_JFS_IN_MAX_SGE, arg->in.max_sge);
	SPEC(s++, CREATE_JFS_IN_MAX_RSGE, arg->in.max_rsge);
	SPEC(s++, CREATE_JFS_IN_MAX_INLINE_DATA,
	     arg->in.max_inline_data);
	SPEC(s++, CREATE_JFS_IN_RETRY_CNT, arg->in.retry_cnt);
	SPEC(s++, CREATE_JFS_IN_RNR_RETRY, arg->in.rnr_retry);
	SPEC(s++, CREATE_JFS_IN_ERR_TIMEOUT, arg->in.err_timeout);
	SPEC(s++, CREATE_JFS_IN_JFC_ID, arg->in.jfc_id);
	SPEC(s++, CREATE_JFS_IN_JFC_HANDLE, arg->in.jfc_handle);
	SPEC(s++, CREATE_JFS_IN_URMA_JFS, arg->in.urma_jfs);
	SPEC(s++, CREATE_JFS_IN_UDATA, arg->udata);
}

static void
uburma_create_jfs_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JFS_OUT_ID, arg->out.id);
	SPEC(s++, CREATE_JFS_OUT_DEPTH, arg->out.depth);
	SPEC(s++, CREATE_JFS_OUT_MAX_SGE, arg->out.max_sge);
	SPEC(s++, CREATE_JFS_OUT_MAX_RSGE, arg->out.max_rsge);
	SPEC(s++, CREATE_JFS_OUT_MAX_INLINE_DATA,
	     arg->out.max_inline_data);
	SPEC(s++, CREATE_JFS_OUT_HANDLE, arg->out.handle);
	SPEC(s++, CREATE_JFS_OUT_UDATA, arg->udata);
}

static void
uburma_modify_jfs_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JFS_IN_HANDLE, arg->in.handle);
	SPEC(s++, MODIFY_JFS_IN_MASK, arg->in.mask);
	SPEC(s++, MODIFY_JFS_IN_STATE, arg->in.state);
	SPEC(s++, MODIFY_JFS_IN_UDATA, arg->udata);
}

static void
uburma_modify_jfs_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JFS_OUT_UDATA, arg->udata);
}

static void
uburma_query_jfs_fill_spec_in(void *arg_addr,
			      struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_query_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, QUERY_JFS_IN_HANDLE, arg->in.handle);
}

static void
uburma_query_jfs_fill_spec_out(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_query_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, QUERY_JFS_OUT_DEPTH, arg->out.depth);
	SPEC(s++, QUERY_JFS_OUT_FLAG, arg->out.flag);
	SPEC(s++, QUERY_JFS_OUT_TRANS_MODE, arg->out.trans_mode);
	SPEC(s++, QUERY_JFS_OUT_PRIORITY, arg->out.priority);
	SPEC(s++, QUERY_JFS_OUT_MAX_SGE, arg->out.max_sge);
	SPEC(s++, QUERY_JFS_OUT_MAX_RSGE, arg->out.max_rsge);
	SPEC(s++, QUERY_JFS_OUT_MAX_INLINE_DATA,
	     arg->out.max_inline_data);
	SPEC(s++, QUERY_JFS_OUT_RETRY_CNT, arg->out.retry_cnt);
	SPEC(s++, QUERY_JFS_OUT_RNR_RETRY, arg->out.rnr_retry);
	SPEC(s++, QUERY_JFS_OUT_ERR_TIMEOUT, arg->out.err_timeout);
	SPEC(s++, QUERY_JFS_OUT_STATE, arg->out.state);
}

static void
uburma_delete_jfs_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFS_IN_HANDLE, arg->in.handle);
}

static void
uburma_delete_jfs_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfs *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFS_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
}

static void
uburma_delete_jfs_batch_fill_spec_in(void *arg_addr,
				     struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfs_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFS_BATCH_IN_JFS_COUNT, arg->in.jfs_num);
	SPEC(s++, DELETE_JFS_BATCH_IN_JFS_PTR, arg->in.jfs_ptr);
}

static void
uburma_delete_jfs_batch_fill_spec_out(void *arg_addr,
				      struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfs_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFS_BATCH_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
	SPEC(s++, DELETE_JFS_BATCH_OUT_BAD_JFS_INDEX,
	     arg->out.bad_jfs_index);
}

static void
uburma_create_jfr_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JFR_IN_DEPTH, arg->in.depth);
	SPEC(s++, CREATE_JFR_IN_FLAG, arg->in.flag);
	SPEC(s++, CREATE_JFR_IN_TRANS_MODE, arg->in.trans_mode);
	SPEC(s++, CREATE_JFR_IN_MAX_SGE, arg->in.max_sge);
	SPEC(s++, CREATE_JFR_IN_MIN_RNR_TIMER, arg->in.min_rnr_timer);
	SPEC(s++, CREATE_JFR_IN_JFC_ID, arg->in.jfc_id);
	SPEC(s++, CREATE_JFR_IN_JFC_HANDLE, arg->in.jfc_handle);
	SPEC(s++, CREATE_JFR_IN_TOKEN, arg->in.token);
	SPEC(s++, CREATE_JFR_IN_ID, arg->in.id);
	SPEC(s++, CREATE_JFR_IN_URMA_JFR, arg->in.urma_jfr);
	SPEC(s++, CREATE_JFR_IN_UDATA, arg->udata);
}

static void
uburma_create_jfr_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JFR_OUT_ID, arg->out.id);
	SPEC(s++, CREATE_JFR_OUT_DEPTH, arg->out.depth);
	SPEC(s++, CREATE_JFR_OUT_MAX_SGE, arg->out.max_sge);
	SPEC(s++, CREATE_JFR_OUT_HANDLE, arg->out.handle);
	SPEC(s++, CREATE_JFR_OUT_UDATA, arg->udata);
}

static void
uburma_modify_jfr_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JFR_IN_HANDLE, arg->in.handle);
	SPEC(s++, MODIFY_JFR_IN_MASK, arg->in.mask);
	SPEC(s++, MODIFY_JFR_IN_RX_THRESHOLD, arg->in.rx_threshold);
	SPEC(s++, MODIFY_JFR_IN_STATE, arg->in.state);
	SPEC(s++, MODIFY_JFR_IN_UDATA, arg->udata);
}

static void
uburma_modify_jfr_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JFR_OUT_UDATA, arg->udata);
}

static void
uburma_cmd_query_jfr_fill_spec_in(void *arg_addr,
				  struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_query_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, QUERY_JFR_IN_HANDLE, arg->in.handle);
}

static void
uburma_cmd_query_jfr_fill_spec_out(void *arg_addr,
				   struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_query_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, QUERY_JFR_OUT_DEPTH, arg->out.depth);
	SPEC(s++, QUERY_JFR_OUT_FLAG, arg->out.flag);
	SPEC(s++, QUERY_JFR_OUT_TRANS_MODE, arg->out.trans_mode);
	SPEC(s++, QUERY_JFR_OUT_MAX_SGE, arg->out.max_sge);
	SPEC(s++, QUERY_JFR_OUT_MIN_RNR_TIMER,
	     arg->out.min_rnr_timer);
	SPEC(s++, QUERY_JFR_OUT_TOKEN, arg->out.token);
	SPEC(s++, QUERY_JFR_OUT_ID, arg->out.id);
	SPEC(s++, QUERY_JFR_OUT_RX_THRESHOLD, arg->out.rx_threshold);
	SPEC(s++, QUERY_JFR_OUT_STATE, arg->out.state);
}

static void
uburma_delete_jfr_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFR_IN_HANDLE, arg->in.handle);
}

static void
uburma_delete_jfr_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfr *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFR_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
}

static void
uburma_delete_jfr_batch_fill_spec_in(void *arg_addr,
				     struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfr_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFR_BATCH_IN_JFR_COUNT, arg->in.jfr_num);
	SPEC(s++, DELETE_JFR_BATCH_IN_JFR_PTR, arg->in.jfr_ptr);
}

static void
uburma_delete_jfr_batch_fill_spec_out(void *arg_addr,
				      struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfr_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFR_BATCH_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
	SPEC(s++, DELETE_JFR_BATCH_OUT_BAD_JFR_INDEX,
	     arg->out.bad_jfr_index);
}

static void
uburma_create_jfc_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jfc *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JFC_IN_DEPTH, arg->in.depth);
	SPEC(s++, CREATE_JFC_IN_FLAG, arg->in.flag);
	SPEC(s++, CREATE_JFC_IN_JFCE_FD, arg->in.jfce_fd);
	SPEC(s++, CREATE_JFC_IN_URMA_JFC, arg->in.urma_jfc);
	SPEC(s++, CREATE_JFC_IN_CEQN, arg->in.ceqn);
	SPEC(s++, CREATE_JFC_IN_UDATA, arg->udata);
}

static void
uburma_create_jfc_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jfc *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JFC_OUT_ID, arg->out.id);
	SPEC(s++, CREATE_JFC_OUT_DEPTH, arg->out.depth);
	SPEC(s++, CREATE_JFC_OUT_HANDLE, arg->out.handle);
	SPEC(s++, CREATE_JFC_OUT_UDATA, arg->udata);
}

static void
uburma_modify_jfc_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jfc *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JFC_IN_HANDLE, arg->in.handle);
	SPEC(s++, MODIFY_JFC_IN_MASK, arg->in.mask);
	SPEC(s++, MODIFY_JFC_IN_MODERATE_COUNT,
	     arg->in.moderate_count);
	SPEC(s++, MODIFY_JFC_IN_MODERATE_PERIOD,
	     arg->in.moderate_period);
	SPEC(s++, MODIFY_JFC_IN_UDATA, arg->udata);
}

static void
uburma_modify_jfc_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jfc *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JFC_OUT_UDATA, arg->udata);
}

static void
uburma_delete_jfc_fill_spec_in(void *arg_addr,
			       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfc *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFC_IN_HANDLE, arg->in.handle);
}

static void
uburma_delete_jfc_fill_spec_out(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfc *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFC_OUT_COMP_EVENTS_REPORTED,
	     arg->out.comp_events_reported);
	SPEC(s++, DELETE_JFC_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
}

static void
uburma_delete_jfc_batch_fill_spec_in(void *arg_addr,
				     struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfc_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFC_BATCH_IN_JFC_COUNT, arg->in.jfc_num);
	SPEC(s++, DELETE_JFC_BATCH_IN_JFC_PTR, arg->in.jfc_ptr);
}

static void
uburma_delete_jfc_batch_fill_spec_out(void *arg_addr,
				      struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jfc_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JFC_BATCH_OUT_COMP_EVENTS_REPORTED,
	     arg->out.comp_events_reported);
	SPEC(s++, DELETE_JFC_BATCH_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
	SPEC(s++, DELETE_JFC_BATCH_OUT_BAD_JFC_INDEX,
	     arg->out.bad_jfc_index);
}

static void
uburma_create_jfce_fill_spec_out(void *arg_addr,
				 struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jfce *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JFCE_OUT_FD, arg->out.fd);
}

static void
uburma_create_jetty_fill_spec_in(void *arg_addr,
				 struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JETTY_IN_ID, arg->in.id);
	SPEC(s++, CREATE_JETTY_IN_JETTY_FLAG, arg->in.jetty_flag);
	SPEC(s++, CREATE_JETTY_IN_JFS_DEPTH, arg->in.jfs_depth);
	SPEC(s++, CREATE_JETTY_IN_JFS_FLAG, arg->in.jfs_flag);
	SPEC(s++, CREATE_JETTY_IN_TRANS_MODE, arg->in.trans_mode);
	SPEC(s++, CREATE_JETTY_IN_PRIORITY, arg->in.priority);
	SPEC(s++, CREATE_JETTY_IN_MAX_SEND_SGE, arg->in.max_send_sge);
	SPEC(s++, CREATE_JETTY_IN_MAX_SEND_RSGE,
	     arg->in.max_send_rsge);
	SPEC(s++, CREATE_JETTY_IN_MAX_INLINE_DATA,
	     arg->in.max_inline_data);
	SPEC(s++, CREATE_JETTY_IN_RNR_RETRY, arg->in.rnr_retry);
	SPEC(s++, CREATE_JETTY_IN_ERR_TIMEOUT, arg->in.err_timeout);
	SPEC(s++, CREATE_JETTY_IN_SEND_JFC_ID, arg->in.send_jfc_id);
	SPEC(s++, CREATE_JETTY_IN_SEND_JFC_HANDLE,
	     arg->in.send_jfc_handle);
	SPEC(s++, CREATE_JETTY_IN_JFR_DEPTH, arg->in.jfr_depth);
	SPEC(s++, CREATE_JETTY_IN_JFR_FLAG, arg->in.jfr_flag);
	SPEC(s++, CREATE_JETTY_IN_MAX_RECV_SGE, arg->in.max_recv_sge);
	SPEC(s++, CREATE_JETTY_IN_MIN_RNR_TIMER,
	     arg->in.min_rnr_timer);
	SPEC(s++, CREATE_JETTY_IN_RECV_JFC_ID, arg->in.recv_jfc_id);
	SPEC(s++, CREATE_JETTY_IN_RECV_JFC_HANDLE,
	     arg->in.recv_jfc_handle);
	SPEC(s++, CREATE_JETTY_IN_TOKEN, arg->in.token);
	SPEC(s++, CREATE_JETTY_IN_JFR_ID, arg->in.jfr_id);
	SPEC(s++, CREATE_JETTY_IN_JFR_HANDLE, arg->in.jfr_handle);
	SPEC(s++, CREATE_JETTY_IN_JETTY_GRP_HANDLE,
	     arg->in.jetty_grp_handle);
	SPEC(s++, CREATE_JETTY_IN_IS_JETTY_GRP, arg->in.is_jetty_grp);
	SPEC(s++, CREATE_JETTY_IN_URMA_JETTY, arg->in.urma_jetty);
	SPEC(s++, CREATE_JETTY_IN_UDATA, arg->udata);
}

static void
uburma_create_jetty_fill_spec_out(void *arg_addr,
				  struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JETTY_OUT_ID, arg->out.id);
	SPEC(s++, CREATE_JETTY_OUT_HANDLE, arg->out.handle);
	SPEC(s++, CREATE_JETTY_OUT_JFS_DEPTH, arg->out.jfs_depth);
	SPEC(s++, CREATE_JETTY_OUT_JFR_DEPTH, arg->out.jfr_depth);
	SPEC(s++, CREATE_JETTY_OUT_MAX_SEND_SGE,
	     arg->out.max_send_sge);
	SPEC(s++, CREATE_JETTY_OUT_MAX_SEND_RSGE,
	     arg->out.max_send_rsge);
	SPEC(s++, CREATE_JETTY_OUT_MAX_RECV_SGE,
	     arg->out.max_recv_sge);
	SPEC(s++, CREATE_JETTY_OUT_MAX_INLINE_DATA,
	     arg->out.max_inline_data);
	SPEC(s++, CREATE_JETTY_OUT_UDATA, arg->udata);
}

static void
uburma_modify_jetty_fill_spec_in(void *arg_addr,
				 struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JETTY_IN_HANDLE, arg->in.handle);
	SPEC(s++, MODIFY_JETTY_IN_MASK, arg->in.mask);
	SPEC(s++, MODIFY_JETTY_IN_RX_THRESHOLD, arg->in.rx_threshold);
	SPEC(s++, MODIFY_JETTY_IN_STATE, arg->in.state);
	SPEC(s++, MODIFY_JETTY_IN_UDATA, arg->udata);
}

static void
uburma_modify_jetty_fill_spec_out(void *arg_addr,
				  struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_modify_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, MODIFY_JETTY_OUT_UDATA, arg->udata);
}

static void
uburma_query_jetty_fill_spec_in(void *arg_addr,
				struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_query_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, QUERY_JETTY_IN_HANDLE, arg->in.handle);
}

static void
uburma_query_jetty_fill_spec_out(void *arg_addr,
				 struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_query_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, QUERY_JETTY_OUT_ID, arg->out.id);
	SPEC(s++, QUERY_JETTY_OUT_JETTY_FLAG, arg->out.jetty_flag);
	SPEC(s++, QUERY_JETTY_OUT_JFS_DEPTH, arg->out.jfs_depth);
	SPEC(s++, QUERY_JETTY_OUT_JFR_DEPTH, arg->out.jfr_depth);
	SPEC(s++, QUERY_JETTY_OUT_JFS_FLAG, arg->out.jfs_flag);
	SPEC(s++, QUERY_JETTY_OUT_JFR_FLAG, arg->out.jfr_flag);
	SPEC(s++, QUERY_JETTY_OUT_TRANS_MODE, arg->out.trans_mode);
	SPEC(s++, QUERY_JETTY_OUT_MAX_SEND_SGE,
	     arg->out.max_send_sge);
	SPEC(s++, QUERY_JETTY_OUT_MAX_SEND_RSGE,
	     arg->out.max_send_rsge);
	SPEC(s++, QUERY_JETTY_OUT_MAX_RECV_SGE,
	     arg->out.max_recv_sge);
	SPEC(s++, QUERY_JETTY_OUT_MAX_INLINE_DATA,
	     arg->out.max_inline_data);
	SPEC(s++, QUERY_JETTY_OUT_PRIORITY, arg->out.priority);
	SPEC(s++, QUERY_JETTY_OUT_RETRY_CNT, arg->out.retry_cnt);
	SPEC(s++, QUERY_JETTY_OUT_RNR_RETRY, arg->out.rnr_retry);
	SPEC(s++, QUERY_JETTY_OUT_ERR_TIMEOUT, arg->out.err_timeout);
	SPEC(s++, QUERY_JETTY_OUT_MIN_RNR_TIMER,
	     arg->out.min_rnr_timer);
	SPEC(s++, QUERY_JETTY_OUT_JFR_ID, arg->out.jfr_id);
	SPEC(s++, QUERY_JETTY_OUT_TOKEN, arg->out.token);
	SPEC(s++, QUERY_JETTY_OUT_RX_THRESHOLD,
	     arg->out.rx_threshold);
	SPEC(s++, QUERY_JETTY_OUT_STATE, arg->out.state);
}

static void
uburma_delete_jetty_fill_spec_in(void *arg_addr,
				 struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JETTY_IN_HANDLE, arg->in.handle);
}

static void
uburma_delete_jetty_fill_spec_out(void *arg_addr,
				  struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jetty *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JETTY_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
}

static void
uburma_delete_jetty_batch_fill_spec_in(void *arg_addr,
				       struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jetty_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JETTY_BATCH_IN_JETTY_COUNT,
	     arg->in.jetty_num);
	SPEC(s++, DELETE_JETTY_BATCH_IN_JETTY_PTR, arg->in.jetty_ptr);
}

static void
uburma_delete_jetty_batch_fill_spec_out(void *arg_addr,
					struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jetty_batch *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JETTY_BATCH_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
	SPEC(s++, DELETE_JETTY_BATCH_OUT_BAD_JETTY_INDEX,
	     arg->out.bad_jetty_index);
}

static void
uburma_create_jetty_grp_fill_spec_in(void *arg_addr,
				     struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jetty_grp *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JETTY_GRP_IN_NAME, arg->in.name);
	SPEC(s++, CREATE_JETTY_GRP_IN_TOKEN, arg->in.token);
	SPEC(s++, CREATE_JETTY_GRP_IN_ID, arg->in.id);
	SPEC(s++, CREATE_JETTY_GRP_IN_POLICY, arg->in.policy);
	SPEC(s++, CREATE_JETTY_GRP_IN_FLAG, arg->in.flag);
	SPEC(s++, CREATE_JETTY_GRP_IN_URMA_JETTY_GRP,
	     arg->in.urma_jetty_grp);
	SPEC(s++, CREATE_JETTY_GRP_IN_UDATA, arg->udata);
}

static void
uburma_create_jetty_grp_fill_spec_out(void *arg_addr,
				      struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_create_jetty_grp *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, CREATE_JETTY_GRP_OUT_ID, arg->out.id);
	SPEC(s++, CREATE_JETTY_GRP_OUT_HANDLE, arg->out.handle);
	SPEC(s++, CREATE_JETTY_GRP_OUT_UDATA, arg->udata);
}

static void
uburma_delete_jetty_grp_fill_spec_in(void *arg_addr,
				     struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jetty_grp *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JETTY_GRP_IN_HANDLE, arg->in.handle);
}

static void
uburma_delete_jetty_grp_fill_spec_out(void *arg_addr,
				      struct uburma_cmd_spec *spec)
{
	struct uburma_cmd_delete_jetty_grp *arg = arg_addr;
	struct uburma_cmd_spec *s = spec;

	SPEC(s++, DELETE_JETTY_GRP_OUT_ASYNC_EVENTS_REPORTED,
	     arg->out.async_events_reported);
}

static struct uburma_tlv_handler g_tlv_handler[] = {
	[0] = {0},
	[UBURMA_CMD_CREATE_CTX] = {
		uburma_create_ctx_fill_spec_in, CREATE_CTX_IN_NUM,
		uburma_create_ctx_fill_spec_out, CREATE_CTX_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_REGISTER_SEG] = {
		uburma_register_seg_fill_spec_in, REGISTER_SEG_IN_NUM,
		uburma_register_seg_fill_spec_out, REGISTER_SEG_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_UNREGISTER_SEG] = {
		uburma_unregister_seg_fill_spec_in, UNREGISTER_SEG_IN_NUM,
		NULL, 0,
	},
	[UBURMA_CMD_CREATE_JFR] = {
		uburma_create_jfr_fill_spec_in, CREATE_JFR_IN_NUM,
		uburma_create_jfr_fill_spec_out, CREATE_JFR_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_MODIFY_JFR] = {
		uburma_modify_jfr_fill_spec_in, MODIFY_JFR_IN_NUM,
		uburma_modify_jfr_fill_spec_out, MODIFY_JFR_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_QUERY_JFR] = {
		uburma_cmd_query_jfr_fill_spec_in, QUERY_JFR_IN_NUM,
		uburma_cmd_query_jfr_fill_spec_out, QUERY_JFR_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JFR] = {
		uburma_delete_jfr_fill_spec_in, DELETE_JFR_IN_NUM,
		uburma_delete_jfr_fill_spec_out, DELETE_JFR_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_CREATE_JFS] = {
		uburma_create_jfs_fill_spec_in, CREATE_JFS_IN_NUM,
		uburma_create_jfs_fill_spec_out, CREATE_JFS_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_MODIFY_JFS] = {
		uburma_modify_jfs_fill_spec_in, MODIFY_JFS_IN_NUM,
		uburma_modify_jfs_fill_spec_out, MODIFY_JFS_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_QUERY_JFS] = {
		uburma_query_jfs_fill_spec_in, QUERY_JFS_IN_NUM,
		uburma_query_jfs_fill_spec_out, QUERY_JFS_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JFS] = {
		uburma_delete_jfs_fill_spec_in, DELETE_JFS_IN_NUM,
		uburma_delete_jfs_fill_spec_out, DELETE_JFS_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_CREATE_JFC] = {
		uburma_create_jfc_fill_spec_in, CREATE_JFC_IN_NUM,
		uburma_create_jfc_fill_spec_out, CREATE_JFC_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_MODIFY_JFC] = {
		uburma_modify_jfc_fill_spec_in, MODIFY_JFC_IN_NUM,
		uburma_modify_jfc_fill_spec_out, MODIFY_JFC_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JFC] = {
		uburma_delete_jfc_fill_spec_in, DELETE_JFC_IN_NUM,
		uburma_delete_jfc_fill_spec_out, DELETE_JFC_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_CREATE_JFCE] = {
		NULL, 0,
		uburma_create_jfce_fill_spec_out, CREATE_JFCE_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_CREATE_JETTY] = {
		uburma_create_jetty_fill_spec_in, CREATE_JETTY_IN_NUM,
		uburma_create_jetty_fill_spec_out, CREATE_JETTY_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_MODIFY_JETTY] = {
		uburma_modify_jetty_fill_spec_in, MODIFY_JETTY_IN_NUM,
		uburma_modify_jetty_fill_spec_out, MODIFY_JETTY_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_QUERY_JETTY] = {
		uburma_query_jetty_fill_spec_in, QUERY_JETTY_IN_NUM,
		uburma_query_jetty_fill_spec_out, QUERY_JETTY_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JETTY] = {
		uburma_delete_jetty_fill_spec_in, DELETE_JETTY_IN_NUM,
		uburma_delete_jetty_fill_spec_out, DELETE_JETTY_OUT_NUM - UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_CREATE_JETTY_GRP] = {
		uburma_create_jetty_grp_fill_spec_in, CREATE_JETTY_GRP_IN_NUM,
		uburma_create_jetty_grp_fill_spec_out, CREATE_JETTY_GRP_OUT_NUM -
		UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DESTROY_JETTY_GRP] = {
		uburma_delete_jetty_grp_fill_spec_in, DELETE_JETTY_GRP_IN_NUM,
		uburma_delete_jetty_grp_fill_spec_out, DELETE_JETTY_GRP_OUT_NUM -
		UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JFS_BATCH] = {
		uburma_delete_jfs_batch_fill_spec_in, DELETE_JFS_BATCH_IN_NUM,
		uburma_delete_jfs_batch_fill_spec_out, DELETE_JFS_BATCH_OUT_NUM -
		UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JFR_BATCH] = {
		uburma_delete_jfr_batch_fill_spec_in, DELETE_JFR_BATCH_IN_NUM,
		uburma_delete_jfr_batch_fill_spec_out, DELETE_JFR_BATCH_OUT_NUM -
		UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JFC_BATCH] = {
		uburma_delete_jfc_batch_fill_spec_in, DELETE_JFC_BATCH_IN_NUM,
		uburma_delete_jfc_batch_fill_spec_out, DELETE_JFC_BATCH_OUT_NUM -
		UBURMA_CMD_OUT_TYPE_INIT,
	},
	[UBURMA_CMD_DELETE_JETTY_BATCH] = {
		uburma_delete_jetty_batch_fill_spec_in, DELETE_JETTY_BATCH_IN_NUM,
		uburma_delete_jetty_batch_fill_spec_out, DELETE_JETTY_BATCH_OUT_NUM -
		UBURMA_CMD_OUT_TYPE_INIT,
	},
};

static struct uburma_cmd_attr *
uburma_create_tlv_attr(struct uburma_cmd_hdr *hdr,
		       uint32_t *attr_size)
{
	struct uburma_cmd_attr *attr;
	int ret;

	if (hdr->args_len % sizeof(struct uburma_cmd_attr) != 0 ||
	    hdr->args_len >= UBURMA_CMD_TLV_MAX_LEN) {
		uburma_log_err("Invalid args_len: %u.\n",
			       hdr->args_len);
		return NULL;
	}
	attr = kzalloc(hdr->args_len, GFP_KERNEL);
	if (!attr)
		return NULL;

	ret = uburma_copy_from_user(
		attr, (void __user *)(uintptr_t)hdr->args_addr,
		hdr->args_len);
	if (ret != 0) {
		kfree(attr);
		return NULL;
	}
	*attr_size = hdr->args_len / sizeof(struct uburma_cmd_attr);
	return attr;
}

static int uburma_cmd_tlv_parse_type(struct uburma_cmd_spec *spec,
				     struct uburma_cmd_attr *attr)
{
	uintptr_t ptr_src, ptr_dst;
	uint32_t i;
	int ret;

	/* length of uburma spec and from uvs should be strictly checked */
	/* as length of uvs ioctl attr should be strictly equal to length of uburma */
	if (spec->field_size != attr->field_size ||
	    spec->attr_data.bs.el_num != attr->attr_data.bs.el_num) {
		uburma_log_err(
			"Invalid attr, spec/attr, field_size: %u/%u, el_num: %u/%u, type: %u.\n",
			spec->field_size, attr->field_size,
			spec->attr_data.bs.el_num,
			attr->attr_data.bs.el_num, spec->type);
		return -EINVAL;
	}

	for (i = 0; i < spec->attr_data.bs.el_num; i++) {
		ptr_dst =
			(spec->data) + i * spec->attr_data.bs.el_size;
		ptr_src =
			(attr->data) + i * attr->attr_data.bs.el_size;
		ret = uburma_copy_from_user((void *)ptr_dst,
					    (void __user *)ptr_src,
					    spec->field_size);
		if (ret != 0)
			return ret;
	}

	return ret;
}

static int uburma_cmd_tlv_parse(struct uburma_cmd_spec *spec,
				uint32_t spec_size,
				struct uburma_cmd_attr *attr,
				uint32_t attr_size)
{
	uint32_t spec_idx, attr_idx;
	bool match;
	int ret;

	/* spec type of this range is only in type */
	for (spec_idx = 0; spec_idx < spec_size; spec_idx++) {
		match = false;
		for (attr_idx = 0; attr_idx < attr_size; attr_idx++) {
			if (spec[spec_idx].type ==
			    attr[attr_idx].type) {
				ret = uburma_cmd_tlv_parse_type(
					&spec[spec_idx],
					&attr[attr_idx]);
				if (ret != 0)
					return ret;
				match = true;
				break;
			}
		}
		if (!match &&
		    ((spec[spec_idx].flag.bs.mandatory) != 0)) {
			uburma_log_err(
				"Failed to match mandatory in type: %u.\n",
				spec[spec_idx].type);
			return -EINVAL;
		}
	}

	return 0;
}

static int uburma_cmd_tlv_append_type(struct uburma_cmd_spec *spec,
				      struct uburma_cmd_attr *attr)
{
	uintptr_t ptr_src, ptr_dst;
	uint32_t i;
	int ret;

	/* length of uburma spec and from uvs should be strictly checked */
	/* as length of uvs ioctl attr should be strictly equal to length of uburma */
	if (spec->field_size != attr->field_size ||
	    spec->attr_data.bs.el_num > attr->attr_data.bs.el_num) {
		uburma_log_err(
			"Invalid attr, spec/attr, field_size: %u/%u, array_size: %u/%u, type: %u.\n",
			spec->field_size, attr->field_size,
			spec->attr_data.bs.el_num,
			attr->attr_data.bs.el_num, spec->type);
		return -EINVAL;
	}

	for (i = 0; i < spec->attr_data.bs.el_num; i++) {
		ptr_src =
			(spec->data) + i * spec->attr_data.bs.el_size;
		ptr_dst =
			(attr->data) + i * attr->attr_data.bs.el_size;
		ret = uburma_copy_to_user((void __user *)ptr_dst,
					  (void *)ptr_src,
					  spec->field_size);
		if (ret != 0)
			return ret;
	}

	return ret;
}

static int uburma_cmd_tlv_append(struct uburma_cmd_spec *spec,
				 uint32_t spec_size,
				 struct uburma_cmd_attr *attr,
				 uint32_t attr_size)
{
	uint32_t spec_idx, attr_idx;
	bool match;
	int ret;

	for (spec_idx = 0; spec_idx < spec_size; spec_idx++) {
		match = false;
		for (attr_idx = 0; attr_idx < attr_size; attr_idx++) {
			if (spec[spec_idx].type ==
			    attr[attr_idx].type) {
				ret = uburma_cmd_tlv_append_type(
					&spec[spec_idx],
					&attr[attr_idx]);
				if (ret != 0)
					return ret;
				match = true;
				break;
			}
		}
		if (!match && spec[spec_idx].flag.bs.mandatory) {
			uburma_log_err(
				"Failed to match mandatory out type: %u.\n",
				spec[spec_idx].type);
			return -EINVAL;
		}
	}

	return 0;
}

int uburma_tlv_parse(struct uburma_cmd_hdr *hdr, void *arg)
{
	struct uburma_cmd_spec *spec = NULL;
	struct uburma_cmd_attr *attr = NULL;
	uint32_t attr_size, spec_size;
	int ret;

	/* Command of hdr is valid, no need to check it */
	if (!g_tlv_handler[hdr->command].fill_spec_in) {
		uburma_log_err("Invalid command: %u.\n",
			       hdr->command);
		return -EINVAL;
	}

	spec_size = g_tlv_handler[hdr->command].spec_in_len;
	spec = kcalloc(spec_size, sizeof(struct uburma_cmd_spec),
		       GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	g_tlv_handler[hdr->command].fill_spec_in(arg, spec);

	attr = uburma_create_tlv_attr(hdr, &attr_size);
	if (!attr) {
		ret = -ENOMEM;
		goto free_spec;
	}

	ret = uburma_cmd_tlv_parse(spec, spec_size, attr, attr_size);

	kfree(attr);
free_spec:
	kfree(spec);
	return ret;
}

int uburma_tlv_append(struct uburma_cmd_hdr *hdr, void *arg)
{
	struct uburma_cmd_spec *spec = NULL;
	struct uburma_cmd_attr *attr = NULL;
	uint32_t attr_size, spec_size;
	int ret;

	/* Command of hdr is valid, no need to check it */
	if (!g_tlv_handler[hdr->command].fill_spec_out) {
		uburma_log_err("Invalid command: %u.\n",
			       hdr->command);
		return -EINVAL;
	}

	spec_size = g_tlv_handler[hdr->command].spec_out_len;
	spec = kcalloc(spec_size, sizeof(struct uburma_cmd_spec),
		       GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	g_tlv_handler[hdr->command].fill_spec_out(arg, spec);

	attr = uburma_create_tlv_attr(hdr, &attr_size);
	if (!attr) {
		ret = -ENOMEM;
		goto free_spec;
	}

	ret = uburma_cmd_tlv_append(spec, spec_size, attr, attr_size);

	kfree(attr);
free_spec:
	kfree(spec);
	return ret;
}


static struct uburma_tlv_handler g_event_tlv_handler[] = {
	[0] = {0},
};

int uburma_event_tlv_parse(struct uburma_cmd_hdr *hdr, void *arg)
{
	struct uburma_cmd_spec *spec = NULL;
	struct uburma_cmd_attr *attr = NULL;
	uint32_t attr_size, spec_size;
	int ret;

	/* Command of hdr is valid, no need to check it */
	if (!g_event_tlv_handler[hdr->command].fill_spec_in) {
		uburma_log_err("Invalid command: %u.\n",
			       hdr->command);
		return -EINVAL;
	}

	spec_size = g_event_tlv_handler[hdr->command].spec_in_len;
	spec = kcalloc(spec_size, sizeof(struct uburma_cmd_spec),
		       GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	g_event_tlv_handler[hdr->command].fill_spec_in(arg, spec);

	attr = uburma_create_tlv_attr(hdr, &attr_size);
	if (!attr) {
		ret = -ENOMEM;
		goto free_spec;
	}

	ret = uburma_cmd_tlv_parse(spec, spec_size, attr, attr_size);

	kfree(attr);
free_spec:
	kfree(spec);
	return ret;
}

int uburma_event_tlv_append(struct uburma_cmd_hdr *hdr, void *arg)
{
	struct uburma_cmd_spec *spec = NULL;
	struct uburma_cmd_attr *attr = NULL;
	uint32_t attr_size, spec_size;
	int ret;

	/* Command of hdr is valid, no need to check it */
	if (!g_event_tlv_handler[hdr->command].fill_spec_out) {
		uburma_log_err("Invalid command: %u.\n",
			       hdr->command);
		return -EINVAL;
	}

	spec_size = g_event_tlv_handler[hdr->command].spec_out_len;
	spec = kcalloc(spec_size, sizeof(struct uburma_cmd_spec),
		       GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	g_event_tlv_handler[hdr->command].fill_spec_out(arg, spec);

	attr = uburma_create_tlv_attr(hdr, &attr_size);
	if (!attr) {
		ret = -ENOMEM;
		goto free_spec;
	}

	ret = uburma_cmd_tlv_append(spec, spec_size, attr, attr_size);

	kfree(attr);
free_spec:
	kfree(spec);
	return ret;
}
