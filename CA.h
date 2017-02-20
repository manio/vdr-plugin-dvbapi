#ifndef ___CA_H
#define ___CA_H

/* aes key */

typedef struct ca_descr_aes {
  unsigned int index;
  unsigned int parity;    /* 0 == even, 1 == odd */
  unsigned char cw[16];
} ca_descr_aes_t;

#endif
