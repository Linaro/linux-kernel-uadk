// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: uburma device ops file
 * Author: Qian Guoxin
 * Create: 2021-08-04
 * Note:
 * History: 2021-08-04: Create file
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "ub/urma/ubcore_types.h"
#include "ub/urma/ubcore_uapi.h"

#include "uburma_log.h"
#include "uburma_types.h"
#include "uburma_uobj.h"
#include "uburma_cmd.h"
#include "uburma_mmap.h"

static void uburma_mmu_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct uburma_mn *ub_mn = container_of(mn, struct uburma_mn, mn);
	struct uburma_file *file =
		container_of(ub_mn, struct uburma_file, ub_mn);
	struct uburma_device *ubu_dev = file->ubu_dev;
	struct ubcore_device *ubc_dev;
	int srcu_idx;

	uburma_log_debug("Start mmu release uobjs and ucontext\n");
	if (ub_mn->mm != mm || !ub_mn->mm) {
		uburma_log_debug("mm already released.\n");
		return;
	}
	ub_mn->mm = NULL;

	if (!ubu_dev) {
		uburma_log_err("ubu dev is null.\n");
		return;
	}

	srcu_idx = srcu_read_lock(&ubu_dev->ubc_dev_srcu);
	ubc_dev = srcu_dereference(ubu_dev->ubc_dev, &ubu_dev->ubc_dev_srcu);

	down_write(&file->ucontext_rwsem);

	uburma_cleanup_uobjs(file, UBURMA_REMOVE_CLOSE);
	if (file->ucontext) {
		uburma_log_info("Start ubcore free ucontext.\n");
		if (ubc_dev) {
			ubcore_free_ucontext(ubc_dev, file->ucontext);
			file->ucontext = NULL;
		}
	}
	up_write(&file->ucontext_rwsem);
	srcu_read_unlock(&ubu_dev->ubc_dev_srcu, srcu_idx);
	uburma_log_debug("Release uobjs and ucontext\n");
}

static const struct mmu_notifier_ops uburma_mm_notifier_ops = {
	.release = uburma_mmu_release,
};

void uburma_unregister_mmu(struct uburma_file *file)
{
	struct uburma_mn *ub_mn = &file->ub_mn;
	struct mm_struct *mm = ub_mn->mm;

	if (!mm)
		return;

	file->ub_mn.mm = NULL;
	mmu_notifier_unregister(&file->ub_mn.mn, mm);
}

int uburma_register_mmu(struct uburma_file *file)
{
	struct uburma_mn *ub_mn = &file->ub_mn;
	int ret = 0;

	ub_mn->mm = current->mm;
	ub_mn->mn.ops = &uburma_mm_notifier_ops;
	ret = mmu_notifier_register(&ub_mn->mn, current->mm);
	if (ret) {
		ub_mn->mm = NULL;
		return ret;
	}

	return 0;
}

void uburma_release_file(struct kref *ref)
{
	struct uburma_file *file = container_of(ref, struct uburma_file, ref);
	struct ubcore_device *ubc_dev;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&file->ubu_dev->ubc_dev_srcu);
	ubc_dev = srcu_dereference(file->ubu_dev->ubc_dev,
				   &file->ubu_dev->ubc_dev_srcu);
	if (ubc_dev && !ubc_dev->ops->disassociate_ucontext &&
	    ubc_dev->ops->owner != NULL)
		module_put(ubc_dev->ops->owner);

	srcu_read_unlock(&file->ubu_dev->ubc_dev_srcu, srcu_idx);

	uburma_unregister_mmu(file);
	if (atomic_dec_and_test(&file->ubu_dev->refcnt))
		complete(&file->ubu_dev->comp);

	kobject_put(&file->ubu_dev->kobj);
	if (file->fault_page)
		__free_pages(file->fault_page, 0);
	mutex_destroy(&file->umap_mutex);
	kfree(file);
}
