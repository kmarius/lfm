#ifndef KEYS_H
#define KEYS_H

#ifndef ALT
#define ALT(x) (1024 + x)
#endif

#ifndef CTRL
#define CTRL(x) ((x)&0x1f)
#endif

const char *keytrans(int key);

#endif
