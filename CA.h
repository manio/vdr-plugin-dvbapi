#ifndef ___CA_H
#define ___CA_H

/* aes key */

typedef struct ca_descr_aes {
  unsigned int index;
  unsigned int parity;    /* 0 == even, 1 == odd */
  unsigned char cw[16];
} ca_descr_aes_t;

#ifndef CA_SET_PID /* removed in kernel 4.14 */
typedef struct ca_pid {
        unsigned int pid;
        int index;          /* -1 == disable */
} ca_pid_t;
#define CA_SET_PID _IOW('o', 135, struct ca_pid)
#endif

#endif // __CA_H
