#include "scope2common.h"
#include "rtapi_string.h"
#include "rtapi_app.h"

static int comp_id;
static int shm_id;
static int nsamples = 4000;
struct scope_shm *shm;
RTAPI_MP_INT(nsamples, "Number of samples in ring buffer");

static int next(int i) {
    i++;
    if(i == nsamples) i=0;
    return i;
}

static struct scope_record *get_in_ptr() {
    int in = shm->in;
    int next_in = next(in);
    //printf("in=%d next_in=%d out=%d\n", in, next_in, shm->out);
    if(next_in == shm->out) return 0;
    return &shm->data[in];
}

static void advance_in_ptr() {
    shm->in = next(shm->in);
}

static void capture(void* unused, long period) {
    int i;
    struct scope_record *record;
    int newstate;

    if(!shm) return;

    newstate = shm->request_state;
    if(newstate != shm->current_state) {
	if(newstate == RUNNING)
	    shm->in = shm->out = shm->overruns = 0;
	shm->current_state = newstate;
    }

    if(shm->current_state != RUNNING) return;

    record = get_in_ptr();
    if(!record) {
	shm->overruns ++;
	return;
    }

    for(i=0; i<NCHANNELS; i++) {
	unsigned long offset = shm->channels[i].data_offset;
	hal_data_u *data = &record->data[i];
	if(offset) {
	    hal_data_u *addr = SHMPTR(offset);
	    switch(shm->channels[i].data_type) {
	    case HAL_BIT: data->b = addr->b; break;
	    case HAL_S32: data->s = addr->s; break;
	    case HAL_U32: data->u = addr->u; break;
	    case HAL_FLOAT: {
		ireal_t a, b;
		do {
		    a = addr->fi;
		    b = addr->fi;
		} while(a != b);
		data->fi = a;
	    }; break;
	    default: // should be impossible
		memset(data, 0, sizeof(*data));
	    }
	} else {
	    memset(data, 0, sizeof(*data));
	}
    }
    advance_in_ptr();
}

int rtapi_app_main() {
    int res = 0;

    comp_id = res = hal_init("scope2rt");
    if(comp_id < 0) goto fail1;

    res = hal_export_funct("scope2rt.capture", capture, 0, 0, 0, comp_id);
    if(res < 0) goto fail2;

    shm_id = res = rtapi_shmem_new(SHM_KEY, comp_id,
	    sizeof(*shm) + sizeof(*shm->data) * nsamples);
    if(res < 0) goto fail2;

    res = rtapi_shmem_getptr(shm_id, (void**)&shm);
    if(res < 0) goto fail3;

    shm->nsamples = nsamples;

    hal_ready(comp_id);
    return 0;

fail3:
    rtapi_shmem_delete(shm_id, comp_id);
fail2:
    hal_exit(comp_id);
fail1:
    return res;
}

void rtapi_app_exit() {
    shm = 0;
    rtapi_shmem_delete(shm_id, comp_id);
    hal_exit(comp_id);
}
