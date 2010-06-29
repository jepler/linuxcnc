#include <Python.h>
#include <object.h>
#include "scope2common.h"

#define HAL_MUTEX_GET \
    rtapi_mutex_get(&(hal_data->mutex))
#define HAL_MUTEX_GIVE \
    rtapi_mutex_give(&(hal_data->mutex))

typedef struct { PyObject_HEAD
    int hal_id;
    int shm_id;
    struct scope_shm *shm;
} scopeobject;

static int next(struct scope_shm *shm, int i) {
    i++;
    if(i == shm->nsamples) i=0;
    return i;
}

static struct scope_record *get_out_ptr(struct scope_shm *shm) {
    int out = shm->out;
    int in = shm->in;
    //printf("in=%d out=%d\n", in, out);
    if(out == in) return 0;
    return &shm->data[out];
}

static void advance_out_ptr(struct scope_shm *shm) {
    shm->out = next(shm, shm->out);
}

static PyObject *attach_thread(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    char *new_thread;
    int res;

    if(!PyArg_ParseTuple(args, "s:attach_thread", &new_thread)) return 0;

    if(*self->shm->current_thread) {
	hal_del_funct_from_thread("scope2rt.capture", self->shm->current_thread);
	strcpy(self->shm->current_thread, "");
    }

    res = hal_add_funct_to_thread("scope2rt.capture", new_thread, -1);
    if(res < 0) {
	errno = -res;
	PyErr_SetFromErrno(PyExc_RuntimeError);
	return 0;
    }
    snprintf(self->shm->current_thread, sizeof(self->shm->current_thread), "%s", new_thread);
    Py_RETURN_NONE;
}

static PyObject *get_thread_period(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    char *name = self->shm->current_thread;
    int next;
    PyObject *result = 0;

    if(!PyArg_ParseTuple(args, "|s:get_thread_period", &name)) return 0;

    HAL_MUTEX_GET;
    next = hal_data->thread_list_ptr;
    while(next) {
	hal_thread_t *thread = SHMPTR(next);
	if(!strcmp(name, thread->name)) {
	    result = PyInt_FromLong(thread->period);
	    break;
	}
    }
    HAL_MUTEX_GIVE;

    if(!result)
	PyErr_Format(PyExc_ValueError, "No such thread '%s'", name);
    return result;
}

static PyObject *start_capture(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    self->shm->request_state = RUNNING;
    Py_RETURN_NONE;
}

static PyObject *stop_capture(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    self->shm->request_state = STOPPED;
    Py_RETURN_NONE;
}

static PyObject *capture_state(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    return PyBool_FromLong(self->shm->current_state == RUNNING);
}

PyObject *set_channel_offset(struct scope_shm *shm, unsigned channel, unsigned long offset, hal_type_t type) {
    if(shm->current_state != STOPPED || shm->request_state != STOPPED) {
	PyErr_SetString(PyExc_RuntimeError, "Can only set channel while stopped");
	return 0;
    }
    if(!SHMCHK(SHMPTR(offset))) {
	PyErr_Format(PyExc_RuntimeError, "Data offset %lu out of range", offset);
	return 0;
    }
    if(channel >= NCHANNELS) { 
	PyErr_Format(PyExc_RuntimeError, "Channel %d out of range", channel);
	return 0;
    }
    shm->channels[channel].data_type = type;
    shm->channels[channel].data_offset = offset;
    Py_RETURN_NONE;
}

PyObject *set_channel_pin(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    hal_pin_t *pin;
    char *name;
    unsigned channel;
    PyObject *result;

    if(!PyArg_ParseTuple(args, "Is:set_channel_pin", &channel, &name)) return 0;

    HAL_MUTEX_GET;
    pin = halpr_find_pin_by_name(name);
    if(!pin) {
	HAL_MUTEX_GIVE;
	PyErr_Format(PyExc_KeyError, "No pin named %s", name);
	return 0;
    }

    result = set_channel_offset(self->shm, channel,
	    pin->signal
		? ((hal_sig_t*)SHMPTR(pin->signal))->data_ptr
		: SHMOFF(&pin->dummysig),
	    pin->type);
    HAL_MUTEX_GIVE;
    return result;
}

PyObject *set_channel_sig(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    hal_sig_t *sig;
    char *name;
    unsigned channel;
    PyObject *result;

    if(!PyArg_ParseTuple(args, "Is:set_channel_sig", &channel, &name)) return 0;

    HAL_MUTEX_GET;
    sig = halpr_find_sig_by_name(name);
    if(!sig) {
	HAL_MUTEX_GIVE;
	PyErr_Format(PyExc_KeyError, "No sig named %s", name);
	return 0;
    }

    result = set_channel_offset(self->shm, channel, sig->data_ptr, sig->type);
    HAL_MUTEX_GIVE;
    return result;
}

PyObject *set_channel_param(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    hal_param_t *param;
    char *name;
    unsigned channel;
    PyObject *result;

    if(!PyArg_ParseTuple(args, "Is:set_channel_param", &channel, &name))
	return 0;

    HAL_MUTEX_GET;
    param = halpr_find_param_by_name(name);
    if(!param) {
	HAL_MUTEX_GIVE;
	PyErr_Format(PyExc_KeyError, "No param named %s", name);
	return 0;
    }

    result = set_channel_offset(self->shm, channel, param->data_ptr, param->type);
    HAL_MUTEX_GIVE;
    return result;
}

PyObject *check_overflow(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    return PyInt_FromLong(self->shm->overruns);
}

PyObject *get_record(struct scope_shm *shm, struct scope_record *data) {
    PyObject *result = PyTuple_New(NCHANNELS);
    int i;
    if(!result) return 0;

    for(i=0; i<NCHANNELS; i++) {
	PyObject *value = 0;
	if(!shm->channels[i].data_offset) {
	    Py_INCREF(Py_None);
	    value = Py_None;
	} else switch(shm->channels[i].data_type) {
	    case HAL_BIT: value = PyBool_FromLong(data->data[i].b); break;
	    case HAL_U32: value = PyLong_FromUnsignedLong(data->data[i].u); break;
	    case HAL_S32: value = PyInt_FromLong(data->data[i].s); break;
	    case HAL_FLOAT: value = PyFloat_FromDouble(data->data[i].f); break;
	    default: PyErr_Format(PyExc_ValueError, "Unexpected type %d for channel %d\n", (int)shm->channels[i].data_type, i);
	}
	if(!value) {
	    Py_DECREF(value);
	    return 0;
	}
	PyTuple_SET_ITEM(result, i, value);
    }
    return result;
}

PyObject *get_samples(PyObject *_self, PyObject *args) {
    scopeobject *self = (scopeobject*)_self;
    PyObject *result = PyList_New(0);
    int max = self->shm->nsamples, count=0;
    struct scope_record *record;
    if(!PyArg_ParseTuple(args, "|i:get_samples", &max)) return 0;
    while(count < max && (record = get_out_ptr(self->shm))) {
	PyObject *pyrecord = get_record(self->shm, record);
	count++;
	if(!pyrecord) {
	    Py_DECREF(result);
	    return 0; // some records were lost if result was not empty!
	}
	if(PyList_Append(result, pyrecord) < 0) {
	    Py_DECREF(result);
	    Py_DECREF(pyrecord);
	    return 0; // some records were lost if result was not empty!
	}
	advance_out_ptr(self->shm);
    }
    return result;
}

PyObject *list_threads(PyObject *_self, PyObject *args) {
    PyObject *result = PyList_New(0);
    int next;

    if(!result) return 0;

    HAL_MUTEX_GET;
    next = hal_data->thread_list_ptr;
    while(next) {
	hal_thread_t *thread = SHMPTR(next);
	PyObject *pyname = PyString_FromString(thread->name);
	if(!pyname) {
	     Py_DECREF(result);
	     result = 0;
	     goto fail;
	}
	if(PyList_Append(result, pyname) < 0) {
	     Py_DECREF(result);
	     Py_DECREF(pyname);
	     result = 0;
	     goto fail;
	}
	next = thread->next_ptr;
    }

fail:
    HAL_MUTEX_GIVE;
    return result;
}

PyObject *list_pins(PyObject *_self, PyObject *args) {
    PyObject *result = PyList_New(0);
    int next;

    if(!result) return 0;

    HAL_MUTEX_GET;
    next = hal_data->pin_list_ptr;
    while(next) {
	hal_pin_t *pin = SHMPTR(next);
	PyObject *pyname = PyString_FromString(pin->name);
	if(!pyname) {
	     Py_DECREF(result);
	     result = 0;
	     goto fail;
	}
	if(PyList_Append(result, pyname) < 0) {
	     Py_DECREF(result);
	     Py_DECREF(pyname);
	     result = 0;
	     goto fail;
	}
	next = pin->next_ptr;
    }

fail:
    HAL_MUTEX_GIVE;
    return result;
}

PyObject *list_params(PyObject *_self, PyObject *args) {
    PyObject *result = PyList_New(0);
    int next;

    if(!result) return 0;

    HAL_MUTEX_GET;
    next = hal_data->param_list_ptr;
    while(next) {
	hal_param_t *param = SHMPTR(next);
	PyObject *pyname = PyString_FromString(param->name);
	if(!pyname) {
	     Py_DECREF(result);
	     result = 0;
	     goto fail;
	}
	if(PyList_Append(result, pyname) < 0) {
	     Py_DECREF(result);
	     Py_DECREF(pyname);
	     result = 0;
	     goto fail;
	}
	next = param->next_ptr;
    }

fail:
    HAL_MUTEX_GIVE;
    return result;
}

PyObject *list_sigs(PyObject *_self, PyObject *args) {
    PyObject *result = PyList_New(0);
    int next;

    if(!result) return 0;

    HAL_MUTEX_GET;
    next = hal_data->sig_list_ptr;
    while(next) {
	hal_sig_t *sig = SHMPTR(next);
	PyObject *pyname = PyString_FromString(sig->name);
	if(!pyname) {
	     Py_DECREF(result);
	     result = 0;
	     goto fail;
	}
	if(PyList_Append(result, pyname) < 0) {
	     Py_DECREF(result);
	     Py_DECREF(pyname);
	     result = 0;
	     goto fail;
	}
	next = sig->next_ptr;
    }

fail:
    HAL_MUTEX_GIVE;
    return result;
}

int scope_init(PyObject *_self, PyObject *args, PyObject *kw) {
    scopeobject *self = (scopeobject*)_self;
    char *name = "scope2usr";
    unsigned int nsamples;
    int res;

    if(!PyArg_ParseTuple(args, "|s:scope.Scope", &name)) return -1;

    self->hal_id = hal_init(name);
    if(self->hal_id <= 0) {
	errno = -self->hal_id;
	self->hal_id = 0;
	PyErr_SetFromErrno(PyExc_RuntimeError);
	return -1;
    }

    self->shm_id = res = rtapi_shmem_new(SHM_KEY, self->hal_id,
	    sizeof(*self->shm));
    if(res < 0) {
	errno = -res;
	PyErr_SetFromErrno(PyExc_RuntimeError);
	hal_exit(self->hal_id);
	self->hal_id = 0;
	return -1;
    }

    res = rtapi_shmem_getptr(self->shm_id, (void**)&self->shm);
    if(res < 0) {
	errno = -res;
	PyErr_SetFromErrno(PyExc_RuntimeError);
	rtapi_shmem_delete(self->shm_id, self->hal_id);
	hal_exit(self->hal_id);
	self->hal_id = 0;
	return -1;
    }

    nsamples = self->shm->nsamples;
    rtapi_shmem_delete(self->shm_id, self->hal_id);

    if(nsamples == 0) {
	PyErr_SetString(PyExc_RuntimeError, "nsamples is 0 (is scope2rt loaded?)");
	hal_exit(self->hal_id);
	self->hal_id = 0;
	return -1;
    }

    self->shm_id = res = rtapi_shmem_new(SHM_KEY, self->hal_id,
	    sizeof(*self->shm) + nsamples * sizeof(self->shm->data[0]));
    if(res < 0) {
	errno = -res;
	PyErr_SetFromErrno(PyExc_RuntimeError);
	hal_exit(self->hal_id);
	self->hal_id = 0;
	return -1;
    }

    res = rtapi_shmem_getptr(self->shm_id, (void**)&self->shm);
    if(res < 0) {
	errno = -res;
	PyErr_SetFromErrno(PyExc_RuntimeError);
	rtapi_shmem_delete(self->shm_id, self->hal_id);
	hal_exit(self->hal_id);
	self->hal_id = 0;
	return -1;
    }

    return 0;
}

static void scope_delete(PyObject *_self) {
    scopeobject *self = (scopeobject *)_self;
    if(self->shm_id > 0)
	rtapi_shmem_delete(self->shm_id, self->hal_id);
    if(self->hal_id > 0) 
        hal_exit(self->hal_id);

    PyObject_Del(_self);
}

static PyMethodDef scope_methods[] = {
    {"attach_thread", attach_thread, METH_VARARGS,
	"attach the capture function to the named thread"},
    {"get_thread_period", get_thread_period, METH_VARARGS,
	"get the period in nanoseconds of the attached or named thread"},
    {"start_capture", start_capture, METH_VARARGS,
	"start capturing the selected channels"},
    {"stop_capture", stop_capture, METH_VARARGS,
	"stop capturing the selected channels"},
    {"capture_state", capture_state, METH_NOARGS,
	"return the current capture state"},
    {"set_channel_pin", set_channel_pin, METH_VARARGS,
	"Make the given channel record the given pin"},
    {"set_channel_param", set_channel_param, METH_VARARGS,
	"Make the given channel record the given parameter"},
    {"set_channel_sig", set_channel_sig, METH_VARARGS,
	"Make the given channel record the given signal"},
    {"check_overflow", check_overflow, METH_NOARGS,
	"Return the number of overflows since capture started"},
    {"get_samples", get_samples, METH_VARARGS,
	"Return all available samples"},
    {"list_threads", list_threads, METH_NOARGS,
	"Return a list of realtime threads"},
    {"list_pins", list_pins, METH_NOARGS,
	"Return a list of pins"},
    {"list_params", list_params, METH_NOARGS,
	"Return a list of parameters"},
    {"list_sigs", list_sigs, METH_NOARGS,
	"Return a list of signals"},
    {NULL}
};

PyTypeObject scopeobject_type = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "scope2.Scope",            /*tp_name*/
    sizeof(scopeobject),       /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    scope_delete,              /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,	                       /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "HAL Scope NG",            /*tp_doc*/
    0,                         /*tp_traverse*/
    0,                         /*tp_clear*/
    0,                         /*tp_richcompare*/
    0,                         /*tp_weaklistoffset*/
    0,                         /*tp_iter*/
    0,                         /*tp_iternext*/
    scope_methods,             /*tp_methods*/
    0,                         /*tp_members*/
    0,                         /*tp_getset*/
    0,                         /*tp_base*/
    0,                         /*tp_dict*/
    0,                         /*tp_descr_get*/
    0,                         /*tp_descr_set*/
    0,                         /*tp_dictoffset*/
    scope_init,                /*tp_init*/
    0,                         /*tp_alloc*/
    PyType_GenericNew,         /*tp_new*/
    0,                         /*tp_free*/
    0,                         /*tp_is_gc*/
};


PyMethodDef module_methods[] = {
    {NULL},
};

char *module_doc = "Interface to hal's scope2\n"
"\n"
"This module allows capture of data in real time, with analysis in userspace.";

void initscope2(void) {
    PyObject *m = Py_InitModule3("scope2", module_methods, module_doc);

    PyType_Ready(&scopeobject_type);
    PyModule_AddObject(m, "Scope", (PyObject*)&scopeobject_type);

    PyModule_AddIntConstant(m, "HAL_BIT", HAL_BIT);
    PyModule_AddIntConstant(m, "HAL_FLOAT", HAL_FLOAT);
    PyModule_AddIntConstant(m, "HAL_S32", HAL_S32);
    PyModule_AddIntConstant(m, "HAL_U32", HAL_U32);

    PyModule_AddIntConstant(m, "NCHANNELS", NCHANNELS);
}

