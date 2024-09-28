#include <linux/slab.h> // For kmalloc and kfree
#include <linux/types.h>
#include <linux/string.h>
#include <linux/jhash.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/jhash.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

int major_number;

#include "hcd_module.h"

struct list {
	void *data;
	struct list *next;
};

/* the ffs is saved in data of the first node which also functions similar a dummy node
 * the responsibility of freeing the ffs does not fall on the list functions
 */
typedef struct list *LIST;
typedef bool (*list_element_identifier)(void *element, void *additional_data);
typedef void (*free_element_func)(void *element, void *additional_data);

#define SUCCESS (0)
#define MEM_ERROR (-1)
#define FAILURE (-2)
#define NOT_FOUND (-3)
#define EXISTS (-4)
#define UNAUTHORIZED (-5)
#define ILLEGAL (-6)
#define MALFORMED_STRING (-7)
typedef int RETURN_CODE;

#define NO_PROCESS 0

struct free_func_struct {
	free_element_func func;
	void *additional_data;
};

typedef struct free_func_struct *FFS;

void list_set_free_func_struct(LIST list, FFS free_func_struct)
{
	list->data = free_func_struct;
}

void invoke_ffs(void *to_free, FFS ffs)
{
	ffs->func(to_free, ffs->additional_data);
}

void list_invoke_free(void *to_free, LIST list)
{
	FFS ffs = list->data;

	invoke_ffs(to_free, ffs);
}

void *m_malloc(size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

void m_free(void *to_free)
{
	kfree(to_free);
}

char *int_to_str(unsigned long long num, char *buffer, int buffer_size)
{
	if (!buffer) {
		return NULL;
	}
	snprintf(buffer, buffer_size, "%llu", num);
	return buffer;
}

void free_hcd_keys(hcd_keys *keys)
{
	if (!keys) {
		return;
	}
	m_free(keys->keys);
	m_free(keys);
}

/*****************************************************************************/
typedef struct mutex MUTEX;
typedef MUTEX * MUTEX_PTR;

MUTEX_PTR mutex_create(void)
{
	MUTEX_PTR mutex = (MUTEX_PTR)m_malloc(sizeof(MUTEX));

	if (!mutex) {
		return NULL;
	}
	mutex_init(mutex);
	return mutex;
}

void mutex_free(MUTEX_PTR mutex)
{
	if (!mutex) {
		return;
	}
	mutex_destroy(mutex);
	m_free(mutex);
}

struct sync {
	MUTEX_PTR resource;
	MUTEX_PTR rmutex;
	MUTEX_PTR service;
	int read_count;
};

typedef struct sync *SYNC;

SYNC sync_create(void)
{
	MUTEX_PTR resource = mutex_create();
	MUTEX_PTR rmutex = mutex_create();
	MUTEX_PTR service = mutex_create();
	SYNC sync = (SYNC)m_malloc(sizeof(struct sync));

	if (!resource || !rmutex || !service || !sync) {
		mutex_free(resource);
		mutex_free(rmutex);
		mutex_free(service);
		m_free(sync);
		return NULL;
	}
	sync->resource = resource;
	sync->rmutex = rmutex;
	sync->service = service;
	sync->read_count = 0;
	return sync;
}

void sync_free(SYNC sync)
{
	if (!sync) {
		return;
	}
	mutex_free(sync->service);
	mutex_free(sync->rmutex);
	mutex_free(sync->resource);
	m_free(sync);
}

void sync_before_read(SYNC sync)
{
	mutex_lock(sync->service);
	mutex_lock(sync->rmutex);
	sync->read_count++;
	if (sync->read_count == 1) {
		mutex_lock(sync->resource);
	}
	mutex_unlock(sync->service);
	mutex_unlock(sync->rmutex);
}

void sync_after_read(SYNC sync)
{
	mutex_lock(sync->rmutex);
	sync->read_count--;
	if (sync->read_count == 0) {
		mutex_unlock(sync->resource);
	}
	mutex_unlock(sync->rmutex);
}

void sync_before_write(SYNC sync)
{
	mutex_lock(sync->service);
	mutex_lock(sync->resource);
	mutex_unlock(sync->service);
}

void sync_after_write(SYNC sync)
{
	mutex_unlock(sync->resource);
}

/*****************************************************************************/

LIST list_create(FFS ffs)
{
	LIST created = (LIST)m_malloc(sizeof(struct list));

	if (!created) {
		return NULL;
	}
	created->next = NULL;
	list_set_free_func_struct(created, ffs);
	return created;
}

RETURN_CODE m_list_add(LIST list, void *element)
{
	LIST prev_head = list->next;
	LIST created = (LIST)m_malloc(sizeof(struct list));

	if (!created) {
		return MEM_ERROR;
	}
	created->data = element;
	created->next = prev_head;
	list->next = created;
	return SUCCESS;
}

RETURN_CODE list_delete(LIST list, list_element_identifier func,
			void *func_data)
{
	for (LIST cur = list; cur->next; cur = cur->next) {
		if (func(cur->next->data, func_data)) {
			LIST new_next = cur->next->next;

			list_invoke_free(cur->next->data, list);
			m_free(cur->next);
			cur->next = new_next;
			return SUCCESS;
		}
	}
	return NOT_FOUND;
}

#define FOREACH_LIST_NODE(cur, list) \
	for (LIST(cur) = (list)->next; (cur) != NULL; (cur) = (cur)->next)

void *list_node_get_data(LIST node)
{
	return node->data;
}

void list_node_set_data(LIST node, void *data)
{
	node->data = data;
}

void *list_get(LIST list, list_element_identifier func, void *additional_data)
{
	for (LIST cur = list->next; cur; cur = cur->next) {
		if (func(cur->data, additional_data)) {
			return cur->data;
		}
	}
	return NULL;
}

bool find_any_element_in_list(void *element, void *additional_data)
{
	return true;
}

void list_free(LIST list)
{
	if (!list) {
		return;
	}
	while (list->next) {
		list_delete(list, find_any_element_in_list, NULL);
	}
	m_free(list);
}

RETURN_CODE read_string_from_user(char *ptr, int max_size, char **result)
{
	char *cur_buffer = (char *)m_malloc(sizeof(char) * 1);
	char *prev_buffer;

	if (!cur_buffer) {
		return MEM_ERROR;
	}
	cur_buffer[0] = '\0';
	for (int i = 0; i < max_size; i++) {
		prev_buffer = cur_buffer;
		cur_buffer = (char *)m_malloc(sizeof(char) * (i + 2));
		if (!cur_buffer) {
			m_free(prev_buffer);
			return MEM_ERROR;
		}
		strcpy(cur_buffer, prev_buffer);
		cur_buffer[i + 1] = '\0';
		m_free(prev_buffer);
		if (copy_from_user(cur_buffer + i, ptr + i, 1) == 0) {
			if (cur_buffer[i] == '\0') {
				*result = cur_buffer;
				return SUCCESS;
			}
		} else {
			m_free(cur_buffer);
			return ILLEGAL;
		}
	}
	m_free(cur_buffer);
	return MALFORMED_STRING;
}

int hash_func(char *str, int max_output)
{
	int result = jhash(str, strlen(str), 0);

	if (result < 0) {
		result = -result;
	}
	result = result % max_output;
	return result;
}

struct ht {
	LIST *array;
	int total_size;
	int cur_size;
	FFS ffs;
	SYNC sync;
};

/* the responsibility of freeing the val ffs does not fall on the ht functions
 * the free function of ht frees the built ffs without freeing the given val ffs
 */
typedef struct ht *HT;

struct dict_node {
	char *key;
	void *val;
};

typedef struct dict_node *DICT_NODE;

void *ht_get_primitive(char *key, LIST *array, int size);

void free_dict_node_func(void *to_free, void *ffs)
{
	DICT_NODE _to_free = (DICT_NODE)to_free;
	FFS _ffs = (FFS)ffs;

	if (!_to_free) {
		return;
	}
	m_free(_to_free->key);
	invoke_ffs(_to_free->val, _ffs);
	m_free(_to_free);
}

bool is_node_with_key(DICT_NODE node, void *key_ptr)
{
	DICT_NODE _node = (DICT_NODE)node;
	int *_key_ptr = (int *)key_ptr;
	char *key = (char *)_key_ptr;

	return strcmp(_node->key, key) == 0;
}

#define HT_INIT_SIZE 8

void ht_free_array(LIST *array, int size, HT ht)
{
	if (!array) {
		return;
	}
	for (int i = 0; i < size; i++) {
		if (!array[i]) {
			continue;
		}
		list_free(array[i]);
	}
	m_free(array);
}

FFS ht_create_ffs(FFS val_ffs)
{
	FFS ffs = m_malloc(sizeof(struct free_func_struct));

	if (!ffs) {
		return NULL;
	}
	ffs->func = free_dict_node_func;
	ffs->additional_data = val_ffs;
	return ffs;
}

LIST *ht_create_array(int size, HT ht)
{
	LIST *array = m_malloc(sizeof(void *) * size);

	if (!array) {
		return NULL;
	}
	for (int i = 0; i < size; i++) {
		array[i] = NULL;
	}
	for (int i = 0; i < size; i++) {
		array[i] = list_create(ht->ffs);
		if (!array[i]) {
			ht_free_array(array, size, ht);
			return NULL;
		}
	}
	return array;
}

void ht_free_ffs(FFS ffs)
{
	m_free(ffs);
}

HT ht_create(FFS free_val_ffs)
{
	HT ht = (HT)m_malloc(sizeof(struct ht));

	if (!ht) {
		return NULL;
	}
	ht->ffs = ht_create_ffs(free_val_ffs);
	if (!ht->ffs) {
		m_free(ht);
		return NULL;
	}
	LIST *array = ht_create_array(HT_INIT_SIZE, ht);

	if (!array) {
		m_free(ht->ffs);
		m_free(ht);
		return NULL;
	}
	SYNC sync = sync_create();

	if (!sync) {
		ht_free_array(array, 0, ht);
		m_free(ht->ffs);
		m_free(ht);
		return NULL;
	}
	ht->sync = sync;
	ht->array = array;
	ht->cur_size = 0;
	ht->total_size = HT_INIT_SIZE;
	return ht;
}

int ht_get_list_idx(char *key, LIST *array, int size)
{
	return hash_func(key, size);
}

// the responsibility of freeing the node in the event of an EXISTS error does not fall on this function
RETURN_CODE ht_primitive_add_node(DICT_NODE node, LIST *array, int size)
{
	void *duplicate = ht_get_primitive(node->key, array, size);

	if (duplicate) {
		return EXISTS;
	}
	int array_index = ht_get_list_idx(node->key, array, size);
	LIST list = array[array_index];

	return m_list_add(list, node);
}

#define HT_ARRAY_FOREACH_LIST_NODE(cur, array, size, brk, proc)       \
	do {                                                          \
		bool(brk) = false;                                    \
		for (int lidx = 0; (lidx < (size)) && !brk; lidx++) { \
			FOREACH_LIST_NODE(cur, (array)[lidx])         \
			{                                             \
				({ proc });                           \
				if ((brk)) {                          \
					break;                        \
				}                                     \
			}                                             \
		}                                                     \
	} while (false)

#define HT_ARRAY_FOREACH(cur, array, size, brk, proc)                          \
	HT_ARRAY_FOREACH_LIST_NODE(cur_list_node, array, size, brk, {          \
		DICT_NODE(cur) = (DICT_NODE)list_node_get_data(cur_list_node); \
		({ proc });                                                    \
	})


#define HT_FOREACH(cur, ht, brk, proc) \
	HT_ARRAY_FOREACH(cur, ht->array, ht->total_size, brk, proc)


void ht_array_shallow_empty(LIST *ht_array, int size)
{
	HT_ARRAY_FOREACH_LIST_NODE(node, ht_array, size, brk,
				   { list_node_set_data(node, NULL); });
}

RETURN_CODE ht_migrate_to_new_array(LIST *ht_old_array, int old_size,
				    LIST *ht_new_array, int new_size)
{
	RETURN_CODE res = SUCCESS;

	HT_ARRAY_FOREACH(dict_node, ht_old_array, old_size, brk1, {
		res = ht_primitive_add_node(dict_node, ht_new_array, new_size);
		if (res != SUCCESS) {
			brk1 = true;
		}
	});
	if (res != SUCCESS) {
		ht_array_shallow_empty(ht_new_array, new_size);
		return res;
	} else {
		ht_array_shallow_empty(ht_old_array, old_size);
		return SUCCESS;
	}
}

RETURN_CODE ht_resize(HT ht, int new_size)
{
	LIST *new_array = ht_create_array(new_size, ht);

	if (!new_array) {
		return MEM_ERROR;
	}
	RETURN_CODE res = ht_migrate_to_new_array(ht->array, ht->total_size,
						  new_array, new_size);
	if (res != SUCCESS) {
		ht_free_array(new_array, new_size, ht);
		return res;
	}
	ht_free_array(ht->array, ht->total_size, ht);
	ht->array = new_array;
	ht->total_size = new_size;
	return SUCCESS;
}

RETURN_CODE ht_shrink(HT ht)
{
	int new_size = ht->total_size / 2;

	return ht_resize(ht, new_size);
}

RETURN_CODE ht_expand(HT ht)
{
	int new_size = ht->total_size * 2;

	return ht_resize(ht, new_size);
}

RETURN_CODE ht_resize_if_needed(HT ht)
{
	if (ht->cur_size * 2 < ht->total_size &&
	    ht->total_size > HT_INIT_SIZE) {
		return ht_shrink(ht);
	} else if (ht->cur_size > ht->total_size * 2) {
		return ht_expand(ht);
	}
	return SUCCESS;
}

RETURN_CODE ht_remove_node(char *key, HT ht);

// the responsibility of freeing the node in the event of an EXISTS error does not fall on this function
RETURN_CODE ht_add_node(DICT_NODE node, HT ht)
{
	RETURN_CODE res =
		ht_primitive_add_node(node, ht->array, ht->total_size);
	if (res == EXISTS) {
		ht_remove_node(node->key, ht);
		res = ht_primitive_add_node(node, ht->array, ht->total_size);
	}
	if (res != SUCCESS) {
		return res;
	}
	ht->cur_size++;
	res = ht_resize_if_needed(ht);
	return res;
}

/* the responsibility of freeing the value parameter in the event of an EXISTS error does not fall on this function
 * the dict_node created is freed without freeing the given value parameter
 */
RETURN_CODE ht_copy_key_and_add_node(char *key, void *value, HT ht)
{
	DICT_NODE node = (DICT_NODE)m_malloc(sizeof(struct dict_node));

	if (!node) {
		return MEM_ERROR;
	}
	char *copied_key = (char *)m_malloc(sizeof(char) * (strlen(key) + 1));

	if (!copied_key) {
		m_free(node);
		return MEM_ERROR;
	}
	strcpy(copied_key, key);
	node->key = copied_key;
	node->val = value;
	RETURN_CODE res = ht_add_node(node, ht);

	if (res != SUCCESS) {
		node->val = NULL;
		invoke_ffs(node, ht->ffs);
	}
	return res;
}

bool ht_is_key_identical(void *node, void *key)
{
	char *_key = (char *)key;
	DICT_NODE _node = (DICT_NODE)node;

	return strcmp(_node->key, _key) == 0;
}

void *ht_get_primitive(char *key, LIST *array, int size)
{
	int idx = ht_get_list_idx(key, array, size);
	DICT_NODE node =
		(DICT_NODE)list_get(array[idx], ht_is_key_identical, key);
	if (!node) {
		return NULL;
	} else {
		return node->val;
	}
}

void *ht_get(char *key, HT ht)
{
	return ht_get_primitive(key, ht->array, ht->total_size);
}

RETURN_CODE ht_remove_node(char *key, HT ht)
{
	int idx = ht_get_list_idx(key, ht->array, ht->total_size);
	RETURN_CODE res = (RETURN_CODE)list_delete(ht->array[idx],
						   ht_is_key_identical, key);
	if (res != SUCCESS) {
		return res;
	}
	ht->cur_size--;
	res = ht_resize_if_needed(ht);
	return res;
}

void ht_free(HT ht)
{
	if (!ht) {
		return;
	}
	ht_free_array(ht->array, ht->total_size, ht);
	ht_free_ffs(ht->ffs);
	sync_free(ht->sync);
	m_free(ht);
}

/***********************************************************/
struct m_val {
	void *val;
	int size;
};

typedef struct m_val *MVAL;

MVAL create_m_val_struct(void *val, int size)
{
	MVAL m_val = (MVAL)m_malloc(sizeof(struct m_val));

	if (!m_val) {
		return NULL;
	}

	m_val->val = val;
	m_val->size = size;
	return m_val;
}

void m_val_free(void *m_val, void *additional_data)
{
	if (!m_val) {
		return;
	}
	MVAL _m_val = (MVAL)m_val;

	m_free(_m_val->val);
	m_free(_m_val);
}

void *copy_val_in_mval(MVAL mval)
{
	void *copy_val = m_malloc(sizeof(char) * mval->size);

	if (!copy_val) {
		return NULL;
	}
	char *_copy_to = (char *)copy_val;
	char *_copy_from = (char *)(mval->val);

	for (int i = 0; i < mval->size; i++) {
		_copy_to[i] = _copy_from[i];
	}
	return copy_val;
}

MVAL copy_mval(MVAL mval)
{
	MVAL copy = (MVAL)m_malloc(sizeof(struct m_val));
	void *copy_val = copy_val_in_mval(mval);

	if (!copy || !copy_val) {
		m_free(copy);
		m_free(mval);
		return NULL;
	}
	copy->size = mval->size;
	copy->val = copy_val;
	return copy;
}

FFS create_m_val_ffs(void)
{
	FFS ffs = m_malloc(sizeof(struct free_func_struct));

	if (!ffs) {
		return NULL;
	}
	ffs->additional_data = NULL;
	ffs->func = m_val_free;
	return ffs;
}

void free_m_val_ffs(FFS ffs)
{
	m_free(ffs);
}

struct room {
	int cnt;
	HT ht;
	int protected_for_process;
};

typedef struct room *ROOM;

struct ffs_s {
	FFS m_val_ffs;
	FFS room_ffs;
	FFS process_ffs;
};

struct ffs_s ffs_s;

ROOM room_create(int protected_for_process)
{
	ROOM room = (ROOM)m_malloc(sizeof(struct room));

	if (!room) {
		return NULL;
	}
	room->cnt = 1;
	room->ht = ht_create(ffs_s.m_val_ffs);
	room->protected_for_process = protected_for_process;
	if (!room->ht) {
		m_free(room);
		return NULL;
	}
	return room;
}

void room_free(ROOM room)
{
	ht_free(room->ht);
	m_free(room);
}

struct process {
	char *room_key;
	int unique_id;
};

typedef struct process *PROCESS;

bool is_room_write_allowed(PROCESS process, ROOM room)
{
	if (room->protected_for_process == NO_PROCESS) {
		return true;
	}
	return process->unique_id == room->protected_for_process;
}

void room_enter(ROOM room)
{
	sync_before_write(room->ht->sync);
	room->cnt++;
	sync_after_write(room->ht->sync);
}

void room_leave(ROOM room)
{
	sync_before_write(room->ht->sync);
	room->cnt--;
	sync_after_write(room->ht->sync);
}

void *room_get_and_lock(char *key, ROOM room)
{
	sync_before_read(room->ht->sync);
	void *res = ht_get(key, room->ht);
	return res;
}

void room_release_after_get(ROOM room)
{
	sync_after_read(room->ht->sync);
}

RETURN_CODE room_add_pair(char *key, void *value, ROOM room)
{
	sync_before_write(room->ht->sync);
	RETURN_CODE res = ht_copy_key_and_add_node(key, value, room->ht);

	sync_after_write(room->ht->sync);
	return res;
}

RETURN_CODE room_remove_pair(char *key, ROOM room)
{
	sync_before_write(room->ht->sync);
	RETURN_CODE res = ht_remove_node(key, room->ht);

	sync_after_write(room->ht->sync);
	return res;
}

int room_dump_keys(ROOM room, hcd_keys *keys)
{
	sync_before_read(room->ht->sync);
	int cnt = 0;

	HT_FOREACH(cur, room->ht, brk, {
		strcpy(keys->keys[cnt], cur->key);
		cnt++;
		if (cnt == keys->count) {
			brk = true;
		}
	});
	sync_after_read(room->ht->sync);
	return cnt;
}

int room_get_key_count(ROOM room)
{
	sync_before_read(room->ht->sync);
	int cnt = room->ht->cur_size;

	sync_after_read(room->ht->sync);
	return cnt;
}

struct rooms {
	HT rooms_ht;
	HT processes_ht;
	SYNC sync;
};

typedef struct rooms *ROOMS;

char *copy_str(char *str)
{
	char *copied = (char *)m_malloc(sizeof(char) * (strlen(str) + 1));

	if (!copied) {
		return NULL;
	}
	strcpy(copied, str);
	return copied;
}

int process_unique_counter = 1;

PROCESS process_create(char *key)
{
	PROCESS process = (PROCESS)m_malloc(sizeof(struct process));
	char *room_key = copy_str(key);

	if (!process || !room_key) {
		m_free(process);
		m_free(room_key);
		return NULL;
	}
	process->room_key = room_key;
	process->unique_id = process_unique_counter;
	process_unique_counter++;
	return process;
}

void process_free(PROCESS process)
{
	if (!process) {
		return;
	}
	m_free(process->room_key);
	m_free(process);
}

char *process_get_room_key(PROCESS process)
{
	return process->room_key;
};

RETURN_CODE process_change_room_key(char *new_key, PROCESS process)
{
	char *key = copy_str(new_key);

	if (!key) {
		return MEM_ERROR;
	}
	m_free(process->room_key);
	process->room_key = key;
	return SUCCESS;
}

void process_free_func(void *to_free, void *additional_data)
{
	PROCESS process = (PROCESS)to_free;

	process_free(process);
}

void free_process_ffs(FFS ffs)
{
	m_free(ffs);
}

FFS create_process_ffs(void)
{
	FFS ffs = (FFS)m_malloc(sizeof(struct free_func_struct));

	if (!ffs) {
		return NULL;
	}
	ffs->func = process_free_func;
	ffs->additional_data = NULL;
	return ffs;
}

void room_free_func(void *room, void *additional_data)
{
	ROOM _room = (ROOM)room;

	room_free(_room);
}

FFS create_room_ffs(FFS m_val_ffs)
{
	FFS ffs = (FFS)m_malloc(sizeof(struct free_func_struct));

	if (!ffs) {
		return NULL;
	}
	ffs->func = room_free_func;
	ffs->additional_data = m_val_ffs;
	return ffs;
}

void free_room_ffs(FFS ffs)
{
	m_free(ffs);
}

void clean_ffs_s(void)
{
	free_m_val_ffs(ffs_s.m_val_ffs);
	free_room_ffs(ffs_s.room_ffs);
	free_process_ffs(ffs_s.process_ffs);
}

RETURN_CODE init_ffs_s(void)
{
	ffs_s.m_val_ffs = create_m_val_ffs();
	ffs_s.room_ffs = create_room_ffs(ffs_s.m_val_ffs);
	ffs_s.process_ffs = create_process_ffs();
	if (!ffs_s.m_val_ffs || !ffs_s.room_ffs || !ffs_s.process_ffs) {
		clean_ffs_s();
		return MEM_ERROR;
	}
	return SUCCESS;
}

void rooms_free(ROOMS rooms)
{
	if (!rooms) {
		return;
	}
	ht_free(rooms->rooms_ht);
	ht_free(rooms->processes_ht);
	sync_free(rooms->sync);
	m_free(rooms);
}

ROOMS rooms_create(void)
{
	ROOMS rooms = (ROOMS)m_malloc(sizeof(struct rooms));

	if (!rooms) {
		return NULL;
	}
	rooms->rooms_ht = ht_create(ffs_s.room_ffs);
	rooms->processes_ht = ht_create(ffs_s.process_ffs);
	rooms->sync = sync_create();
	if (!rooms->rooms_ht || !rooms->processes_ht || !rooms->sync) {
		rooms_free(rooms);
		return NULL;
	}
	return rooms;
}

ROOMS rooms;

RETURN_CODE rooms_init(void)
{
	rooms = rooms_create();
	if (!rooms) {
		return MEM_ERROR;
	}
	return SUCCESS;
}

#define INT2STR_BUFFER_SIZE (HCD_KEYSIZE)

char *get_anonymous_key(void *process_id)
{
	char *buffer = (char *)m_malloc(sizeof(char) * INT2STR_BUFFER_SIZE);

	if (!buffer) {
		return NULL;
	}
	buffer[0] = '\n';
	int_to_str((unsigned long long)process_id, buffer + 1,
		   INT2STR_BUFFER_SIZE - 1);
	return buffer;
}

RETURN_CODE rooms_free_room_if_empty(ROOM room, char *key)
{
	if (room->cnt == 0) {
		return ht_remove_node(key, rooms->rooms_ht);
	}
	return SUCCESS;
}

RETURN_CODE rooms_unprotected_get_process(void *process_id,
					  PROCESS *process_ptr)
{
	char *process_key = get_anonymous_key(process_id);

	if (!process_key) {
		return MEM_ERROR;
	}
	PROCESS process = ht_get(process_key, rooms->processes_ht);

	m_free(process_key);
	if (!process) {
		return NOT_FOUND;
	}
	*process_ptr = process;
	return SUCCESS;
}

//you should not change or free the room ptr
RETURN_CODE rooms_unprotected_get_room_by_key(char *room_key, ROOM *room_ptr)
{
	ROOM room = ht_get(room_key, rooms->rooms_ht);

	if (!room) {
		return NOT_FOUND;
	}
	*room_ptr = room;
	return SUCCESS;
}

//you should not change or free the room and key ptrs
RETURN_CODE rooms_unprotected_get_room(void *process_id, ROOM *room_ptr,
				       char **room_key_ptr)
{
	PROCESS process;
	RETURN_CODE res = rooms_unprotected_get_process(process_id, &process);

	if (res != SUCCESS) {
		return res;
	}
	ROOM room;

	res = rooms_unprotected_get_room_by_key(process_get_room_key(process),
						&room);
	if (res != SUCCESS) {
		return res;
	}
	*room_ptr = room;
	if (room_key_ptr) {
		char *copied_key = copy_str(process_get_room_key(process));

		if (!copied_key) {
			return MEM_ERROR;
		}
		*room_key_ptr = copied_key;
	}
	return SUCCESS;
}

RETURN_CODE rooms_unprotected_remove_process_and_exit_room(void *process_id)
{
	char *process_key = get_anonymous_key(process_id);

	if (!process_key) {
		return MEM_ERROR;
	}
	ROOM room;
	char *room_key;
	RETURN_CODE res =
		rooms_unprotected_get_room(process_id, &room, &room_key);
	if (res != SUCCESS) {
		m_free(process_key);
		return MEM_ERROR;
	}
	room_leave(room);
	RETURN_CODE res1 = rooms_free_room_if_empty(room, room_key);
	RETURN_CODE res2 = ht_remove_node(process_key, rooms->processes_ht);

	m_free(process_key);
	if (res1 != SUCCESS || res2 != SUCCESS) {
		m_free(process_key);
		m_free(room_key);
		return MEM_ERROR;
	}
	m_free(room_key);
	return SUCCESS;
}

RETURN_CODE rooms_unprotected_get_value(char *key, void *process_id,
					MVAL *val_ptr)
{
	ROOM room;
	RETURN_CODE res = rooms_unprotected_get_room(process_id, &room, NULL);

	if (res != SUCCESS) {
		return res;
	}
	MVAL val = room_get_and_lock(key, room);

	if (!val) {
		room_release_after_get(room);
		return NOT_FOUND;
	}
	MVAL copied_val = copy_mval(val);

	if (!copied_val) {
		room_release_after_get(room);
		return MEM_ERROR;
	}

	room_release_after_get(room);

	*val_ptr = copied_val;
	return SUCCESS;
}

bool rooms_unprotected_does_room_exist(char *room_key)
{
	ROOM room;
	RETURN_CODE res = rooms_unprotected_get_room_by_key(room_key, &room);
	return res == SUCCESS;
}

bool is_room_name_valid(char *name)
{
    int len = strlen(name);

    for (int i = 0; i < len; i++){
        if (name[i] == '\n'){
            return false;
        }
    }
    return true;
}

RETURN_CODE rooms_unprotected_change_room(char *new_room_key, void *process_id,
					  bool create_flag, bool is_protected)
{
	bool does_room_exist = rooms_unprotected_does_room_exist(new_room_key);

	if (create_flag && does_room_exist) {
		return EXISTS;
	}
	if (!create_flag && !does_room_exist) {
		return NOT_FOUND;
	}
    if(create_flag && !is_room_name_valid(new_room_key)){
        return MALFORMED_STRING;
    }
	PROCESS process;
	RETURN_CODE res = rooms_unprotected_get_process(process_id, &process);

	if (res != SUCCESS) {
		return res;
	}
	ROOM old_room;
	char *old_room_key;

	res = rooms_unprotected_get_room(process_id, &old_room, &old_room_key);
	if (res != SUCCESS) {
		return res;
	}
	ROOM new_room;

	if (create_flag) {
		if (is_protected) {
			new_room = room_create(process->unique_id);
		} else {
			new_room = room_create(NO_PROCESS);
		}
		if (!new_room) {
			m_free(old_room_key);
			return MEM_ERROR;
		}
		res = ht_copy_key_and_add_node(new_room_key, new_room,
					       rooms->rooms_ht);
		if (res != SUCCESS) {
			m_free(old_room_key);
			return MEM_ERROR;
		}
	} else {
		res = rooms_unprotected_get_room_by_key(new_room_key,
							&new_room);
		if (res != SUCCESS) {
			m_free(old_room_key);
			return res;
		}
	}

	res = process_change_room_key(new_room_key, process);
	if (res != SUCCESS) {
		m_free(old_room_key);
		return res;
	}
	if (!create_flag) {
		room_enter(new_room);
	}
	room_leave(old_room);
	res = rooms_free_room_if_empty(old_room, old_room_key);
	m_free(old_room_key);
	return res;
}

RETURN_CODE rooms_unprotected_set_value(char *key, MVAL val, void *process_id)
{
	ROOM room;
	PROCESS process;
	RETURN_CODE res = rooms_unprotected_get_process(process_id, &process);
	RETURN_CODE res2 = rooms_unprotected_get_room(process_id, &room, NULL);

	if (res != SUCCESS) {
		return res;
	}
	if (res2 != SUCCESS) {
		return res;
	}
	if (!is_room_write_allowed(process, room)) {
		return UNAUTHORIZED;
	}
	MVAL copied_val = copy_mval(val);

	if (!copied_val) {
		return MEM_ERROR;
	}
	res = room_add_pair(key, copied_val, room);
	if (res != SUCCESS) {
		m_val_free(copied_val, NULL);
	}
	return res;
}

RETURN_CODE rooms_unprotected_delete_pair(char *key, void *process_id)
{
	ROOM room;
	PROCESS process;

	RETURN_CODE res = rooms_unprotected_get_room(process_id, &room, NULL);
	RETURN_CODE res2 = rooms_unprotected_get_process(process_id, &process);

	if (res != SUCCESS) {
		return res;
	}
	if (res2 != SUCCESS) {
		return res2;
	}
	if (!is_room_write_allowed(process, room)) {
		return UNAUTHORIZED;
	}

	res = room_remove_pair(key, room);
	return res;
}

RETURN_CODE rooms_unprotected_get_key_count(void *process_id,
					    int *key_count_ptr)
{
	ROOM room;
	RETURN_CODE res = rooms_unprotected_get_room(process_id, &room, NULL);

	if (res != SUCCESS) {
		return res;
	}
	*key_count_ptr = room_get_key_count(room);
	return SUCCESS;
}

RETURN_CODE rooms_unprotected_dump_keys(void *entity_id, hcd_keys *keys,
					int *cnt)
{
	ROOM room;
	RETURN_CODE res = rooms_unprotected_get_room(entity_id, &room, NULL);

	if (res != SUCCESS) {
		return res;
	}
	*cnt = room_dump_keys(room, keys);
	return SUCCESS;
}

RETURN_CODE rooms_unprotected_create_process(void *process_id)
{
	ROOM room = room_create(NO_PROCESS);

	if (!room) {
		return MEM_ERROR;
	}
	char *key = get_anonymous_key(process_id);

	if (!key) {
		m_free(room);
		return MEM_ERROR;
	}
	PROCESS process = process_create(key);

	if (!process) {
		room_free(room);
		m_free(key);
		return MEM_ERROR;
	}

	RETURN_CODE res = ht_copy_key_and_add_node(key, room, rooms->rooms_ht);

	if (res != SUCCESS) {
		room_free(room);
		process_free(process);
		m_free(key);
		return MEM_ERROR;
	}
	res = ht_copy_key_and_add_node(key, process, rooms->processes_ht);
	if (res != SUCCESS) {
		ht_remove_node(key, rooms->rooms_ht);
		process_free(process);
		m_free(key);
		return MEM_ERROR;
	}

	m_free(key);
	return SUCCESS;
}

RETURN_CODE rooms_entity_enter(void *entity_id)
{
	sync_before_write(rooms->sync);
	RETURN_CODE res = rooms_unprotected_create_process(entity_id);

	sync_after_write(rooms->sync);
	return res;
}

RETURN_CODE rooms_entity_exit(void *entity_id)
{
	sync_before_write(rooms->sync);
	RETURN_CODE res =
		rooms_unprotected_remove_process_and_exit_room(entity_id);
	sync_after_write(rooms->sync);
	return res;
}

RETURN_CODE rooms_change_room(char *new_room_key, void *entity_id)
{
	sync_before_write(rooms->sync);
	RETURN_CODE res = rooms_unprotected_change_room(new_room_key, entity_id,
							false, false);
	sync_after_write(rooms->sync);
	return res;
}

RETURN_CODE rooms_create_room_and_move(char *new_room_key, bool is_protected,
				       void *entity_id)
{
	sync_before_write(rooms->sync);
	RETURN_CODE res = rooms_unprotected_change_room(new_room_key, entity_id,
							true, is_protected);
	sync_after_write(rooms->sync);
	return res;
}

RETURN_CODE rooms_add_pair(char *key, MVAL val, void *process_id)
{
	sync_before_read(rooms->sync);
	RETURN_CODE res = rooms_unprotected_set_value(key, val, process_id);

	sync_after_read(rooms->sync);
	return res;
}

RETURN_CODE rooms_delete_pair(char *key, void *process_id)
{
	sync_before_read(rooms->sync);
	RETURN_CODE res = rooms_unprotected_delete_pair(key, process_id);

	sync_after_read(rooms->sync);
	return res;
}

//freeing the returned val is the responsibility of the caller
RETURN_CODE rooms_get_val(char *key, MVAL *val, void *process_id)
{
	sync_before_read(rooms->sync);
	RETURN_CODE res = rooms_unprotected_get_value(key, process_id, val);

	sync_after_read(rooms->sync);
	return res;
}

RETURN_CODE rooms_dump_keys(void *entity_id, hcd_keys *keys, int *cnt)
{
	sync_before_read(rooms->sync);
	RETURN_CODE res = rooms_unprotected_dump_keys(entity_id, keys, cnt);

	sync_after_read(rooms->sync);
	return res;
}

RETURN_CODE rooms_get_key_count(void *process_id, int *key_count_ptr)
{
	sync_before_read(rooms->sync);
	RETURN_CODE res =
		rooms_unprotected_get_key_count(process_id, key_count_ptr);
	sync_after_read(rooms->sync);
	return res;
}

RETURN_CODE init(void)
{
	RETURN_CODE res = init_ffs_s();

	if (res != SUCCESS) {
		return res;
	}
	res = rooms_init();
	if (res != SUCCESS) {
		clean_ffs_s();
		return res;
	}
	return SUCCESS;
}

void clean(void)
{
	rooms_free(rooms);
	clean_ffs_s();
}

/***********************************************************/
//module api

#define DEVICE_NAME "kdlp-character-device"

// Function prototypes
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char __user *, size_t,
			    loff_t *);
static long device_ioctl(struct file *, unsigned int, unsigned long);
static loff_t device_llseek(struct file *file, loff_t offset, int whence);

// File operations structure
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = device_read,
	.write = device_write,
	.llseek = device_llseek,
	.unlocked_ioctl = device_ioctl,
	.open = device_open,
	.release = device_release,
};

int translate_error_code(RETURN_CODE code, bool is_ioctl)
{
	if (code == MEM_ERROR) {
		return -ENOMEM;
	} else if (code == NOT_FOUND) {
		if (is_ioctl) {
			return -ENOENT;
		} else {
			return -EINVAL;
		}
	} else if (code == ILLEGAL) {
		return -EFAULT;
	} else if (code == UNAUTHORIZED) {
		return -EPERM;
	} else if (code == EXISTS) {
		return -EEXIST;
	} else if (code == MALFORMED_STRING) {
		return -EINVAL;
	} else if (code == SUCCESS) {
		return 0;
	}
	return 0;
}

int translate_error_code_for_ioctl(RETURN_CODE code)
{
	return translate_error_code(code, true);
}

//intended for every call that is not ioctl
int translate_error_code_regular(RETURN_CODE code)
{
	return translate_error_code(code, false);
}

RETURN_CODE copy_val_to_user(MVAL mval, void *user_val_ptr)
{
	long res = copy_to_user(user_val_ptr, mval->val, mval->size);

	if (res > 0) {
		return ILLEGAL;
	}
	return SUCCESS;
}

hcd_key *get_hcd_pair_key_pointer(hcd_pair *ptr)
{
	hcd_pair pair;
	unsigned long long diff =
		(unsigned long long)pair.key - (unsigned long long)&pair;
	return (hcd_key *)ptr + diff;
}

void *get_hcd_pair_void_ptr_pointer(hcd_pair *ptr)
{
	hcd_pair pair;
	unsigned long long diff =
		(unsigned long long)&pair.value - (unsigned long long)&pair;
	return (void *)ptr + diff;
};

bool is_string_legal(char *str, int max_size)
{
	for (int i = 0; i < max_size; i++) {
		if (str[i] == '\0') {
			return true;
		}
	}
	return false;
}

RETURN_CODE read_hcd_pair_key(hcd_pair *pair, char **key_ptr)
{
	hcd_pair copy;
	long res1 = copy_from_user(&copy, pair, sizeof(hcd_pair));

	if (res1 > 0) {
		return ILLEGAL;
	}
	if (!is_string_legal(copy.key, HCD_KEYSIZE)) {
		return MALFORMED_STRING;
	}

	char *key = m_malloc(HCD_KEYSIZE);

	if (!key) {
		return MEM_ERROR;
	}
	strcpy(key, copy.key);
	*key_ptr = key;
	return SUCCESS;
}

RETURN_CODE read_hcd_pair_val(hcd_pair *pair, int val_size, MVAL *mval_ptr)
{
	hcd_pair copy;
	long res = copy_from_user(&copy, pair, sizeof(hcd_pair));

	if (res > 0) {
		return ILLEGAL;
	}
	void *val = m_malloc(val_size);

	if (!val) {
		return MEM_ERROR;
	}
	res = copy_from_user(val, copy.value, val_size);
	if (res > 0) {
		m_free(val);
		return ILLEGAL;
	}
	MVAL mval = create_m_val_struct(val, val_size);

	if (!mval) {
		m_free(val);
		return MEM_ERROR;
	}
	*mval_ptr = mval;
	return SUCCESS;
}

RETURN_CODE read_hcd_pair_for_write(hcd_pair *pair, int val_size,
				    MVAL *mval_ptr, char **key_ptr)
{
	RETURN_CODE res = read_hcd_pair_key(pair, key_ptr);

	if (res != SUCCESS) {
		return res;
	}

	res = read_hcd_pair_val(pair, val_size, mval_ptr);
	if (res != SUCCESS) {
		m_free(*key_ptr);
		return res;
	}

	return SUCCESS;
}

RETURN_CODE read_hcd_pair_for_read(hcd_pair *pair, char **key_ptr)
{
	return read_hcd_pair_key(pair, key_ptr);
}

RETURN_CODE write_val_in_hcd_pair(hcd_pair *pair, MVAL mval)
{
	hcd_pair copy;
	long res1 = copy_from_user(&copy, pair, sizeof(hcd_pair));

	if (res1 > 0) {
		return ILLEGAL;
	}
	long res = copy_to_user(copy.value, mval->val, mval->size);

	if (res > 0) {
		return ILLEGAL;
	}
	return SUCCESS;
}

RETURN_CODE read_hcd_create_info(hcd_create_info *info, char **string_ptr,
				 bool *is_protected)
{
	hcd_create_info copy;
	long res1 = copy_from_user(&copy, info, sizeof(hcd_create_info));

	if (res1 > 0) {
		return ILLEGAL;
	}
	RETURN_CODE res2 = read_string_from_user(copy.name, 100, string_ptr);

	if (res2 != SUCCESS) {
		return res2;
	}
	*is_protected = copy.flags == HCD_O_PROTECTED;
	return SUCCESS;
}

RETURN_CODE read_hcd_keys(hcd_keys *keys_user, hcd_key **user_keys_field_ptr,
			  hcd_keys **ks_keys_ptr)
{
	hcd_keys copy;
	long res1 = copy_from_user(&copy, keys_user, sizeof(hcd_keys));

	if (res1 > 0) {
		return ILLEGAL;
	}
	hcd_key *keys_p = (hcd_key *)m_malloc(sizeof(hcd_key) * copy.count);

	if (!keys_p) {
		return MEM_ERROR;
	}
	hcd_keys *new_keys = (hcd_keys *)m_malloc(sizeof(hcd_keys));

	if (!new_keys) {
		m_free(keys_p);
		return MEM_ERROR;
	}
	new_keys->count = copy.count;
	new_keys->keys = keys_p;

	*ks_keys_ptr = new_keys;
	*user_keys_field_ptr = copy.keys;
	return SUCCESS;
}

RETURN_CODE write_hcd_keys(hcd_keys *kernel_keys, hcd_key *user_keys,
			   unsigned int cnt)
{
	for (unsigned int i = 0; i < cnt; i++) {
		long res = copy_to_user(user_keys + i, kernel_keys->keys + i,
					HCD_KEYSIZE);
		if (res > 0) {
			return ILLEGAL;
		}
	}
	return SUCCESS;
}

RETURN_CODE read_hcd_key(hcd_key key, hcd_key user_key)
{
	int res = copy_from_user(key, user_key, sizeof(hcd_key));

	if (res > 0) {
		return ILLEGAL;
	} else {
		return SUCCESS;
	}
}

RETURN_CODE read_room_name(char **name_ptr, char *user_name)
{
	return read_string_from_user(user_name, 100, name_ptr);
}

static int device_open(struct inode *inode, struct file *file)
{
	file->f_mode = FMODE_READ | FMODE_WRITE;
	RETURN_CODE res = rooms_entity_enter(file);

	return translate_error_code_regular(res);
}

static int device_release(struct inode *inode, struct file *file)
{
	RETURN_CODE res = rooms_entity_exit(file);

	return translate_error_code_regular(res);
}

static ssize_t device_read(struct file *file, char __user *buffer, size_t len,
			   loff_t *offset)
{
	hcd_pair *_buffer = (hcd_pair *)buffer;
	MVAL mval;
	char *key;
	RETURN_CODE res = read_hcd_pair_for_read(_buffer, &key);

	if (res != SUCCESS) {
		return translate_error_code_regular(res);
	}
	res = rooms_get_val(key, &mval, file);
	m_free(key);
	if (res != SUCCESS) {
		return translate_error_code_regular(res);
	}
	int size = mval->size;

	if (mval->size > (int)len) {
		m_val_free(mval, NULL);
		return size;
	}
	res = write_val_in_hcd_pair(_buffer, mval);
	m_val_free(mval, NULL);
	return translate_error_code_regular(res);
}

static ssize_t device_write(struct file *file, const char __user *buffer,
			    size_t len, loff_t *offset)
{
	hcd_pair *_buffer = (hcd_pair *)buffer;
	char *key;
	MVAL mval;
	RETURN_CODE res =
		read_hcd_pair_for_write(_buffer, (int)len, &mval, &key);
	if (res != SUCCESS) {
		return translate_error_code_regular(res);
	}
	res = rooms_add_pair(key, mval, file);
	m_free(key);
	m_val_free(mval, NULL);
	return translate_error_code_regular(res);
}

long device_ioctl_delete_entry(struct file *file, unsigned long arg)
{
	char *_arg = (char *)arg;
	hcd_key key;
	RETURN_CODE res = read_hcd_key(key, _arg);

	if (res != SUCCESS) {
		return translate_error_code_for_ioctl(res);
	}
	res = rooms_delete_pair(key, file);
	return translate_error_code_for_ioctl(res);
}

long device_ioctl_create_room(struct file *file, unsigned long arg)
{
	hcd_create_info *_arg = (hcd_create_info *)arg;
	bool is_protected;
	char *name;
	RETURN_CODE res = read_hcd_create_info(_arg, &name, &is_protected);

	if (res != SUCCESS) {
		return translate_error_code_for_ioctl(res);
	}
	res = rooms_create_room_and_move(name, is_protected, file);
	m_free(name);
	return translate_error_code_for_ioctl(res);
}

long device_ioctl_move_room(struct file *file, unsigned long arg)
{
	char *_arg = (char *)arg;
	char *name;
	RETURN_CODE res = read_room_name(&name, _arg);

	if (res != SUCCESS) {
		return translate_error_code_for_ioctl(res);
	}
	res = rooms_change_room(name, file);
	m_free(name);
	return translate_error_code_for_ioctl(res);
}

long device_ioctl_get_key_count(struct file *file, unsigned long arg)
{
	int cnt;
	RETURN_CODE res = rooms_get_key_count(file, &cnt);

	if (res != SUCCESS) {
		return translate_error_code_for_ioctl(res);
	}
	return cnt;
}

long device_ioctl_dump_keys(struct file *file, unsigned long arg)
{
	hcd_keys *_arg = (hcd_keys *)arg;
	hcd_key *user_keys_to_fill;
	hcd_keys *copied_keys_struct;
	int cnt;
	RETURN_CODE res =
		read_hcd_keys(_arg, &user_keys_to_fill, &copied_keys_struct);
	if (res != SUCCESS) {
		return translate_error_code_for_ioctl(res);
	}
	res = rooms_dump_keys(file, copied_keys_struct, &cnt);
	if (res != SUCCESS) {
		free_hcd_keys(copied_keys_struct);
		return translate_error_code_for_ioctl(res);
	}
	res = write_hcd_keys(copied_keys_struct, user_keys_to_fill, cnt);
	free_hcd_keys(copied_keys_struct);
	if (res != SUCCESS) {
		return translate_error_code_for_ioctl(res);
	} else {
		return cnt;
	}
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (cmd == HCD_CREATE_ROOM_NUM) {
		return device_ioctl_create_room(file, arg);
	} else if (cmd == HCD_MOVE_ROOM_NUM) {
		return device_ioctl_move_room(file, arg);
	} else if (cmd == HCD_KEY_COUNT_NUM) {
		return device_ioctl_get_key_count(file, arg);
	} else if (cmd == HCD_KEY_DUMP_NUM) {
		return device_ioctl_dump_keys(file, arg);
	} else if (cmd == HCD_DELETE_ENTRY_NUM) {
		return device_ioctl_delete_entry(file, arg);
	} else {
		return -EINVAL;
	}
}

static loff_t device_llseek(struct file *file, loff_t offset, int whence)
{
	return -EPERM; // Disallow seek
}

#define DEVICE_NNAME "kdlp-module"

// Define the miscdevice structure
static struct miscdevice my_misc_device = {
	.minor = MISC_DYNAMIC_MINOR, // Automatically assign a minor number
	.name = DEVICE_NAME, // Name of the device (visible in /dev)
	.fops = &fops, // Pointer to file operations structure
};

static int __init hcd_init(void)
{
	RETURN_CODE res = init();

	if (res != SUCCESS) {
		printk(KERN_ALERT "Failed to allocate major number\n");
		return ENOMEM;
	}

	int error;
	// Register the misc device
	error = misc_register(&my_misc_device);
	if (error) {
		clean();
		pr_err("Failed to register misc device\n");
		return error;
	}

	pr_info("Misc device registered\n");
	return 0;
}

// Module exit function
static void __exit hcd_exit(void)
{
	clean();
	// Deregister the misc device
	misc_deregister(&my_misc_device);
	pr_info("Misc device deregistered\n");
}


module_init(hcd_init);
module_exit(hcd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bashar-osama");
MODULE_DESCRIPTION("F1 character device driver");
