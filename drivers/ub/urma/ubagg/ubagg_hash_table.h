/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2025. All rights reserved.
 *
 * Description: define hash table ops
 * Author: Yan Fangfang
 * Create: 2022-08-03
 * Note:
 * History: 2022-08-03  Yan Fangfang  Add base code
 */

#ifndef UBAGG_HASH_TABLE_H
#define UBAGG_HASH_TABLE_H

#include "ubagg_types.h"

static inline void *ubagg_ht_obj(const struct ubagg_hash_table *ht,
				 const struct hlist_node *hnode)
{
	return (char *)hnode - ht->p.node_offset;
}

static inline void *ubagg_ht_key(const struct ubagg_hash_table *ht,
				 const struct hlist_node *hnode)
{
	return ((char *)hnode - ht->p.node_offset) + ht->p.key_offset;
}

/* Init ht head, not calloc hash table itself */
int ubagg_hash_table_alloc(struct ubagg_hash_table *ht,
			   struct ubagg_ht_param *p);
/* Free ht head, not release hash table itself */
void ubagg_hash_table_free(struct ubagg_hash_table *ht);

void ubagg_hash_table_free_with_cb(struct ubagg_hash_table *ht,
				   void (*free_cb)(void *));

void ubagg_hash_table_add(struct ubagg_hash_table *ht, struct hlist_node *hnode,
			  uint32_t hash);

void ubagg_hash_table_add_nolock(struct ubagg_hash_table *ht,
				 struct hlist_node *hnode, uint32_t hash);

void ubagg_hash_table_remove(struct ubagg_hash_table *ht,
			     struct hlist_node *hnode);

int ubagg_hash_table_check_remove(struct ubagg_hash_table *ht,
				  struct hlist_node *hnode);

void ubagg_hash_table_remove_nolock(struct ubagg_hash_table *ht,
				    struct hlist_node *hnode);

void *ubagg_hash_table_lookup(struct ubagg_hash_table *ht, uint32_t hash,
			      const void *key);

void *ubagg_hash_table_lookup_nolock(struct ubagg_hash_table *ht, uint32_t hash,
				     const void *key);

void *ubagg_hash_table_lookup_get(struct ubagg_hash_table *ht, uint32_t hash,
				  const void *key);

void *ubagg_hash_table_lookup_nolock_get(struct ubagg_hash_table *ht,
					 uint32_t hash, const void *key);

void *ubagg_hash_table_find_remove(struct ubagg_hash_table *ht, uint32_t hash,
				   const void *key);
/* Do not insert a new entry if an old entry with the same key exists */
int ubagg_hash_table_find_add(struct ubagg_hash_table *ht,
			      struct hlist_node *hnode, uint32_t hash);
#endif
