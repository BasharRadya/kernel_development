// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <sys/wait.h>

#include "hcd_module.h"
#include "tap.h"

#include <pthread.h>

#define MOD_PATH "/dev/kdlp-character-device"

// coloring
#define TESTING "\033[1;33mTESTING\033[0m"
#define FAIL_TXT "\033[0;31mFAIL\033[0m"
#define PASS_TXT "\033[0;32mPASS\033[0m"

static int test_count;
static int tests_failed;

#define RUN_TEST(name)                                                        \
	do {                                                                  \
		pid_t pid = fork();                                           \
		if (pid < 0) {                                                \
			perror("fork");                                       \
			exit(1);                                              \
		} else if (pid == 0) {                                        \
			printf(TESTING ": %s\n", #name);                      \
			printf("1..%d\n",                                     \
			       top_counter_##name - base_counter_##name - 1); \
			int test_result = test_##name();                      \
			printf("TEST RESULT: %s\n\n",                         \
			       (test_result == 0) ? PASS_TXT : FAIL_TXT);     \
			exit(test_result);                                    \
		} else {                                                      \
			test_count++;                                         \
			int status;                                           \
			waitpid(pid, &status, 0);                             \
			if (WIFEXITED(status) &&                              \
			    WEXITSTATUS(status) == TEST_FAIL) {               \
				tests_failed++;                               \
			}                                                     \
		}                                                             \
	} while (0)

TEST_DEFINE(just_open);
TEST_BODY(just_open, {
	int fd = open(MOD_PATH, 0);

	assert_false(fd < 0, "open should return a valid fd");
})

TEST_DEFINE(simple_write);
TEST_BODY(simple_write, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair pair;

	strncpy(pair.key, "key", sizeof(pair.key));
	pair.value = "value";

	int ret = write(room, &pair, strlen(pair.value));

	assert_eq(ret, 0, "write shouldn't fail");
})

TEST_DEFINE(multiple_writes);
TEST_BODY(multiple_writes, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair pair;
	char desc[1024];
	char value[64];

	pair.value = value;

	int ret;

	for (int i = 1; i <= 50; i++) {
		snprintf(pair.key, sizeof(pair.key), "key-%d", i);
		snprintf(pair.value, sizeof(value), "value-%d", i);
		snprintf(desc, sizeof(desc), "write number %d", i);

		ret = write(room, &pair, strlen(pair.value));

		if (ret != 0) {
			break;
		}
	}

	assert_eq(ret, 0, "none of the writes should failed");
})

TEST_DEFINE(blind_override_writes);
TEST_BODY(blind_override_writes, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair pair;
	char desc[1024];
	char value[64];

	pair.value = value;

	int ret;

	for (int i = 1; i <= 50; i++) {
		snprintf(pair.key, sizeof(pair.key), "same key");
		snprintf(pair.value, sizeof(value), "different value %d", i);
		snprintf(desc, sizeof(desc), "write number %d", i);

		ret = write(room, &pair, strlen(pair.value));

		if (ret != 0) {
			break;
		}
	}

	assert_eq(ret, 0, "none of the overriding writes should fail");
})

TEST_DEFINE(invalid_address_write);
TEST_BODY(invalid_address_write, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair pair;

	strncpy(pair.key, "some key", sizeof(pair.key));
	pair.value = (void *)30000; // address outside of the process's access

	int ret = write(room, &pair, 30);

	assert_eq(errno, EFAULT, "should fail to write outside of access");

	assert_eq(ret, -1, "write should return -1 on fail");
})

TEST_DEFINE(blind_read);
TEST_BODY(blind_read, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair pair;
	char value[20];

	strncpy(pair.key, "simple key", sizeof(pair.key));
	pair.value = value;

	int ret = read(room, &pair, sizeof(value));

	assert_eq(errno, EINVAL, "there should be no entries in the map");
	assert_eq(ret, -1, "on error, fail should return -1");
})

TEST_DEFINE(simple_write_read);
TEST_BODY(simple_write_read, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";

	int size = strlen("value") + 1;

	int ret = write(room, &write_pair, size);

	assert_eq(ret, 0, "write shouldn't fail");

	hcd_pair read_pair;
	char read_value_buffer[64];

	strncpy(read_pair.key, "key", sizeof(read_pair.key));
	read_pair.value = read_value_buffer;

	ret = read(room, &read_pair, size);

	assert_eq(ret, 0, "reading from \"key\" shouldn't fail");

	assert_eq(strncmp(write_pair.value, read_pair.value, size), 0,
		  "read should return what write wrote");
})

TEST_DEFINE(override_writes_and_read);
TEST_BODY(override_writes_and_read, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";

	int size = strlen("value") + 1;

	int ret = write(room, &write_pair, size);

	assert_eq(ret, 0, "first write shouldn't fail");

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "cooler value";

	size = strlen("cooler value") + 1;

	ret = write(room, &write_pair, size);

	assert_eq(ret, 0, "second write shouldn't fail");

	hcd_pair read_pair;
	char read_value_buffer[64];

	strncpy(read_pair.key, "key", sizeof(read_pair.key));
	read_pair.value = read_value_buffer;

	ret = read(room, &read_pair, size);

	assert_eq(ret, 0, "reading from \"key\" shouldn't fail");

	assert_eq(strncmp(write_pair.value, read_pair.value, size), 0,
		  "read should return what second write wrote");
})

TEST_DEFINE(count_too_small_read);
TEST_BODY(count_too_small_read, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";

	int size = strlen("value") + 1;

	int ret = write(room, &write_pair, size);

	assert_eq(ret, 0, "write shouldn't fail");

	hcd_pair read_pair;
	char read_value_buffer[64];

	strncpy(read_pair.key, "key", sizeof(read_pair.key));
	read_pair.value = read_value_buffer;

	int bytes_needed = read(room, &read_pair, 0);

	assert_eq(bytes_needed, size, "read should return needed byte count");

	ret = read(room, &read_pair, bytes_needed);

	assert_eq(ret, 0, "reading from \"key\" shouldn't fail");

	assert_eq(strncmp(write_pair.value, read_pair.value, size), 0,
		  "read should return what write wrote");
})

TEST_DEFINE(invalid_address_read);
TEST_BODY(invalid_address_read, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";

	int size = strlen("value") + 1;

	int ret = write(room, &write_pair, size);

	assert_eq(ret, 0, "write shouldn't fail");

	hcd_pair read_pair;

	strncpy(read_pair.key, "key", sizeof(read_pair.key));
	read_pair.value = (void *)30000; // outside of process's access

	ret = read(room, &read_pair, size);

	assert_eq(errno, EFAULT, "value is outside of process's access");

	assert_eq(ret, -1, "write should return -1 on fail");
})

TEST_DEFINE(override_writes);
TEST_BODY(override_writes, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair write_pair;
	char value[64];

	write_pair.value = value;

	hcd_pair read_pair;
	char read_buffer[64];

	strncpy(read_pair.key, "same key", sizeof(read_pair.key));
	read_pair.value = read_buffer;

	char desc[1024];

	int bad_writes = 0;
	int bad_reads = 0;
	int mismatch_reads = 0;

	for (int i = 1; i <= 50; i++) {
		snprintf(write_pair.key, sizeof(write_pair.key), "same key");
		snprintf(write_pair.value, sizeof(value), "value-%d", i);
		snprintf(desc, sizeof(desc), "write number %d", i);

		int value_len = strlen(write_pair.value) + 1;

		int ret = write(room, &write_pair, value_len);

		if (ret != 0)
			bad_writes++;

		ret = read(room, &read_pair, value_len);

		if (ret != 0)
			bad_reads++;

		if (strncmp(read_pair.value, write_pair.value, value_len))
			mismatch_reads++;
	}

	assert_eq(bad_writes, 0, "there should not be any bad writes");
	assert_eq(bad_reads, 0, "there should not be any bad reads");
	assert_eq(mismatch_reads, 0,
		  "read should always return most recent write");
})

TEST_DEFINE(rooms_dont_share_data);
TEST_BODY(rooms_dont_share_data, {
	int fd1 = open(MOD_PATH, 0);

	assert_true(fd1 > 0, "open room 1");

	int fd2 = open(MOD_PATH, 0);

	assert_true(fd2 > 0, "open room 2");

	hcd_pair pair;

	pair.value = "winds of winter";
	strncpy(pair.key, "somekey", sizeof(pair.key));

	int res = write(fd1, &pair, strlen(pair.value));

	assert_eq(res, 0, "check write returns success");

	char value2[32];

	pair.value = value2;
	res = read(fd2, &pair, sizeof(value2));

	assert_eq(res, -1, "check read returns failure");
	assert_eq(errno, EINVAL, "check errno equals EINVAL");
	//	assert_eq(strcmp(pair.value, "winds of winter"), 0,
	//		  "check read returns what write wrote");
})

TEST_DEFINE(create_room);
TEST_BODY(create_room, {
	int fd = open(MOD_PATH, 0);

	assert_true(fd > 0, "open module");

	hcd_create_info info;

	info.name = "best room";
	info.flags = HCD_O_PUBLIC;

	int res = ioctl(fd, HCD_CREATE_ROOM, &info);

	assert_eq(res, 0, "check open room returned success");
})

TEST_DEFINE(move_to_nonexistent_room);
TEST_BODY(move_to_nonexistent_room, {
	int fd = open(MOD_PATH, 0);

	assert_true(fd > 0, "open module");

	int res = ioctl(fd, HCD_MOVE_ROOM, "alice's nonexistent room");

	assert_eq(errno, ENOENT, "move should set errno to ENOENT");
	assert_eq(res, -1, "move should return -1");
})

TEST_DEFINE(key_count);
TEST_BODY(key_count, {
	int fd = open(MOD_PATH, 0);

	assert_true(fd > 0, "open module");

	hcd_pair pair;

	char buffer[] = "a dance with dragons";

	pair.value = &buffer;
	strncpy(pair.key, "hodor", 6);
	int key_len = strlen(pair.key);

	bool writes_passed = true;

	for (int i = 0; i < key_len; i++) {
		pair.key[i] = 'z';
		if (write(fd, &pair, sizeof(buffer))) {
			writes_passed = false;
		}
	}

	assert_true(writes_passed, "check all writes returned success");

	int keys_in_room = ioctl(fd, HCD_KEY_COUNT);

	assert_eq(keys_in_room, key_len,
		  "check key_num returned is the amount written");
})

TEST_DEFINE(delete_changes_key_count);
TEST_BODY(delete_changes_key_count, {
	int fd = open(MOD_PATH, 0);

	assert_true(fd > 0, "open module");

	hcd_pair pair;

	char buffer[] = "a dance with dragons";

	pair.value = &buffer;
	strncpy(pair.key, "hodor", 6);
	int key_len = strlen(pair.key);

	bool writes_passed = true;

	for (int i = 0; i < key_len; i++) {
		pair.key[i] = 'z';
		if (write(fd, &pair, sizeof(buffer))) {
			writes_passed = false;
		}
	}

	assert_true(writes_passed, "check all writes returned success");

	int res = ioctl(fd, HCD_DELETE_ENTRY, "zodor");

	assert_eq(res, 0, "delete should return success");

	int keys_in_room = ioctl(fd, HCD_KEY_COUNT);

	assert_eq(keys_in_room, key_len - 1,
		  "check delete reduced size by one");
})

TEST_DEFINE(key_dump);
TEST_BODY(key_dump, {
	int fd = open(MOD_PATH, 0);

	assert_true(fd > 0, "open module");

	hcd_pair pair;

	char buffer[] = "a feast for crows";

	pair.value = buffer;
	strncpy(pair.key, "bbb", sizeof(pair.key));
	int key_len = strlen(pair.key);

	bool writes_passed = true;

	for (int i = 0; i < key_len; i++) {
		pair.key[i] = 'a';
		if (write(fd, &pair, sizeof(buffer))) {
			writes_passed = false;
		}
	}

	assert_true(writes_passed, "check all writes returned success");

	int keys_in_room = ioctl(fd, HCD_KEY_COUNT);

	assert_eq(keys_in_room, key_len,
		  "check key_num returned is the amount written");

	hcd_keys keys;
	hcd_key keys_buffer[key_len];

	keys.keys = keys_buffer;
	keys.count = key_len;

	int res = ioctl(fd, HCD_KEY_DUMP, &keys);

	assert_eq(res, key_len,
		  "dump should return the amount of written keys");

	bool has_abb = false;
	bool has_aab = false;
	bool has_aaa = false;

	bool unexpected_key = false;

	for (int i = 0; i < keys_in_room; i++) {
		if (strcmp(keys.keys[i], "abb") == 0) {
			has_abb = true;
		} else if (strcmp(keys.keys[i], "aab") == 0) {
			has_aab = true;
		} else if (strcmp(keys.keys[i], "aaa") == 0) {
			has_aaa = true;
		} else {
			unexpected_key = true;
			break;
		}
	}

	assert_eq(unexpected_key, false,
		  "all keys should be in {abb, aab, aaa}");

	assert_eq(has_abb, true, "abb should be dumped");
	assert_eq(has_aab, true, "aab should be dumped");
	assert_eq(has_aaa, true, "aaa should be dumped");
})

TEST_DEFINE(simple_delete_entry);
TEST_BODY(simple_delete_entry, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";

	int size = strlen("value") + 1;

	int ret = write(room, &write_pair, size);

	assert_eq(ret, 0, "write shouldn't fail");

	ret = ioctl(room, HCD_DELETE_ENTRY, &write_pair.key);
	assert_eq(ret, 0, "delete shouldn't fail");

	hcd_pair read_pair;
	char read_value_buffer[64];

	strncpy(read_pair.key, "key", sizeof(read_pair.key));
	read_pair.value = read_value_buffer;

	ret = read(room, &read_pair, size);

	assert_eq(errno, EINVAL, "reading should return EINVAL");
})

TEST_DEFINE(delete_nonexisting_key);
TEST_BODY(delete_nonexisting_key, {
	int room = open(MOD_PATH, 0);

	assert_false(room < 0, "open should return a valid fd");

	int ret = ioctl(room, HCD_DELETE_ENTRY, "nonexisting");

	assert_eq(errno, ENOENT,
		  "deleting non existing key should set errno to ENOENT");
	assert_eq(ret, -1, "deleting non existing key should error");
})

TEST_DEFINE(check_moving_room);
TEST_BODY(check_moving_room, {
	int room1 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "child open should return a valid fd");

	hcd_create_info info;

	info.name = "lovely room";
	info.flags = HCD_O_PUBLIC;

	int ret = ioctl(room1, HCD_CREATE_ROOM, &info);

	assert_eq(ret, 0, "create lovely room should work");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";
	int size = strlen("value") + 1;

	ret = write(room1, &write_pair, size);
	assert_eq(ret, 0, "initial write shouldn't fail");

	int room2 = open(MOD_PATH, 0);

	assert_false(room2 < 0, "room2 should be created");

	ret = ioctl(room2, HCD_MOVE_ROOM, "lovely room");
	assert_eq(ret, 0, "room2 should point to room1 successfully");

	hcd_pair pair;

	strcpy(pair.key, "key");
	char buffer2[32];

	pair.value = &buffer2;
	ret = read(room2, &pair, sizeof(buffer2));

	assert_eq(ret, 0, "read returns success");

	assert_eq(strcmp(pair.value, "value"), 0,
		  "read the string that was written to room1");
})

TEST_DEFINE(check_room_write_permission);
TEST_BODY(check_room_write_permission, {
	int room1 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "child open should return a valid fd");

	hcd_create_info info;

	info.name = "lovely room";
	info.flags = HCD_O_PROTECTED;

	int ret = ioctl(room1, HCD_CREATE_ROOM, &info);

	assert_eq(ret, 0, "create lovely room should work");

	int room2 = open(MOD_PATH, 0);

	assert_false(room2 < 0, "room2 should be created");

	ret = ioctl(room2, HCD_MOVE_ROOM, "lovely room");
	assert_eq(ret, 0, "room2 should point to room1 successfully");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";
	int size = strlen("value") + 1;

	ret = write(room2, &write_pair, size);

	int errno_copy = errno;

	assert_eq(ret, -1, "write failure should return -1");

	assert_eq(errno_copy, EPERM,
		  "errno should be EPERM on permission error");
})

TEST_DEFINE(check_room_delete_permission);
TEST_BODY(check_room_delete_permission, {
	int room1 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "child open should return a valid fd");

	hcd_create_info info;

	info.name = "lovely room";
	info.flags = HCD_O_PROTECTED;

	int ret = ioctl(room1, HCD_CREATE_ROOM, &info);

	assert_eq(ret, 0, "create lovely room should work");

	hcd_pair write_pair;

	strncpy(write_pair.key, "key", sizeof(write_pair.key));
	write_pair.value = "value";
	int size = strlen("value") + 1;

	ret = write(room1, &write_pair, size);
	assert_eq(ret, 0, "initial write shouldn't fail");

	int room2 = open(MOD_PATH, 0);

	assert_false(room2 < 0, "room2 should be created");

	ret = ioctl(room2, HCD_MOVE_ROOM, "lovely room");
	assert_eq(ret, 0, "room2 should point to room1 successfully");

	ret = ioctl(room2, HCD_DELETE_ENTRY, write_pair.key);

	int errno_copy = errno;

	assert_eq(ret, -1, "delete failure should return -1");

	assert_eq(errno_copy, EPERM,
		  "errno should be EPERM on permission error");
})

TEST_DEFINE(check_close_deletes);
TEST_BODY(check_close_deletes, {
	int room1 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "child open should return a valid fd");

	hcd_create_info info;

	info.name = "lovely room";
	info.flags = HCD_O_PUBLIC;

	int ret = ioctl(room1, HCD_CREATE_ROOM, &info);

	assert_eq(ret, 0, "create lovely room should work");

	close(room1);

	int room2 = open(MOD_PATH, 0);

	assert_false(room2 < 0, "room2 should be created");

	ret = ioctl(room2, HCD_MOVE_ROOM, "lovely room");
	int errno_copy = errno;

	assert_eq(ret, -1, "moving to deleted room shouldn't work");
	assert_eq(errno_copy, ENOENT, "errno should be ENOENT");
})

TEST_DEFINE(double_create_room);
TEST_BODY(double_create_room, {
	int room1 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "open should return a valid fd");

	hcd_create_info info;

	info.name = "lovely room";
	info.flags = HCD_O_PUBLIC;

	int ret = ioctl(room1, HCD_CREATE_ROOM, &info);

	assert_eq(ret, 0, "create lovely room should work");

	info.name = "not so lovely room";

	ret = ioctl(room1, HCD_CREATE_ROOM, &info);
	assert_eq(ret, 0, "create not so lovely room should work");

	ret = ioctl(room1, HCD_MOVE_ROOM, "lovely room");
	int errno_copy = errno;

	assert_eq(ret, -1, "lovely room shouldn't exist");
	assert_eq(errno_copy, ENOENT, "errno should be ENOENT");
})

TEST_DEFINE(advanced_override);
TEST_BODY(advanced_override, {
	int room1 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "open should return a valid fd");

	int room2 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "open should return a valid fd");

	int room3 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "open should return a valid fd");

	hcd_create_info info_;

	info_.name = "dark room";
	info_.flags = HCD_O_PUBLIC;
	int ret = ioctl(room1, HCD_CREATE_ROOM, &info_);

	assert_eq(ret, 0, "create should work");

	hcd_pair initial_pair;

	strncpy(initial_pair.key, "initial_key", sizeof(initial_pair.key));
	initial_pair.value = "initial_value";
	int size = strlen(initial_pair.value) + 1;

	ret = write(room1, &initial_pair, size);
	assert_eq(ret, 0, "initial write shouldn't fail");

	ret = ioctl(room2, HCD_MOVE_ROOM, "dark room");
	assert_eq(ret, 0, "moving to dark room should work");

	hcd_pair override_pair;

	strncpy(override_pair.key, "initial_key", sizeof(override_pair.key));
	override_pair.value = "monkey banana";
	size = strlen(override_pair.value) + 1;

	ret = write(room2, &override_pair, size);
	assert_eq(ret, 0, "override write shouldn't fail");

	ret = ioctl(room3, HCD_MOVE_ROOM, "dark room");
	assert_eq(ret, 0, "moving to dark room should work");

	hcd_pair reading_pair;

	strncpy(reading_pair.key, "initial_key", sizeof(reading_pair.key));
	char buff[32];

	reading_pair.value = buff;

	ret = read(room3, &reading_pair, sizeof(buff));
	assert_eq(ret, 0, "reading should work");

	assert_eq(strcmp(reading_pair.value, "monkey banana"), 0,
		  "should return overridden value");
})

TEST_DEFINE(advanced_keycount);
TEST_BODY(advanced_keycount, {
	int room1 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "open should return a valid fd");

	int room2 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "open should return a valid fd");

	int room3 = open(MOD_PATH, 0);

	assert_false(room1 < 0, "open should return a valid fd");

	hcd_create_info info_;

	info_.name = "dark room";
	info_.flags = HCD_O_PUBLIC;

	int ret = ioctl(room1, HCD_CREATE_ROOM, &info_);

	assert_eq(ret, 0, "create should work");

	hcd_pair write_pair1;

	strncpy(write_pair1.key, "key1", sizeof(write_pair1.key));
	write_pair1.value = "value1";
	int size = strlen("value1") + 1;

	ret = write(room1, &write_pair1, size);
	assert_eq(ret, 0, "write shouldn't fail");

	ret = ioctl(room2, HCD_MOVE_ROOM, "dark room");
	assert_eq(ret, 0, "moving to dark room should work");

	hcd_pair write_pair2;

	strncpy(write_pair2.key, "key2", sizeof(write_pair2.key));
	write_pair2.value = "value2";
	size = strlen("value2") + 1;

	ret = write(room2, &write_pair2, size);
	assert_eq(ret, 0, "write shouldn't fail");

	ret = ioctl(room3, HCD_MOVE_ROOM, "dark room");
	assert_eq(ret, 0, "moving to dark room should work");

	int key_count = ioctl(room3, HCD_KEY_COUNT);

	assert_eq(key_count, 2, "there should be 2 keys");
})

int main(void)
{
	// basics
	RUN_TEST(just_open);
	RUN_TEST(simple_write);
	RUN_TEST(blind_read);
	RUN_TEST(simple_write_read);
	// more read write
	RUN_TEST(count_too_small_read);

	RUN_TEST(blind_override_writes);
	RUN_TEST(multiple_writes);
	RUN_TEST(override_writes);
	RUN_TEST(invalid_address_write);
	RUN_TEST(invalid_address_read);
	RUN_TEST(override_writes_and_read);
	// basic ioctl
	RUN_TEST(create_room);
	RUN_TEST(move_to_nonexistent_room);
	RUN_TEST(rooms_dont_share_data);
	RUN_TEST(key_count);
	RUN_TEST(key_dump);
	RUN_TEST(simple_delete_entry);
	RUN_TEST(delete_changes_key_count);
	RUN_TEST(check_close_deletes);
	RUN_TEST(double_create_room);
	// advanced ioctl
	RUN_TEST(check_moving_room);
	RUN_TEST(check_room_write_permission);
	RUN_TEST(check_room_delete_permission);
	RUN_TEST(advanced_override);
	RUN_TEST(advanced_keycount);
	printf("SUMMARY: %d/%d tests failed\n", tests_failed, test_count);
}
