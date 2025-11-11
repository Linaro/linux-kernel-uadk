/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: ubcore api for other client kmod, such as uburma.
 * Author: Qian Guoxin, Ouyang Changchun
 * Create: 2021-8-3
 * Note:
 * History: 2021-8-3: Create file
 * History: 2021-11-25: add segment and jetty management function
 * History: 2022-7-25: modify file name
 */

#ifndef UBCORE_UAPI_H
#define UBCORE_UAPI_H

#include "ubcore_types.h"
/**
 * Application specifies the device to allocate an context.
 * @param[in] dev: ubcore_device found by add ops in the client.
 * @param[in] eid_index: function entity id (eid) index to set;
 * @param[in] udrv_data (optional): ucontext and user space driver data
 * @return: ubcore_ucontext pointer on success, NULL on fail.
 * Note: this API is called only by uburma representing user-space application,
 *       not by other kernel modules
 */
struct ubcore_ucontext *
ubcore_alloc_ucontext(struct ubcore_device *dev, uint32_t eid_index,
		      struct ubcore_udrv_priv *udrv_data);
/**
 * Free the allocated context.
 * @param[in] dev: device to free context.
 * @param[in] ucontext: handle of the allocated context.
 * Note: this API is called only by uburma representing user-space application,
 * not by other kernel modules
 */
void ubcore_free_ucontext(struct ubcore_device *dev,
			  struct ubcore_ucontext *ucontext);
/**
 * add a function entity id (eid) to ub device (for uvs)
 * @param[in] dev: the ubcore_device handle;
 * @param[in] ue_idx: ue_idx;
 * @param[in] cfg: eid and the upi of ue to which the eid belongs can be specified;
 * @return: the index of eid/upi, less than 0 indicating error
 */
int ubcore_add_ueid(struct ubcore_device *dev, uint16_t ue_idx,
		    struct ubcore_ueid_cfg *cfg);
/**
 * remove a function entity id (eid) specified by idx from ub device (for uvs)
 * @param[in] dev: the ubcore_device handle;
 * @param[in] ue_idx: ue_idx;
 * @param[in] cfg: eid and the upi of ue to which the eid belongs can be specified;
 * @return: 0 on success, other value on error
 */
int ubcore_delete_ueid(struct ubcore_device *dev, uint16_t ue_idx,
		       struct ubcore_ueid_cfg *cfg);
/**
 * query device attributes
 * @param[in] dev: the ubcore_device handle;
 * @param[out] attr: attributes returned to client
 * @return: 0 on success, other value on error
 */
int ubcore_query_device_attr(struct ubcore_device *dev,
			     struct ubcore_device_attr *attr);
/**
 * query device status
 * @param[in] dev: the ubcore_device handle;
 * @param[out] status: status returned to client
 * @return: 0 on success, other value on error
 */
int ubcore_query_device_status(struct ubcore_device *dev,
			       struct ubcore_device_status *status);
/**
 * query stats
 * @param[in] dev: the ubcore_device handle;
 * @param[in] key: stats type and key;
 * @param[in/out] val: addr and len of value
 * @return: 0 on success, other value on error
 */
int ubcore_query_stats(struct ubcore_device *dev, struct ubcore_stats_key *key,
		       struct ubcore_stats_val *val);
/**
 * query resource
 * @param[in] dev: the ubcore_device handle;
 * @param[in] key: resource type and key;
 * @param[in/out] val: addr and len of value
 * @return: 0 on success, other value on error
 */
int ubcore_query_resource(struct ubcore_device *dev, struct ubcore_res_key *key,
			  struct ubcore_res_val *val);
/**
 * config device
 * @param[in] dev: the ubcore_device handle;
 * @param[in] cfg: device configuration
 * @return: 0 on success, other value on error
 */
int ubcore_config_device(struct ubcore_device *dev,
			 struct ubcore_device_cfg *cfg);
/**
 * set ctx data of a client
 * @param[in] dev: the ubcore_device handle;
 * @param[in] client: ubcore client pointer
 * @param[in] data (optional): client private data to be set
 * @return: 0 on success, other value on error
 */
void ubcore_set_client_ctx_data(struct ubcore_device *dev,
				struct ubcore_client *client, void *data);
/**
 * get ctx data of a client
 * @param[in] dev: the ubcore_device handle;
 * @param[in] client: ubcore client pointer
 * @return: client private data set before
 */
void *ubcore_get_client_ctx_data(struct ubcore_device *dev,
				 struct ubcore_client *client);
/**
 * Register a new client to ubcore
 * @param[in] dev: the ubcore_device handle;
 * @param[in] new_client: ubcore client to be registered
 * @return: 0 on success, other value on error
 */
int ubcore_register_client(struct ubcore_client *new_client);
/**
 * Unregister a client from ubcore
 * @param[in] rm_client: ubcore client to be unregistered
 */
void ubcore_unregister_client(struct ubcore_client *rm_client);
/**
 * alloc token to ubcore device
 * @param[in] dev: the ubcore device handle;
 * @param[in] flag: token_id_flag;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: token id pointer on success, NULL on error
 */
struct ubcore_token_id *ubcore_alloc_token_id(struct ubcore_device *dev,
					      union ubcore_token_id_flag flag,
					      struct ubcore_udata *udata);
/**
 * free token id from ubcore device
 * @param[in] token_id: the token_id id alloced before;
 * @return: 0 on success, other value on error
 */
int ubcore_free_token_id(struct ubcore_token_id *token_id);
/**
 * register segment to ubcore device
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: segment configurations
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: target segment pointer on success, NULL on error
 */
struct ubcore_target_seg *ubcore_register_seg(struct ubcore_device *dev,
					      struct ubcore_seg_cfg *cfg,
					      struct ubcore_udata *udata);
/**
 * unregister segment from ubcore device
 * @param[in] tseg: the segment registered before;
 * @return: 0 on success, other value on error
 */
int ubcore_unregister_seg(struct ubcore_target_seg *tseg);
/**
 * import a remote segment to ubcore device
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: import configurations
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: target segment handle on success, NULL on error
 */
struct ubcore_target_seg *ubcore_import_seg(struct ubcore_device *dev,
					    struct ubcore_target_seg_cfg *cfg,
					    struct ubcore_udata *udata);
/**
 * unimport seg from ubcore device
 * @param[in] tseg: the segment imported before;
 * @return: 0 on success, other value on error
 */
int ubcore_unimport_seg(struct ubcore_target_seg *tseg);
/**
 * create jfc with ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: jfc attributes and configurations
 * @param[in] jfce_handler (optional): completion event handler
 * @param[in] jfae_handler (optional): jfc async_event handler
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: jfc pointer on success, NULL on error
 */
struct ubcore_jfc *ubcore_create_jfc(struct ubcore_device *dev,
				     struct ubcore_jfc_cfg *cfg,
				     ubcore_comp_callback_t jfce_handler,
				     ubcore_event_callback_t jfae_handler,
				     struct ubcore_udata *udata);
/**
 * modify jfc from ubcore device.
 * @param[in] jfc: the jfc created before;
 * @param[in] attr: ubcore jfc attributes;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 */
int ubcore_modify_jfc(struct ubcore_jfc *jfc, struct ubcore_jfc_attr *attr,
		      struct ubcore_udata *udata);
/**
 * destroy jfc from ubcore device.
 * @param[in] jfc: the jfc created before;
 * @return: 0 on success, other value on error
 */
int ubcore_delete_jfc(struct ubcore_jfc *jfc);
/**
 * batch destroy jfc from ubcore device.
 * @param[in] jfc_arr: the jfc array created before;
 * @param[in] jfc_num: jfc array length;
 * @param[out] bad_jfc_index: when error, return error jfc index in the array;
 * @return: 0 on success, EINVAL on invalid parameter, other value on other batch
 * delete errors.
 * If delete error happens(except invalid parameter), stop at the first failed
 * jfc and return, these jfc before the failed jfc will be deleted normally.
 */
int ubcore_delete_jfc_batch(struct ubcore_jfc **jfc_arr, int jfc_num,
			    int *bad_jfc_index);
/**
 * rearm jfc.
 * @param[in] jfc: the jfc created before;
 * @param[in] solicited_only: rearm notify by message marked with solicited flag
 * @return: 0 on success, other value on error
 */
int ubcore_rearm_jfc(struct ubcore_jfc *jfc, bool solicited_only);
/**
 * create jfs with ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: jfs configurations
 * @param[in] jfae_handler (optional): jfs async_event handler
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: jfs pointer on success, NULL on error
 */
struct ubcore_jfs *ubcore_create_jfs(struct ubcore_device *dev,
				     struct ubcore_jfs_cfg *cfg,
				     ubcore_event_callback_t jfae_handler,
				     struct ubcore_udata *udata);
/**
 * modify jfs from ubcore device.
 * @param[in] jfs: the jfs created before;
 * @param[in] attr: ubcore jfs attributes;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 */
int ubcore_modify_jfs(struct ubcore_jfs *jfs, struct ubcore_jfs_attr *attr,
		      struct ubcore_udata *udata);
/**
 * query jfs from ubcore device.
 * @param[in] jfs: the jfs created before;
 * @param[out] cfg: jfs configurations;
 * @param[out] attr: ubcore jfs attributes;
 * @return: 0 on success, other value on error
 */
int ubcore_query_jfs(struct ubcore_jfs *jfs, struct ubcore_jfs_cfg *cfg,
		     struct ubcore_jfs_attr *attr);
/**
 * destroy jfs from ubcore device.
 * @param[in] jfs: the jfs created before;
 * @return: 0 on success, other value on error
 */
int ubcore_delete_jfs(struct ubcore_jfs *jfs);
/**
 * batch destroy jfs from ubcore device.
 * @param[in] jfs_arr: the jfs array created before;
 * @param[in] jfs_num: jfs array length;
 * @param[out] bad_jfs_index: when error, return error jfs index in the array;
 * @return: 0 on success, EINVAL on invalid parameter, other value on other batch
 * delete errors.
 * If delete error happens(except invalid parameter), stop at the first failed
 * jfs and return, these jfs before the failed jfs will be deleted normally.
 */
int ubcore_delete_jfs_batch(struct ubcore_jfs **jfs_arr, int jfs_num,
			    int *bad_jfs_index);
/**
 * return the wrs in JFS that is not consumed to the application through cr.
 * @param[in] jfs: the jfs created before;
 * @param[in] cr_cnt: the maximum number of CRs expected to be returned;
 * @param[out] cr: the addr of returned CRs;
 * @return: the number of completion record returned, 0 means no completion record returned,
 * -1 on error
 */
int ubcore_flush_jfs(struct ubcore_jfs *jfs, int cr_cnt, struct ubcore_cr *cr);
/**
 * create jfr with ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: jfr configurations
 * @param[in] jfae_handler (optional): jfr async_event handler
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: jfr pointer on success, NULL on error
 */
struct ubcore_jfr *ubcore_create_jfr(struct ubcore_device *dev,
				     struct ubcore_jfr_cfg *cfg,
				     ubcore_event_callback_t jfae_handler,
				     struct ubcore_udata *udata);
/**
 * modify jfr from ubcore device.
 * @param[in] jfr: the jfr created before;
 * @param[in] attr: ubcore jfr attr;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 */
int ubcore_modify_jfr(struct ubcore_jfr *jfr, struct ubcore_jfr_attr *attr,
		      struct ubcore_udata *udata);
/**
 * query jfr from ubcore device.
 * @param[in] jfr: the jfr created before;
 * @param[out] cfg: jfr configurations;
 * @param[out] attr: ubcore jfr attributes;
 * @return: 0 on success, other value on error
 */
int ubcore_query_jfr(struct ubcore_jfr *jfr, struct ubcore_jfr_cfg *cfg,
		     struct ubcore_jfr_attr *attr);
/**
 * destroy jfr from ubcore device.
 * @param[in] jfr: the jfr created before;
 * @return: 0 on success, other value on error
 */
int ubcore_delete_jfr(struct ubcore_jfr *jfr);
/**
 * batch destroy jfr from ubcore device.
 * @param[in] jfr_arr: the jfr array created before;
 * @param[in] jfr_num: jfr array length;
 * @param[out] bad_jfr_index: when error, return error jfr index in the array;
 * @return: 0 on success, EINVAL on invalid parameter, other value on other batch
 * delete errors.
 * If delete error happens(except invalid parameter), stop at the first failed
 * jfr and return, these jfr before the failed jfr will be deleted normally.
 */
int ubcore_delete_jfr_batch(struct ubcore_jfr **jfr_arr, int jfr_num,
			    int *bad_jfr_index);
/**
 * create jetty with ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: jetty attributes and configurations
 * @param[in] jfae_handler (optional): jetty async_event handler
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: jetty pointer on success, NULL on error
 */
struct ubcore_jetty *ubcore_create_jetty(struct ubcore_device *dev,
					 struct ubcore_jetty_cfg *cfg,
					 ubcore_event_callback_t jfae_handler,
					 struct ubcore_udata *udata);
/**
 * modify jetty attributes.
 * @param[in] jetty: the jetty created before;
 * @param[in] attr: ubcore jetty attributes;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 */
int ubcore_modify_jetty(struct ubcore_jetty *jetty,
			struct ubcore_jetty_attr *attr,
			struct ubcore_udata *udata);
/**
 * query jetty from ubcore device.
 * @param[in] jetty: the jetty created before;
 * @param[out] cfg: jetty configurations;
 * @param[out] attr: ubcore jetty attributes;
 * @return: 0 on success, other value on error
 */
int ubcore_query_jetty(struct ubcore_jetty *jetty, struct ubcore_jetty_cfg *cfg,
		       struct ubcore_jetty_attr *attr);
/**
 * destroy jetty from ubcore device.
 * @param[in] jetty: the jetty created before;
 * @return: 0 on success, other value on error
 */
int ubcore_delete_jetty(struct ubcore_jetty *jetty);
/**
 * batch destroy jetty from ubcore device.
 * @param[in] jetty_arr: the jetty array created before;
 * @param[in] jetty_num: jetty array length;
 * @param[out] bad_jetty_index: when error, return error jetty index in the array;
 * @return: 0 on success, EINVAL on invalid parameter, other value on other batch
 * delete errors.
 * If delete error happens(except invalid parameter), stop at the first failed
 * jetty and return, these jetty before the failed jetty will be deleted normally.
 */
int ubcore_delete_jetty_batch(struct ubcore_jetty **jetty_arr, int jetty_num,
			      int *bad_jetty_index);
/**
 * return the wrs in JETTY that is not consumed to the application through cr.
 * @param[in] jetty: the jetty created before;
 * @param[in] cr_cnt: the maximum number of CRs expected to be returned;
 * @param[out] cr: the addr of returned CRs;
 * @return: the number of completion record returned, 0 means no completion record returned,
 * -1 on error
 */
int ubcore_flush_jetty(struct ubcore_jetty *jetty, int cr_cnt,
		       struct ubcore_cr *cr);
/**
 * import jfr to ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: remote jfr attributes and import configurations
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: target jfr pointer on success, NULL on error
 */
struct ubcore_tjetty *ubcore_import_jfr(struct ubcore_device *dev,
					struct ubcore_tjetty_cfg *cfg,
					struct ubcore_udata *udata);

/**
 * import jfr to ubcore device by control plane.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: remote jfr attributes and import configurations;
 * @param[in] active_tp_cfg: tp configuration to active;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: target jfr pointer on success, NULL on error
 */
struct ubcore_tjetty *
ubcore_import_jfr_ex(struct ubcore_device *dev, struct ubcore_tjetty_cfg *cfg,
		     struct ubcore_active_tp_cfg *active_tp_cfg,
		     struct ubcore_udata *udata);

/**
 * unimport jfr from ubcore device.
 * @param[in] tjfr: the target jfr imported before;
 * @return: 0 on success, other value on error
 */
int ubcore_unimport_jfr(struct ubcore_tjetty *tjfr);
/**
 * import jetty to ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: remote jetty attributes and import configurations
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: target jetty pointer on success, NULL on error
 */
struct ubcore_tjetty *ubcore_import_jetty(struct ubcore_device *dev,
					  struct ubcore_tjetty_cfg *cfg,
					  struct ubcore_udata *udata);

/**
 * import jetty to ubcore device by control plane.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: remote jetty attributes and import configurations
 * @param[in] active_tp_cfg: tp configuration to active
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: target jetty pointer on success, NULL on error
 */
struct ubcore_tjetty *
ubcore_import_jetty_ex(struct ubcore_device *dev, struct ubcore_tjetty_cfg *cfg,
		       struct ubcore_active_tp_cfg *active_tp_cfg,
		       struct ubcore_udata *udata);

/**
 * unimport jetty from ubcore device.
 * @param[in] tjetty: the target jetty imported before;
 * @return: 0 on success, other value on error
 */
int ubcore_unimport_jetty(struct ubcore_tjetty *tjetty);
/**
 * Advise jfr: construct the transport channel for jfs and remote jfr.
 * @param[in] jfs: jfs to use to construct the transport channel;
 * @param[in] tjfr: target jfr to reach;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 */
int ubcore_advise_jfr(struct ubcore_jfs *jfs, struct ubcore_tjetty *tjfr,
		      struct ubcore_udata *udata);
/**
 * Unadvise jfr: Tear down the transport channel from jfs to remote jfr.
 * @param[in] jfs: jfs to use to destruct the transport channel;
 * @param[in] tjfr: target jfr advised before;
 * @return: 0 on success, other value on error
 */
int ubcore_unadvise_jfr(struct ubcore_jfs *jfs, struct ubcore_tjetty *tjfr);
/**
 * Advise jetty: construct the transport channel between local jetty and remote jetty.
 * @param[in] jetty: local jetty to construct the transport channel;
 * @param[in] tjetty: target jetty to reach imported before;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 */
int ubcore_advise_jetty(struct ubcore_jetty *jetty,
			struct ubcore_tjetty *tjetty,
			struct ubcore_udata *udata);
/**
 * Unadvise jetty: deconstruct the transport channel between local jetty and remote jetty.
 * @param[in] jetty: local jetty to destruct the transport channel;
 * @param[in] tjetty: target jetty advised before;
 * @return: 0 on success, other value on error
 */
int ubcore_unadvise_jetty(struct ubcore_jetty *jetty,
			  struct ubcore_tjetty *tjetty);
/**
 * Bind jetty: Bind local jetty with remote jetty, and construct a transport channel between them.
 * @param[in] jetty: local jetty to bind;
 * @param[in] tjetty: target jetty imported before;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 * Note: A local jetty can be binded with only one remote jetty.
 * Only supported by jetty with URMA_TM_RC.
 */
int ubcore_bind_jetty(struct ubcore_jetty *jetty, struct ubcore_tjetty *tjetty,
		      struct ubcore_udata *udata);

/**
 * Bind jetty: Bind local jetty with remote jetty, and construct a transport channel between them.
 * @param[in] jetty: local jetty to bind;
 * @param[in] tjetty: target jetty imported before;
 * @param[in] active_tp_cfg: tp configuration to active;
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 * Note: A local jetty can be binded with only one remote jetty.
 * Only supported by jetty with URMA_TM_RC.
 */
int ubcore_bind_jetty_ex(struct ubcore_jetty *jetty,
			 struct ubcore_tjetty *tjetty,
			 struct ubcore_active_tp_cfg *active_tp_cfg,
			 struct ubcore_udata *udata);
/**
 * Unbind jetty: Unbind local jetty with remote jetty,
 * and tear down the transport channel between them.
 * @param[in] jetty: local jetty to unbind;
 * @return: 0 on success, other value on error
 */
int ubcore_unbind_jetty(struct ubcore_jetty *jetty);
/**
 * create jetty group with ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: jetty group  configurations
 * @param[in] jfae_handler (optional): jetty async_event handler
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: jetty group pointer on success, NULL on error
 */
struct ubcore_jetty_group *ubcore_create_jetty_grp(
	struct ubcore_device *dev, struct ubcore_jetty_grp_cfg *cfg,
	ubcore_event_callback_t jfae_handler, struct ubcore_udata *udata);
/**
 * destroy jetty group from ubcore device.
 * @param[in] jetty_grp: the jetty group created before;
 * @return: 0 on success, other value on error
 */
int ubcore_delete_jetty_grp(struct ubcore_jetty_group *jetty_grp);
/**
 * import jetty to ubcore device.
 * @param[in] dev: the ubcore device handle;
 * @param[in] cfg: remote jetty attributes and import configurations
 * @param[in] timeout: max time to wait (milliseconds)
 * @param[in] cb: callback function pointer with custom user argument
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: target jetty pointer on success, NULL on error
 */
struct ubcore_tjetty *ubcore_import_jetty_async(struct ubcore_device *dev,
						struct ubcore_tjetty_cfg *cfg,
						int timeout,
						struct ubcore_import_cb *cb,
						struct ubcore_udata *udata);
/**
 * unimport jetty from ubcore device.
 * @param[in] tjetty: the target jetty imported before;
 * @param[in] timeout: max time to wait (milliseconds)
 * @param[in] cb: callback function pointer with custom user argument
 * @return: 0 on success, other value on error
 */
int ubcore_unimport_jetty_async(struct ubcore_tjetty *tjetty, int timeout,
				struct ubcore_unimport_cb *cb);
/**
 * Bind jetty: Bind local jetty with remote jetty, and construct a transport channel between them.
 * @param[in] jetty: local jetty to bind;
 * @param[in] tjetty: target jetty imported before;
 * @param[in] timeout: max time to wait (milliseconds)
 * @param[in] cb: callback function pointer with custom user argument
 * @param[in] udata (optional): ucontext and user space driver data
 * @return: 0 on success, other value on error
 * Note: A local jetty can be binded with only one remote jetty.
 * Only supported by jetty with URMA_TM_RC.
 */
int ubcore_bind_jetty_async(struct ubcore_jetty *jetty,
			    struct ubcore_tjetty *tjetty, int timeout,
			    struct ubcore_bind_cb *cb,
			    struct ubcore_udata *udata);
/**
 * Unbind jetty: Unbind local jetty with remote jetty,
 * and tear down the transport channel between them.
 * @param[in] jetty: local jetty to unbind;
 * @param[in] timeout: max time to wait (milliseconds)
 * @param[in] cb: callback function pointer with custom user argument
 * @return: 0 on success, other value on error
 */
int ubcore_unbind_jetty_async(struct ubcore_jetty *jetty, int timeout,
			      struct ubcore_unbind_cb *cb);

/**
 * get tp list from ubep.
 * @param[in] dev: ubcore device pointer created before;
 * @param[in] cfg: configuration to be matched;
 * @param[in && out] tp_cnt: tp_cnt is the length of tp_list buffer as in parameter;
 *                           tp_cnt is the number of tp as out parameter;
 * @param[out] tp_list: tp info list to get;
 * @param[in && out] udata: ucontext and user space driver data;
 * @return: 0 on success, other value on error
 */
int ubcore_get_tp_list(struct ubcore_device *dev, struct ubcore_get_tp_cfg *cfg,
		       uint32_t *tp_cnt, struct ubcore_tp_info *tp_list,
		       struct ubcore_udata *udata);

/**
 * set tp attributions by control plane.
 * @param[in] dev: ubcore device pointer created before;
 * @param[in] tp_handle: tp_handle got by ubcore_get_tp_list;
 * @param[in] tp_attr_cnt: number of tp attributions;
 * @param[in] tp_attr_bitmap: tp attributions bitmap, current bitmap is as follow:
 *            0-retry_times_init: 3 bit    1-at: 5 bit             2-SIP: 128 bit
 *            3-DIP: 128 bit               4-SMA: 48 bit           5-DMA: 48 bit
 *            6-vlan_id: 12 bit            7-vlan_en: 1 bit        8-dscp: 6 bit
 *            9-at_times: 5 bit            10-sl: 4 bit            11-tti: 8 bit
 * @param[in] tp_attr: tp attribution values to set;
 * @param[in && out] udata: ucontext and user space driver data;
 * @return: 0 on success, other value on error
 */
int ubcore_set_tp_attr(struct ubcore_device *dev, const uint64_t tp_handle,
		       const uint8_t tp_attr_cnt, const uint32_t tp_attr_bitmap,
		       const struct ubcore_tp_attr_value *tp_attr,
		       struct ubcore_udata *udata);

/**
 * get tp attributions by control plane.
 * @param[in] dev: ubcore device pointer created before;
 * @param[in] tp_handle: tp_handle got by ubcore_get_tp_list;
 * @param[out] tp_attr_cnt: number of tp attributions;
 * @param[out] tp_attr_bitmap: tp attributions bitmap, current bitmap is as follow:
 *            0-retry_times_init: 3 bit    1-at: 5 bit             2-SIP: 128 bit
 *            3-DIP: 128 bit               4-SMA: 48 bit           5-DMA: 48 bit
 *            6-vlan_id: 12 bit            7-vlan_en: 1 bit        8-dscp: 6 bit
 *            9-at_times: 5 bit            10-sl: 4 bit            11-ttl: 8 bit
 * @param[out] buf: tp attributions buffer to set;
 * @param[in] buf_size: tp attributions buffer size;
 * @param[in && out] udata: ucontext and user space driver data;
 * @return: 0 on success, other value on error
 */
int ubcore_get_tp_attr(struct ubcore_device *dev, const uint64_t tp_handle,
		       uint8_t *tp_attr_cnt, uint32_t *tp_attr_bitmap,
		       struct ubcore_tp_attr_value *tp_attr,
		       struct ubcore_udata *udata);

/**
 * exchange tp info from ubep.
 * @param[in] dev: ubcore device pointer created before;
 * @param[in] cfg: configuration to be matched;
 * @param[in] tp_handle: local tp handle;
 * @param[in] tx_psn: local packet sequence number;
 * @param[out] peer_tp_handle: tp_handle got by ubcore_exchange_tp_info;
 * @param[out] rx_psn: remote packet sequence number;
 * @param[in] udata: [Optional] udata should be NULL when called
 *                   by kernel application and be valid when called
 *                   by user space application
 * @return: 0 on success, other value on error
 */
int ubcore_exchange_tp_info(struct ubcore_device *dev,
				struct ubcore_get_tp_cfg *cfg, uint64_t tp_handle,
				uint32_t tx_psn, uint64_t *peer_tp_handle,
				uint32_t *rx_psn, struct ubcore_udata *udata);

/**
 * operation of user ioctl cmd.
 * @param[in] dev: the ubcore device handle;
 * @param[in] k_user_ctl: kdrv user control command pointer;
 * @return: 0 on success, other value on error
 */
int ubcore_user_control(struct ubcore_device *dev,
			struct ubcore_user_ctl *k_user_ctl);
/**
 * Client register an async_event handler to ubcore
 * @param[in] dev: the ubcore device handle;
 * @param[in] handler: async_event handler to be registered
 * Note: the handler will be called when driver reports an async_event with
 * ubcore_dispatch_async_event
 */
void ubcore_register_event_handler(struct ubcore_device *dev,
				   struct ubcore_event_handler *handler);
/**
 * Client unregister async_event handler from ubcore
 * @param[in] dev: the ubcore device handle;
 * @param[in] handler: async_event handler to be unregistered
 */
void ubcore_unregister_event_handler(struct ubcore_device *dev,
				     struct ubcore_event_handler *handler);

/* data path API */
/**
 * post jfs wr.
 * @param[in] jfs: the jfs created before;
 * @param[in] wr: the wr to be posted;
 * @param[out] bad_wr: the first failed wr;
 * @return: 0 on success, other value on error
 */
int ubcore_post_jfs_wr(struct ubcore_jfs *jfs, struct ubcore_jfs_wr *wr,
		       struct ubcore_jfs_wr **bad_wr);
/**
 * post jfr wr.
 * @param[in] jfr: the jfr created before;
 * @param[in] wr: the wr to be posted;
 * @param[out] bad_wr: the first failed wr;
 * @return: 0 on success, other value on error
 */
int ubcore_post_jfr_wr(struct ubcore_jfr *jfr, struct ubcore_jfr_wr *wr,
		       struct ubcore_jfr_wr **bad_wr);
/**
 * post jetty send wr.
 * @param[in] jetty: the jetty created before;
 * @param[in] wr: the wr to be posted;
 * @param[out] bad_wr: the first failed wr;
 * @return: 0 on success, other value on error
 */
int ubcore_post_jetty_send_wr(struct ubcore_jetty *jetty,
			      struct ubcore_jfs_wr *wr,
			      struct ubcore_jfs_wr **bad_wr);
/**
 * post jetty receive wr.
 * @param[in] jetty: the jetty created before;
 * @param[in] wr: the wr to be posted;
 * @param[out] bad_wr: the first failed wr;
 * @return: 0 on success, other value on error
 */
int ubcore_post_jetty_recv_wr(struct ubcore_jetty *jetty,
			      struct ubcore_jfr_wr *wr,
			      struct ubcore_jfr_wr **bad_wr);
/**
 * poll jfc.
 * @param[in] jfc: the jfc created before;
 * @param[in] cr_cnt: the maximum number of CRs expected to be polled;
 * @param[out] cr: the addr of returned CRs;
 * @return: the number of completion record returned, 0 means no completion record returned,
 * -1 on error
 */
int ubcore_poll_jfc(struct ubcore_jfc *jfc, int cr_cnt, struct ubcore_cr *cr);

/**
 * Find ubcore device by eid and transport type
 * @param[in] eid: the eid of device;
 * @param[in] type: transport type;
 * @return: ubcore device pointer on success, NULL on error
 */
struct ubcore_device *ubcore_get_device_by_eid(union ubcore_eid *eid,
					       enum ubcore_transport_type type);

// for system not support cgroup
#ifndef CONFIG_CGROUP_RDMA
static inline void ubcore_cgroup_reg_dev(struct ubcore_device *dev)
{
}

static inline void ubcore_cgroup_unreg_dev(struct ubcore_device *dev)
{
}

static inline int ubcore_cgroup_try_charge(struct ubcore_cg_object *cg_obj,
					   struct ubcore_device *dev,
					   enum ubcore_resource_type type)
{
	return 0;
}

static inline void ubcore_cgroup_uncharge(struct ubcore_cg_object *cg_obj,
					  struct ubcore_device *dev,
					  enum ubcore_resource_type type)
{
}
#else
/**
 * Client register cgroup dev
 * @param[in] dev: the ubcore device handle;
 */
void ubcore_cgroup_reg_dev(struct ubcore_device *dev);

/**
 * Client unregister cgroup dev
 * @param[in] dev: the ubcore device handle;
 */
void ubcore_cgroup_unreg_dev(struct ubcore_device *dev);

/**
 * Client try to charge cgroup count
 * @param[in] cg_obj: the cgroup obj
 * @param[in] dev: the ubcore device handle;
 * @param[in] type: the cgroup resource type
 * @return: 0 on success, other value on error
 */
int ubcore_cgroup_try_charge(struct ubcore_cg_object *cg_obj,
			     struct ubcore_device *dev,
			     enum ubcore_resource_type type);

/**
 * Client uncharge cgroup count
 * @param[in] cg_obj: the cgroup obj
 * @param[in] dev: the ubcore device handle;
 * @param[in] type: the cgroup resource type
 */
void ubcore_cgroup_uncharge(struct ubcore_cg_object *cg_obj,
			    struct ubcore_device *dev,
			    enum ubcore_resource_type type);
#endif // CONFIG_CGROUP_RDMA

/**
 * Get primary or port eid from topo info
 * @param[in] tp_type: tp type, 0-RTP, 1-CTP, 2-UTP,
 *                     refer to enum ubcore_tp_type;
 * @param[in] src_v_eid: source virtual eid, refer
 *                     to source bonding eid;
 * @param[in] dst_v_eid: dest virtual eid, refer to
 *                     dest bonding eid;
 * @param[out] src_p_eid: source physical eid, refer
 *                      to source primary or port eid;
 * @param[out] dst_p_eid: dest physical eid, refer
 *                      to dest primary or port eid;
 */
int ubcore_get_topo_eid(uint32_t tp_type, union ubcore_eid *src_v_eid,
	union ubcore_eid *dst_v_eid, union ubcore_eid *src_p_eid,
	union ubcore_eid *dst_p_eid);

#endif
