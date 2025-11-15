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


static struct uburma_tlv_handler g_tlv_handler[] = {
	[0] = {0},
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
