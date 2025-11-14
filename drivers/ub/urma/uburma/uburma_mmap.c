// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Description: uburma mmap module
 * Author: Wen Chen
 * Create: 2024-03-18
 * Note:
 * History: 2024-03-18: Create file
 */

#include <linux/version.h>
#include <linux/mmap_lock.h>

#include <linux/sched/mm.h>

#include "uburma_log.h"
#include "uburma_types.h"

void uburma_umap_priv_init(struct uburma_umap_priv *priv,
			   struct vm_area_struct *vma)
{
	struct uburma_file *ufile = vma->vm_file->private_data;

	priv->vma = vma;
	vma->vm_private_data = priv;

	mutex_lock(&ufile->umap_mutex);
	list_add(&priv->node, &ufile->umaps_list);
	mutex_unlock(&ufile->umap_mutex);
}

void uburma_unmap_vma_pages(struct uburma_file *ufile)
{
	struct uburma_umap_priv *priv, *next_priv;
	struct mm_struct *mm;

	if (list_empty(&ufile->umaps_list))
		return;

	lockdep_assert_held(&ufile->cleanup_rwsem);
	while (1) {
		struct list_head local_list;

		INIT_LIST_HEAD(&local_list);
		mm = NULL;
		mutex_lock(&ufile->umap_mutex);
		list_for_each_entry_safe(priv, next_priv, &ufile->umaps_list,
					 node) {
			struct mm_struct *curr_mm = priv->vma->vm_mm;

			if (!mm) {
				if (!mmget_not_zero(curr_mm)) {
					list_del_init(&priv->node);
					continue;
				}
				mm = curr_mm;
				list_move_tail(&priv->node, &local_list);
			} else if (curr_mm == mm) {
				list_move_tail(&priv->node, &local_list);
			}
		}
		mutex_unlock(&ufile->umap_mutex);

		if (list_empty(&local_list)) {
			if (mm)
				mmput(mm);
			return;
		}

		mmap_read_lock(mm);

		list_for_each_entry_safe(priv, next_priv, &local_list, node) {
			struct vm_area_struct *vma = priv->vma;

			list_del_init(&priv->node);
			if (vma->vm_mm == mm)
				zap_vma_ptes(vma, vma->vm_start,
					     vma->vm_end - vma->vm_start);
		}
		mmap_read_unlock(mm);

		mmput(mm);
	}
}

static void uburma_umap_open(struct vm_area_struct *vma)
{
	struct uburma_file *ufile = vma->vm_file->private_data;
	struct uburma_umap_priv *priv;

	if (!down_read_trylock(&ufile->cleanup_rwsem))
		goto out_zap;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		goto out_unlock;

	uburma_umap_priv_init(priv, vma);

	up_read(&ufile->cleanup_rwsem);
	return;

out_unlock:
	up_read(&ufile->cleanup_rwsem);
out_zap:
	vma->vm_private_data = NULL;
	zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);
}

static void uburma_umap_close(struct vm_area_struct *vma)
{
	struct uburma_file *ufile = vma->vm_file->private_data;
	struct uburma_umap_priv *priv = vma->vm_private_data;

	if (!priv)
		return;

	mutex_lock(&ufile->umap_mutex);
	list_del(&priv->node);
	mutex_unlock(&ufile->umap_mutex);
	kfree(priv);
	vma->vm_private_data = NULL;
}

static vm_fault_t uburma_umap_fault(struct vm_fault *vmf)
{
	struct uburma_file *ufile = vmf->vma->vm_file->private_data;
	struct uburma_umap_priv *priv = vmf->vma->vm_private_data;
	struct page *page;

	if (unlikely(!priv))
		return VM_FAULT_SIGBUS;

	if (!(vmf->vma->vm_flags & (VM_WRITE | VM_MAYWRITE))) {
		vmf->page = ZERO_PAGE(0);
		get_page(vmf->page);
		return 0;
	}

	page = READ_ONCE(ufile->fault_page);
	if (likely(page)) {
		vmf->page = page;
		get_page(vmf->page);
		return 0;
	}

	mutex_lock(&ufile->umap_mutex);
	if (!ufile->fault_page) {
		ufile->fault_page = alloc_pages(vmf->gfp_mask | __GFP_ZERO, 0);
		if (!ufile->fault_page) {
			mutex_unlock(&ufile->umap_mutex);
			return VM_FAULT_SIGBUS;
		}
	}
	vmf->page = ufile->fault_page;
	get_page(vmf->page);
	mutex_unlock(&ufile->umap_mutex);

	return 0;
}

static const struct vm_operations_struct g_urma_umap_ops = {
	.open = uburma_umap_open,
	.close = uburma_umap_close,
	.fault = uburma_umap_fault,
};

const struct vm_operations_struct *uburma_get_umap_ops(void)
{
	return (const struct vm_operations_struct *)&g_urma_umap_ops;
}
