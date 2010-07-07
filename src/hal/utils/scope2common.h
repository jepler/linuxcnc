#ifndef HAL_SCOPE2COMMON_H
#define HAL_SCOPE2COMMON_H
#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"

#define NCHANNELS (16)

#define SHM_KEY (0x32435348) // 'HSC2'

struct scope_capture {
    unsigned long data_offset;
    hal_type_t data_type;
};

struct scope_record {
    hal_data_u data[NCHANNELS];
};

enum scope_state { STOPPED=0, RUNNING=1 };

struct scope_shm {
    struct scope_capture channels[NCHANNELS];
    unsigned int nsamples;
    unsigned int overruns;
    volatile unsigned int in, out;
    volatile enum scope_state request_state;
    volatile enum scope_state current_state;
    char current_thread[HAL_NAME_LEN+1];
    struct scope_record data[];
};
#endif
