#ifndef ___CA_H
#define ___CA_H

/* aes key */

typedef struct ca_descr_aes {
  unsigned int index;
  unsigned int parity;    /* 0 == even, 1 == odd */
  unsigned char cw[16];
} ca_descr_aes_t;

enum ca_descr_data_type {
        CA_DATA_IV,
        CA_DATA_KEY,
};

enum ca_descr_parity {
        CA_PARITY_EVEN,
        CA_PARITY_ODD,
};

typedef struct ca_descr_data {
        uint32_t index;
        enum ca_descr_parity parity;
        enum ca_descr_data_type data_type;
        uint32_t length;
        uint8_t *data;
} ca_descr_data_t;

#ifndef CA_SET_PID /* removed in kernel 4.14 */
typedef struct ca_pid {
        unsigned int pid;
        int index;          /* -1 == disable */
} ca_pid_t;
#define CA_SET_PID _IOW('o', 135, struct ca_pid)
#endif

#endif // __CA_H
