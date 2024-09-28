#define HCD_KEYSIZE 32
#define HCD_O_PUBLIC 01
#define HCD_O_PROTECTED 02

//// define ioctl operations
#define HCD_MAGIC ('z')
#define HCD_CREATE_ROOM _IOW(HCD_MAGIC, 0x01, hcd_create_info)
#define HCD_MOVE_ROOM _IOW(HCD_MAGIC, 0x06, const char *)
#define HCD_KEY_COUNT _IO(HCD_MAGIC, 0x03)
#define HCD_KEY_DUMP _IOR(HCD_MAGIC, 0x04, hcd_keys)
#define HCD_DELETE_ENTRY _IOW(HCD_MAGIC, 0x05, hcd_key)

#define HCD_CREATE_ROOM_NUM 0x01
#define HCD_MOVE_ROOM_NUM 0x06
#define HCD_KEY_COUNT_NUM 0x03
#define HCD_KEY_DUMP_NUM 0x04
#define HCD_DELETE_ENTRY_NUM 0x05

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
