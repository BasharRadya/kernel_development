//
// Created by Os on 27/09/2024.
//

#ifndef F1_USER_MODULE_H
#define F1_USER_MODULE_H

#define HCD_KEYSIZE 32
#define HCD_O_PUBLIC 01
#define HCD_O_PROTECTED 02

#define HCD_CREATE_ROOM 0x01
#define HCD_MOVE_ROOM 0x06
#define HCD_KEY_COUNT 0x03
#define HCD_KEY_DUMP 0x04
#define HCD_DELETE_ENTRY 0x05

typedef char hcd_key[HCD_KEYSIZE];

typedef struct hcd_pair {
	hcd_key key;
	void *value;
} hcd_pair;

typedef struct hcd_create_info {
	char *name;
	int flags;
} hcd_create_info;

typedef struct hcd_keys {
	hcd_key *keys;
	unsigned int count;
} hcd_keys;

#endif //F1_USER_MODULE_H
