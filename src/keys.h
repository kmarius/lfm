#ifndef KEYS_H
#define KEYS_H

#ifndef ALT
#define ALT(x) (1024 + x)
#endif

#ifndef CTRL
#define CTRL(x) ((x)&0x1f)
#endif

/* TODO: make a function to filter (un)printable chars (on 2021-10-23) */

const char *keytrans(int key);

#endif /* KEYS_H */
