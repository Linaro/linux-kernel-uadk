// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#define pr_fmt(fmt) "CDMA: " fmt
#define dev_fmt pr_fmt

#include <linux/fs.h>
#include <ub/ubus/ubus.h>
#include "cdma_ioctl.h"
#include "cdma_context.h"
#include "cdma_chardev.h"
#include "cdma_jfs.h"
#include "cdma_types.h"
#include "cdma_uobj.h"
#include "cdma.h"

#define CDMA_DEVICE_NAME "cdma/dev"

struct cdma_num_manager {
	struct idr idr;
	spinlock_t lock;
};

static struct cdma_num_manager cdma_num_mg = {
	.idr = IDR_INIT(cdma_num_mg.idr),
	.lock = __SPIN_LOCK_UNLOCKED(cdma_num_mg.lock),
};

static void cdma_num_free(struct cdma_dev *cdev)
{
	spin_lock(&cdma_num_mg.lock);
	idr_remove(&cdma_num_mg.idr, cdev->chardev.dev_num);
	spin_unlock(&cdma_num_mg.lock);
}

static inline u64 cdma_get_mmap_idx(struct vm_area_struct *vma)
{
	return (vma->vm_pgoff >> MAP_INDEX_SHIFT) & MAP_INDEX_MASK;
}

static inline int cdma_get_mmap_cmd(struct vm_area_struct *vma)
{
	return (vma->vm_pgoff & MAP_COMMAND_MASK);
}

static int cdma_num_alloc(struct cdma_dev *cdev)
{
#define CDMA_START 0
#define CDMA_END 0xffff
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock(&cdma_num_mg.lock);
	id = idr_alloc(&cdma_num_mg.idr, cdev, CDMA_START, CDMA_END, GFP_NOWAIT);
	spin_unlock(&cdma_num_mg.lock);
	idr_preload_end();

	return id;
}

static long cdma_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
#define CDMA_MAX_CMD_SIZE 8192
	struct cdma_file *cfile = (struct cdma_file *)file->private_data;
	struct cdma_ioctl_hdr hdr = { 0 };
	int ret;

	if (cmd == CDMA_SYNC) {
		ret = copy_from_user(&hdr, (void *)arg, sizeof(hdr));
		if (ret || hdr.args_len > CDMA_MAX_CMD_SIZE) {
			pr_err("copy user ret = %d, input parameter len = %u.\n",
				ret, hdr.args_len);
			return -EINVAL;
		}
		ret = cdma_cmd_parse(cfile, &hdr);
		return ret;
	}

	pr_err("invalid ioctl command, command = %u.\n", cmd);
	return -ENOIOCTLCMD;
}

static int cdma_remap_check_jfs_id(struct cdma_file *cfile, u32 jfs_id)
{
	struct cdma_dev *cdev = cfile->cdev;
	struct cdma_jfs *jfs;
	int ret = -EINVAL;

	spin_lock(&cdev->jfs_table.lock);
	jfs = idr_find(&cdev->jfs_table.idr_tbl.idr, jfs_id);
	if (!jfs) {
		spin_unlock(&cdev->jfs_table.lock);
		dev_err(cdev->dev,
			"check failed, jfs_id = %u not exist.\n", jfs_id);
		return ret;
	}

	if (cfile->uctx != jfs->base_jfs.ctx) {
		dev_err(cdev->dev,
			"check failed, jfs_id = %u, uctx invalid\n", jfs_id);
		spin_unlock(&cdev->jfs_table.lock);
		return -EINVAL;
	}
	spin_unlock(&cdev->jfs_table.lock);

	return 0;
}

static int cdma_remap_pfn_range(struct cdma_file *cfile, struct vm_area_struct *vma)
{
#define JFC_DB_UNMAP_BOUND 1
	struct cdma_dev *cdev = cfile->cdev;
	resource_size_t db_addr;
	u64 address;
	u32 jfs_id;
	u32 cmd;

	db_addr = cdev->db_base;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	cmd = cdma_get_mmap_cmd(vma);
	switch (cmd) {
	case CDMA_MMAP_JFC_PAGE:
		if (io_remap_pfn_range(vma, vma->vm_start,
				       jfc_arm_mode > JFC_DB_UNMAP_BOUND ?
				       (uint64_t)db_addr >> PAGE_SHIFT :
				       page_to_pfn(cdev->arm_db_page),
				       PAGE_SIZE, vma->vm_page_prot)) {
			dev_err(cdev->dev, "remap jfc page fail.\n");
			return -EAGAIN;
		}
		break;
	case CDMA_MMAP_JETTY_DSQE:
		jfs_id = cdma_get_mmap_idx(vma);
		if (cdma_remap_check_jfs_id(cfile, jfs_id)) {
			dev_err(cdev->dev,
				"mmap failed, invalid jfs_id = %u\n", jfs_id);
			return -EINVAL;
		}

		address = (uint64_t)db_addr + CDMA_JETTY_DSQE_OFFSET + jfs_id * PAGE_SIZE;

		if (io_remap_pfn_range(vma, vma->vm_start, address >> PAGE_SHIFT,
				       PAGE_SIZE, vma->vm_page_prot)) {
			dev_err(cdev->dev, "remap jetty page failed.\n");
			return -EAGAIN;
		}
		break;
	default:
		dev_err(cdev->dev,
			"mmap failed, cmd(%u) is not supported.\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static int cdma_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cdma_file *cfile = (struct cdma_file *)file->private_data;
	int ret;

	if (((vma->vm_end - vma->vm_start) % PAGE_SIZE) != 0) {
		pr_err("mmap failed, expect vm area size to be an integer multiple of page size.\n");
		return -EINVAL;
	}

	mutex_lock(&cfile->ctx_mutex);
	ret = cdma_remap_pfn_range(cfile, vma);
	if (ret) {
		mutex_unlock(&cfile->ctx_mutex);
		return ret;
	}
	mutex_unlock(&cfile->ctx_mutex);

	return 0;
}

static int cdma_open(struct inode *inode, struct file *file)
{
	struct cdma_chardev *chardev;
	struct cdma_file *cfile;
	struct cdma_dev *cdev;

	chardev = container_of(inode->i_cdev, struct cdma_chardev, cdev);
	cdev = container_of(chardev, struct cdma_dev, chardev);

	cfile = kzalloc(sizeof(struct cdma_file), GFP_KERNEL);
	if (!cfile)
		return -ENOMEM;

	cdma_init_uobj_idr(cfile);
	mutex_lock(&cdev->file_mutex);
	cfile->cdev = cdev;
	cfile->uctx = NULL;
	kref_init(&cfile->ref);
	file->private_data = cfile;
	mutex_init(&cfile->ctx_mutex);
	list_add_tail(&cfile->list, &cdev->file_list);
	nonseekable_open(inode, file);
	mutex_unlock(&cdev->file_mutex);

	return 0;
}

static int cdma_close(struct inode *inode, struct file *file)
{
	struct cdma_file *cfile = (struct cdma_file *)file->private_data;
	struct cdma_dev *cdev;

	cdev = cfile->cdev;

	mutex_lock(&cdev->file_mutex);
	list_del(&cfile->list);
	mutex_unlock(&cdev->file_mutex);

	mutex_lock(&cfile->ctx_mutex);
	cdma_cleanup_context_uobj(cfile);
	cfile->uctx = NULL;

	mutex_unlock(&cfile->ctx_mutex);
	kref_put(&cfile->ref, cdma_release_file);
	pr_debug("cdma close success.\n");

	return 0;
}

static const struct file_operations cdma_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = cdma_ioctl,
	.mmap = cdma_mmap,
	.open = cdma_open,
	.release = cdma_close,
};

void cdma_destroy_chardev(struct cdma_dev *cdev)
{
	struct cdma_chardev *chardev = &cdev->chardev;

	if (!chardev->dev)
		return;

	device_destroy(cdma_cdev_class, chardev->devno);
	cdev_del(&chardev->cdev);
	unregister_chrdev_region(chardev->devno, CDMA_MAX_DEVICES);
	cdma_num_free(cdev);
}

int cdma_create_chardev(struct cdma_dev *cdev)
{
	struct cdma_chardev *chardev = &cdev->chardev;
	int ret;

	chardev->dev_num = cdma_num_alloc(cdev);
	if (chardev->dev_num < 0) {
		dev_err(cdev->dev, "alloc dev_num failed, ret = %d\n", chardev->dev_num);
		return -ENOMEM;
	}

	ret = snprintf(chardev->name, sizeof(chardev->name),
		       "%s.%d", CDMA_DEVICE_NAME, chardev->dev_num);
	if (ret < 0) {
		dev_err(cdev->dev, "sprintf failed in create cdma chardev\n");
		goto num_free;
	}

	ret = alloc_chrdev_region(&chardev->devno, 0, CDMA_MAX_DEVICES,
				  chardev->name);
	if (ret) {
		dev_err(cdev->dev, "alloc chrdev region failed, ret = %d\n", ret);
		goto num_free;
	}

	cdev_init(&chardev->cdev, &cdma_ops);
	ret = cdev_add(&chardev->cdev, chardev->devno, CDMA_MAX_DEVICES);
	if (ret) {
		dev_err(cdev->dev, "cdev add failed, ret = %d\n", ret);
		goto chrdev_unregister;
	}

	chardev->dev = device_create(cdma_cdev_class, NULL, chardev->devno,
				     NULL, chardev->name);
	if (IS_ERR(chardev->dev)) {
		ret = PTR_ERR(chardev->dev);
		dev_err(cdev->dev, "create device failed, ret = %d\n", ret);
		goto cdev_delete;
	}

	dev_dbg(cdev->dev, "create chardev: %s succeeded\n", chardev->name);
	return 0;

cdev_delete:
	cdev_del(&chardev->cdev);
chrdev_unregister:
	unregister_chrdev_region(chardev->devno, CDMA_MAX_DEVICES);
num_free:
	cdma_num_free(cdev);
	return ret;
}

void cdma_release_file(struct kref *ref)
{
	struct cdma_file *cfile = container_of(ref, struct cdma_file, ref);

	mutex_destroy(&cfile->ctx_mutex);
	idr_destroy(&cfile->idr);
	kfree(cfile);
}
