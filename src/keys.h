#ifndef KEYS_H
#define KEYS_H

#include <notcurses/notcurses.h>

/* if these bits are used we need to go higher */
#define SHIFT_MASK ((unsigned) 1 << 31)
#define CTRL_MASK ((unsigned) 1 << 30)
#define ALT_MASK ((unsigned) 1 << 29)
// #define KEY(u) (u & 0x0fffffff)
#define KEY(u) (u & 0x1fffffff)
#define ISSHIFT(u) ((u&SHIFT_MASK) >> 31)
#define ISCTRL(u) ((u&CTRL_MASK) >> 30)
#define ISALT(u) ((u&ALT_MASK) >> 29)
#define SHIFT(u) (u|SHIFT_MASK)
#define CTRL(u) (u|CTRL_MASK)
#define ALT(u) (u|ALT_MASK)

#define ncinput_to_long(in) \
	(long) ((in)->id \
			| ((in)->alt ? ALT_MASK : 0) \
			| ((in)->ctrl ? CTRL_MASK : 0) \
			| ((in)->shift ? SHIFT_MASK : 0))

const char *long_to_key_name(const long u);
long *key_names_to_longs(const char *keys, long *buf);

#endif /* KEYS_H */
