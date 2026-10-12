/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	The XArray as a sorted array of index/entry slots (lx/misc.h), IDR and
	IDA on top of it, and the radix tree calls amdgpu makes. Reserved
	indices hold XA_ZERO_ENTRY and read as NULL. */

#include <lx/haiku.h>


static int
xa_search(const struct xarray *xa, unsigned long index, bool *found)
{
	unsigned int low = 0, high = xa->count;
	while (low < high) {
		unsigned int mid = (low + high) / 2;
		if (xa->slots[mid].index < index)
			low = mid + 1;
		else
			high = mid;
	}
	*found = low < xa->count && xa->slots[low].index == index;
	return low;
}


static int
xa_make_room(struct xarray *xa)
{
	struct lx_xa_slot *slots;
	unsigned int capacity;
	if (xa->count < xa->capacity)
		return 0;
	capacity = xa->capacity ? xa->capacity * 2 : 16;
	slots = krealloc(xa->slots, capacity * sizeof(*slots), GFP_KERNEL);
	if (slots == NULL)
		return -ENOMEM;
	xa->slots = slots;
	xa->capacity = capacity;
	return 0;
}


static int
xa_insert_at(struct xarray *xa, unsigned int position, unsigned long index,
	void *entry)
{
	int error = xa_make_room(xa);
	if (error != 0)
		return error;
	memmove(&xa->slots[position + 1], &xa->slots[position],
		(xa->count - position) * sizeof(xa->slots[0]));
	xa->slots[position].index = index;
	xa->slots[position].entry = entry;
	xa->slots[position].marks = 0;
	xa->count++;
	return 0;
}


static void
xa_remove_at(struct xarray *xa, unsigned int position)
{
	memmove(&xa->slots[position], &xa->slots[position + 1],
		(xa->count - position - 1) * sizeof(xa->slots[0]));
	xa->count--;
}


static inline void *
xa_visible(void *entry)
{
	return entry == XA_ZERO_ENTRY ? NULL : entry;
}


void
xa_init_flags(struct xarray *xa, gfp_t flags)
{
	spin_lock_init(&xa->xa_lock);
	xa->xa_flags = flags;
	xa->slots = NULL;
	xa->count = 0;
	xa->capacity = 0;
	xa->next_cyclic = 0;
}


void
xa_destroy(struct xarray *xa)
{
	xa_lock(xa);
	kfree(xa->slots);
	xa->slots = NULL;
	xa->count = 0;
	xa->capacity = 0;
	xa_unlock(xa);
}


void *
xa_load(struct xarray *xa, unsigned long index)
{
	bool found;
	void *entry = NULL;
	int position;
	xa_lock(xa);
	position = xa_search(xa, index, &found);
	if (found)
		entry = xa_visible(xa->slots[position].entry);
	xa_unlock(xa);
	return entry;
}


void *
__xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	bool found;
	int position = xa_search(xa, index, &found);
	if (found) {
		void *old = xa->slots[position].entry;
		if (entry == NULL)
			xa_remove_at(xa, position);
		else
			xa->slots[position].entry = entry;
		return xa_visible(old);
	}
	if (entry == NULL)
		return NULL;
	if (xa_insert_at(xa, position, index, entry) != 0)
		return XA_ERROR(-ENOMEM);
	return NULL;
}


void *
xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	void *old;
	xa_lock(xa);
	old = __xa_store(xa, index, entry, gfp);
	xa_unlock(xa);
	return old;
}


void *
__xa_erase(struct xarray *xa, unsigned long index)
{
	return __xa_store(xa, index, NULL, 0);
}


void *
xa_erase(struct xarray *xa, unsigned long index)
{
	void *old;
	xa_lock(xa);
	old = __xa_erase(xa, index);
	xa_unlock(xa);
	return old;
}


void *
__xa_cmpxchg(struct xarray *xa, unsigned long index, void *old, void *entry,
	gfp_t gfp)
{
	bool found;
	int position = xa_search(xa, index, &found);
	void *current_entry = found ? xa->slots[position].entry : NULL;
	if (current_entry != old && xa_visible(current_entry) != old)
		return xa_visible(current_entry);
	if (entry == NULL) {
		if (found)
			xa_remove_at(xa, position);
	} else if (found)
		xa->slots[position].entry = entry;
	else if (xa_insert_at(xa, position, index, entry) != 0)
		return XA_ERROR(-ENOMEM);
	return xa_visible(current_entry);
}


void *
xa_cmpxchg(struct xarray *xa, unsigned long index, void *old, void *entry,
	gfp_t gfp)
{
	void *result;
	xa_lock(xa);
	result = __xa_cmpxchg(xa, index, old, entry, gfp);
	xa_unlock(xa);
	return result;
}


int
__xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	bool found;
	int position = xa_search(xa, index, &found);
	if (found)
		return -EBUSY;
	return xa_insert_at(xa, position, index,
		entry != NULL ? entry : XA_ZERO_ENTRY);
}


int
xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	int error;
	xa_lock(xa);
	error = __xa_insert(xa, index, entry, gfp);
	xa_unlock(xa);
	return error;
}


int
xa_reserve(struct xarray *xa, unsigned long index, gfp_t gfp)
{
	int error;
	xa_lock(xa);
	error = __xa_insert(xa, index, NULL, gfp);
	xa_unlock(xa);
	return error == -EBUSY ? 0 : error;
}


/* the lowest free index in [min, max] at or above start */
static int
xa_find_free(struct xarray *xa, u32 start, struct xa_limit limit, u32 *id)
{
	bool found;
	u64 candidate = max(start, limit.min);
	unsigned int position = xa_search(xa, candidate, &found);
	if ((xa->xa_flags & XA_FLAGS_ZERO_BUSY) && candidate == 0)
		candidate = 1;
	position = xa_search(xa, candidate, &found);
	while (position < xa->count && xa->slots[position].index == candidate) {
		candidate++;
		position++;
	}
	if (candidate > limit.max)
		return -EBUSY;
	*id = candidate;
	return 0;
}


int
__xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit,
	gfp_t gfp)
{
	bool found;
	int error = xa_find_free(xa, limit.min, limit, id);
	if (error != 0)
		return error;
	return xa_insert_at(xa, xa_search(xa, *id, &found), *id,
		entry != NULL ? entry : XA_ZERO_ENTRY);
}


int
xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit,
	gfp_t gfp)
{
	int error;
	xa_lock(xa);
	error = __xa_alloc(xa, id, entry, limit, gfp);
	xa_unlock(xa);
	return error;
}


int
__xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry,
	struct xa_limit limit, u32 *next, gfp_t gfp)
{
	bool found;
	int wrapped = 0;
	int error = xa_find_free(xa, *next, limit, id);
	if (error == -EBUSY) {
		error = xa_find_free(xa, limit.min, limit, id);
		wrapped = 1;
	}
	if (error != 0)
		return error;
	error = xa_insert_at(xa, xa_search(xa, *id, &found), *id,
		entry != NULL ? entry : XA_ZERO_ENTRY);
	if (error != 0)
		return error;
	*next = *id + 1;
	if (*next == 0 || *next > limit.max)
		*next = limit.min;
	return wrapped;
}


int
xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry, struct xa_limit limit,
	u32 *next, gfp_t gfp)
{
	int error;
	xa_lock(xa);
	error = __xa_alloc_cyclic(xa, id, entry, limit, next, gfp);
	xa_unlock(xa);
	return error;
}


static bool
xa_slot_matches(const struct lx_xa_slot *slot, xa_mark_t filter)
{
	if (slot->entry == XA_ZERO_ENTRY)
		return false;
	if (filter == XA_PRESENT)
		return true;
	return (slot->marks & (1U << (__force unsigned)filter)) != 0;
}


static void *
xa_find_from(struct xarray *xa, unsigned long *index, unsigned long first,
	unsigned long max, xa_mark_t filter)
{
	bool found;
	unsigned int position;
	void *entry = NULL;
	xa_lock(xa);
	for (position = xa_search(xa, first, &found); position < xa->count;
			position++) {
		struct lx_xa_slot *slot = &xa->slots[position];
		if (slot->index > max)
			break;
		if (xa_slot_matches(slot, filter)) {
			*index = slot->index;
			entry = slot->entry;
			break;
		}
	}
	xa_unlock(xa);
	return entry;
}


void *
xa_find(struct xarray *xa, unsigned long *index, unsigned long max,
	xa_mark_t filter)
{
	return xa_find_from(xa, index, *index, max, filter);
}


void *
xa_find_after(struct xarray *xa, unsigned long *index, unsigned long max,
	xa_mark_t filter)
{
	if (*index == ULONG_MAX)
		return NULL;
	return xa_find_from(xa, index, *index + 1, max, filter);
}


bool
xa_get_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	bool found, marked = false;
	int position;
	xa_lock(xa);
	position = xa_search(xa, index, &found);
	if (found)
		marked = (xa->slots[position].marks
			& (1U << (__force unsigned)mark)) != 0;
	xa_unlock(xa);
	return marked;
}


void
xa_set_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	bool found;
	int position;
	xa_lock(xa);
	position = xa_search(xa, index, &found);
	if (found)
		xa->slots[position].marks |= 1U << (__force unsigned)mark;
	xa_unlock(xa);
}


void
xa_clear_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	bool found;
	int position;
	xa_lock(xa);
	position = xa_search(xa, index, &found);
	if (found)
		xa->slots[position].marks &= ~(1U << (__force unsigned)mark);
	xa_unlock(xa);
}


/* #pragma mark - IDR */


int
idr_alloc_u32(struct idr *idr, void *ptr, u32 *nextid, unsigned long max,
	gfp_t gfp)
{
	struct xa_limit limit = XA_LIMIT(*nextid - idr->idr_base,
		max - idr->idr_base);
	u32 id;
	int error;
	if ((long)max < (long)*nextid)
		return -ENOSPC;
	error = xa_alloc(&idr->idr_rt, &id, ptr, limit, gfp);
	if (error == -EBUSY)
		return -ENOSPC;
	if (error != 0)
		return error;
	*nextid = id + idr->idr_base;
	return 0;
}


int
idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
	u32 id = start;
	int error;
	if (WARN_ON_ONCE(start < 0))
		return -EINVAL;
	error = idr_alloc_u32(idr, ptr, &id, end > 0 ? end - 1 : INT_MAX, gfp);
	return error != 0 ? error : (int)id;
}


int
idr_alloc_cyclic(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
	u32 id;
	u32 next = max_t(u32, start, idr->idr_next);
	struct xa_limit limit = XA_LIMIT(start, end > 0 ? end - 1 : INT_MAX);
	int error = xa_alloc_cyclic(&idr->idr_rt, &id, ptr, limit, &next, gfp);
	if (error < 0)
		return error == -EBUSY ? -ENOSPC : error;
	idr->idr_next = next;
	return id;
}


void *
idr_find(const struct idr *idr, unsigned long id)
{
	return xa_load((struct xarray *)&idr->idr_rt, id - idr->idr_base);
}


void *
idr_remove(struct idr *idr, unsigned long id)
{
	return xa_erase(&idr->idr_rt, id - idr->idr_base);
}


void *
idr_replace(struct idr *idr, void *ptr, unsigned long id)
{
	bool found;
	int position;
	void *old = ERR_PTR(-ENOENT);
	struct xarray *xa = &idr->idr_rt;
	xa_lock(xa);
	position = xa_search(xa, id - idr->idr_base, &found);
	if (found) {
		old = xa_visible(xa->slots[position].entry);
		xa->slots[position].entry = ptr != NULL ? ptr : XA_ZERO_ENTRY;
	}
	xa_unlock(xa);
	return old;
}


void *
idr_get_next_ul(struct idr *idr, unsigned long *nextid)
{
	unsigned long index = *nextid - idr->idr_base;
	void *entry = xa_find(&idr->idr_rt, &index, ULONG_MAX, XA_PRESENT);
	if (entry != NULL)
		*nextid = index + idr->idr_base;
	return entry;
}


void *
idr_get_next(struct idr *idr, int *nextid)
{
	unsigned long id = *nextid;
	void *entry = idr_get_next_ul(idr, &id);
	if (entry != NULL && id > INT_MAX)
		return NULL;
	if (entry != NULL)
		*nextid = id;
	return entry;
}


int
idr_for_each(const struct idr *idr, int (*fn)(int id, void *p, void *data),
	void *data)
{
	unsigned long index = 0;
	void *entry;
	xa_for_each((struct xarray *)&idr->idr_rt, index, entry) {
		int ret = fn(index + idr->idr_base, entry, data);
		if (ret != 0)
			return ret;
	}
	return 0;
}


void
idr_destroy(struct idr *idr)
{
	xa_destroy(&idr->idr_rt);
}


/* #pragma mark - IDA */


int
ida_alloc_range(struct ida *ida, unsigned int min, unsigned int max, gfp_t gfp)
{
	u32 id;
	int error;
	if ((int)min < 0)
		return -ENOSPC;
	if ((int)max < 0)
		max = INT_MAX;
	error = xa_alloc(&ida->xa, &id, XA_ZERO_ENTRY, XA_LIMIT(min, max), gfp);
	if (error == -EBUSY)
		return -ENOSPC;
	return error != 0 ? error : (int)id;
}


void
ida_free(struct ida *ida, unsigned int id)
{
	struct xarray *xa = &ida->xa;
	bool found;
	int position;
	xa_lock(xa);
	position = xa_search(xa, id, &found);
	if (found)
		xa_remove_at(xa, position);
	xa_unlock(xa);
}


void
ida_destroy(struct ida *ida)
{
	xa_destroy(&ida->xa);
}


/* #pragma mark - radix trees */


void **
lx_radix_tree_next(struct xarray *root, struct radix_tree_iter *iter, int tag)
{
	bool found;
	unsigned int position;
	void **slot = NULL;
	xa_lock(root);
	for (position = xa_search(root, iter->next_index, &found);
			position < root->count; position++) {
		struct lx_xa_slot *s = &root->slots[position];
		if (s->entry == XA_ZERO_ENTRY)
			continue;
		if (tag >= 0 && !(s->marks & (1U << tag)))
			continue;
		iter->index = s->index;
		iter->next_index = s->index + 1;
		slot = &s->entry;
		break;
	}
	xa_unlock(root);
	/* The slot may move when the array changes; the callers only read it
	   and delete through the index before asking for the next one. */
	return slot;
}


static unsigned int
radix_gang(struct xarray *root, void **results, unsigned long first_index,
	unsigned int max_items, int tag)
{
	struct radix_tree_iter iter = { .next_index = first_index };
	unsigned int count = 0;
	void **slot;
	while (count < max_items
		&& (slot = lx_radix_tree_next(root, &iter, tag)) != NULL)
		results[count++] = *slot;
	return count;
}


unsigned int
radix_tree_gang_lookup(struct xarray *root, void **results,
	unsigned long first_index, unsigned int max_items)
{
	return radix_gang(root, results, first_index, max_items, -1);
}


unsigned int
radix_tree_gang_lookup_tag(struct xarray *root, void **results,
	unsigned long first_index, unsigned int max_items, unsigned int tag)
{
	return radix_gang(root, results, first_index, max_items, tag);
}
