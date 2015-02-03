#include "dukpy.h"
#include <python3.4m/structmember.h>

static void DukContext_init_internal(DukContext *self)
{
    /* heap_stash[(void *)self->ctx] = (void *)self */
    duk_push_heap_stash(self->ctx);
    duk_push_pointer(self->ctx, self->ctx);
    duk_push_pointer(self->ctx, self);
    duk_put_prop(self->ctx, -3);
    duk_pop(self->ctx);

    duk_push_global_object(self->ctx);
    self->global = DukObject_from_DukContext(self, -1);
    duk_pop(self->ctx);
}


static int DukContext_init(DukContext *self, PyObject *args, PyObject *kw)
{
    (void)args;
    (void)kw;

    self->heap_manager = NULL;  /* We manage the heap */

    self->ctx = duk_create_heap_default();
    if (!self->ctx) {
        PyErr_SetString(PyExc_MemoryError, "Failed to create duktape heap");
        return -1;
    }

    /* heap_stash.heap = (void *)self */
    duk_push_heap_stash(self->ctx);
    duk_push_pointer(self->ctx, self);
    duk_put_prop_string(self->ctx, -2, "heap");
    duk_pop(self->ctx);

    DukContext_init_internal(self);

    return 0;
}

static PyObject *DukContext_new_global_env(DukContext *self, PyObject *args)
{
    DukContext *new_context;
    (void)args;

    new_context = PyObject_New(DukContext, &DukContext_Type);
    if (new_context == NULL)
        return NULL;

    new_context->heap_manager = self->heap_manager ? self->heap_manager : self;
    Py_INCREF(self);

    /* heap_stash[(void *)new_context] = new_context->ctx (thread object) */
    duk_push_heap_stash(self->ctx);
    duk_push_pointer(self->ctx, new_context);
    duk_push_thread_new_globalenv(self->ctx);
    new_context->ctx = duk_get_context(self->ctx, -1);
    duk_put_prop(self->ctx, -3);
    duk_pop(self->ctx);

    DukContext_init_internal(new_context);

    return (PyObject *)new_context;
}

static void DukContext_dealloc(DukContext *self)
{
    if (!self->heap_manager) {
        duk_destroy_heap(self->ctx);
    } else {
        /* Use heap manager's ctx because self->ctx is destroyed */
        duk_context *ctx = self->heap_manager->ctx;
        duk_push_heap_stash(ctx);

        /* delete heap_stash[(void *)self->ctx] */
        duk_push_pointer(ctx, self->ctx);
        duk_del_prop(ctx, -2);

        /* delete heap_stash[(void *)self] */
        duk_push_pointer(ctx, self);
        duk_del_prop(ctx, -2);

        duk_pop(ctx);
        Py_DECREF(self->heap_manager);
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *DukContext_eval(DukContext *self, PyObject *args, PyObject *kw)
{
    const char *code;
    int noresult = 0;
    PyObject *result = NULL;

    static char *keywords[] = {"code", "noreturn", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kw, "s|$p:eval", keywords,
                                     &code, &noresult)) {
        return NULL;
    }

    if (duk_peval_string(self->ctx, code) != 0) {
        PyErr_Format(PyExc_RuntimeError,
                     "Failed to evaluate code: %s",
                     duk_safe_to_string(self->ctx, -1));
        return NULL;
    }

    if (noresult) {
        duk_pop(self->ctx);
        Py_RETURN_NONE;
    }

    result = duk_to_python(self->ctx, -1);
    duk_pop(self->ctx);

    return result;
}

static PyObject *DukContext_eval_file(DukContext *self, PyObject *args, PyObject *kw)
{
    const char *path;
    int noresult = 0;
    PyObject *result = NULL;

    static char *keywords[] = {"path", "noreturn", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kw, "s|$p:eval", keywords,
                                     &path, &noresult)) {
        return NULL;
    }

    if (duk_peval_file(self->ctx, path) != 0) {
        PyErr_Format(PyExc_RuntimeError,
                     "Failed to evaluate file %s: %s",
                     path, duk_safe_to_string(self->ctx, -1));
        return NULL;
    }

    if (noresult) {
        duk_pop(self->ctx);
        Py_RETURN_NONE;
    }

    result = duk_to_python(self->ctx, -1);
    duk_pop(self->ctx);

    return result;
}


DukContext *DukContext_get(duk_context *ctx)
{
    DukContext *context;

    /* Read DukContext from heap_stash[(void *)ctx] */
    duk_push_heap_stash(ctx);
    duk_push_pointer(ctx, ctx);
    duk_get_prop(ctx, -2);
    context = duk_get_pointer(ctx, -1);
    duk_pop_n(ctx, 2);

    /* context is NULL for uncached contexts */
    return context;
}


static PyMethodDef DukContext_methods[] = {
    {"eval", (PyCFunction)DukContext_eval,
     METH_VARARGS | METH_KEYWORDS, "Evaluate code"},
    {"eval_file", (PyCFunction)DukContext_eval_file,
     METH_VARARGS | METH_KEYWORDS, "Evaluate a file"},
    {"new_global_env", (PyCFunction)DukContext_new_global_env,
     METH_NOARGS, "Return a new context with a fresh global object"},
    {NULL}
};

static PyMemberDef DukContext_members[] = {
    {"g", T_OBJECT_EX, offsetof(DukContext, global), READONLY,
     "The global object"},
    {NULL}
};


PyTypeObject DukContext_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "dukpy.Context",               /* tp_name */
    sizeof(DukContext),              /* tp_basicsize */
    0,                               /* tp_itemsize */
    (destructor)DukContext_dealloc,  /* tp_dealloc */
    0,                               /* tp_print */
    0,                               /* tp_getattr */
    0,                               /* tp_setattr */
    0,                               /* tp_reserved */
    0,                               /* tp_repr */
    0,                               /* tp_as_number */
    0,                               /* tp_as_sequence */
    0,                               /* tp_as_mapping */
    0,                               /* tp_hash  */
    0,                               /* tp_call */
    0,                               /* tp_str */
    0,                               /* tp_getattro */
    0,                               /* tp_setattro */
    0,                               /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,              /* tp_flags */
    "Duktape context",               /* tp_doc */
    0,                               /* tp_traverse */
    0,                               /* tp_clear */
    0,                               /* tp_richcompare */
    0,                               /* tp_weaklistoffset */
    0,                               /* tp_iter */
    0,                               /* tp_iternext */
    DukContext_methods,              /* tp_methods */
    DukContext_members,              /* tp_members */
    0,                               /* tp_getset */
    0,                               /* tp_base */
    0,                               /* tp_dict */
    0,                               /* tp_descr_get */
    0,                               /* tp_descr_set */
    0,                               /* tp_dictoffset */
    (initproc)DukContext_init        /* tp_init */
};
