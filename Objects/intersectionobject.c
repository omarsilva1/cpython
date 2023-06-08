// types.IntersectionType -- used to represent e.g. Intersection[int, str], int & str
#include "Python.h"
#include "pycore_object.h"  // _PyObject_GC_TRACK/UNTRACK
#include "pycore_intersectionobject.h"
#include "structmember.h"


static PyObject *make_intersection(PyObject *);


typedef struct {
    PyObject_HEAD
    PyObject *args;
    PyObject *parameters;
} intersectionobject;

static void
intersectionobject_dealloc(PyObject *self)
{
    intersectionobject *alias = (intersectionobject *)self;

    _PyObject_GC_UNTRACK(self);

    Py_XDECREF(alias->args);
    Py_XDECREF(alias->parameters);
    Py_TYPE(self)->tp_free(self);
}

static int
intersection_traverse(PyObject *self, visitproc visit, void *arg)
{
    intersectionobject *alias = (intersectionobject *)self;
    Py_VISIT(alias->args);
    Py_VISIT(alias->parameters);
    return 0;
}

static Py_hash_t
intersection_hash(PyObject *self)
{
    intersectionobject *alias = (intersectionobject *)self;
    PyObject *args = PyFrozenSet_New(alias->args);
    if (args == NULL) {
        return (Py_hash_t)-1;
    }
    Py_hash_t hash = PyObject_Hash(args);
    Py_DECREF(args);
    return hash;
}

static int
is_generic_alias_in_args(PyObject *args)
{
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    for (Py_ssize_t iarg = 0; iarg < nargs; iarg++) {
        PyObject *arg = PyTuple_GET_ITEM(args, iarg);
        if (_PyGenericAlias_Check(arg)) {
            return 0;
        }
    }
    return 1;
}

static PyObject *
intersection_instancecheck(PyObject *self, PyObject *instance)
{
    intersectionobject *alias = (intersectionobject *) self;
    Py_ssize_t nargs = PyTuple_GET_SIZE(alias->args);
    if (!is_generic_alias_in_args(alias->args)) {
        PyErr_SetString(PyExc_TypeError,
            "isinstance() argument 2 cannot contain a parameterized generic");
        return NULL;
    }
    for (Py_ssize_t iarg = 0; iarg < nargs; iarg++) {
        PyObject *arg = PyTuple_GET_ITEM(alias->args, iarg);
        if (PyType_Check(arg)) {
            int res = PyObject_IsInstance(instance, arg);
            if (res < 0) {
                return NULL;
            }
            if (res) {
                Py_RETURN_TRUE;
            }
        }
    }
    Py_RETURN_FALSE;
}

static PyObject *
intersection_subclasscheck(PyObject *self, PyObject *instance)
{
    if (!PyType_Check(instance)) {
        PyErr_SetString(PyExc_TypeError, "issubclass() arg 1 must be a class");
        return NULL;
    }
    intersectionobject *alias = (intersectionobject *)self;
    if (!is_generic_alias_in_args(alias->args)) {
        PyErr_SetString(PyExc_TypeError,
            "issubclass() argument 2 cannot contain a parameterized generic");
        return NULL;
    }
    Py_ssize_t nargs = PyTuple_GET_SIZE(alias->args);
    for (Py_ssize_t iarg = 0; iarg < nargs; iarg++) {
        PyObject *arg = PyTuple_GET_ITEM(alias->args, iarg);
        if (PyType_Check(arg)) {
            int res = PyObject_IsSubclass(instance, arg);
            if (res < 0) {
                return NULL;
            }
            if (!res) {
                Py_RETURN_FALSE;
            }
        }
    }
    Py_RETURN_TRUE;
}

static PyObject *
intersection_richcompare(PyObject *a, PyObject *b, int op)
{
    if (!_PyIntersection_Check(b) || (op != Py_EQ && op != Py_NE)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    PyObject *a_set = PySet_New(((intersectionobject*)a)->args);
    if (a_set == NULL) {
        return NULL;
    }
    PyObject *b_set = PySet_New(((intersectionobject*)b)->args);
    if (b_set == NULL) {
        Py_DECREF(a_set);
        return NULL;
    }
    PyObject *result = PyObject_RichCompare(a_set, b_set, op);
    Py_DECREF(b_set);
    Py_DECREF(a_set);
    return result;
}

static PyObject*
flatten_args(PyObject* args)
{
    Py_ssize_t arg_length = PyTuple_GET_SIZE(args);
    Py_ssize_t total_args = 0;
    // Get number of total args once it's flattened.
    for (Py_ssize_t i = 0; i < arg_length; i++) {
        PyObject *arg = PyTuple_GET_ITEM(args, i);
        if (_PyIntersection_Check(arg)) {
            total_args += PyTuple_GET_SIZE(((intersectionobject*) arg)->args);
        } else {
            total_args++;
        }
    }
    // Create new tuple of flattened args.
    PyObject *flattened_args = PyTuple_New(total_args);
    if (flattened_args == NULL) {
        return NULL;
    }
    Py_ssize_t pos = 0;
    for (Py_ssize_t i = 0; i < arg_length; i++) {
        PyObject *arg = PyTuple_GET_ITEM(args, i);
        if (_PyIntersection_Check(arg)) {
            PyObject* nested_args = ((intersectionobject*)arg)->args;
            Py_ssize_t nested_arg_length = PyTuple_GET_SIZE(nested_args);
            for (Py_ssize_t j = 0; j < nested_arg_length; j++) {
                PyObject* nested_arg = PyTuple_GET_ITEM(nested_args, j);
                Py_INCREF(nested_arg);
                PyTuple_SET_ITEM(flattened_args, pos, nested_arg);
                pos++;
            }
        } else {
            if (arg == Py_None) {
                arg = (PyObject *)&_PyNone_Type;
            }
            Py_INCREF(arg);
            PyTuple_SET_ITEM(flattened_args, pos, arg);
            pos++;
        }
    }
    assert(pos == total_args);
    return flattened_args;
}

static PyObject*
dedup_and_flatten_args(PyObject* args)
{
    args = flatten_args(args);
    if (args == NULL) {
        return NULL;
    }
    Py_ssize_t arg_length = PyTuple_GET_SIZE(args);
    PyObject *new_args = PyTuple_New(arg_length);
    if (new_args == NULL) {
        Py_DECREF(args);
        return NULL;
    }
    // Add unique elements to an array.
    Py_ssize_t added_items = 0;
    for (Py_ssize_t i = 0; i < arg_length; i++) {
        int is_duplicate = 0;
        PyObject* i_element = PyTuple_GET_ITEM(args, i);
        for (Py_ssize_t j = 0; j < added_items; j++) {
            PyObject* j_element = PyTuple_GET_ITEM(new_args, j);
            int is_ga = _PyGenericAlias_Check(i_element) &&
                        _PyGenericAlias_Check(j_element);
            // RichCompare to also deduplicate GenericAlias types (slower)
            is_duplicate = is_ga ? PyObject_RichCompareBool(i_element, j_element, Py_EQ)
                : i_element == j_element;
            // Should only happen if RichCompare fails
            if (is_duplicate < 0) {
                Py_DECREF(args);
                Py_DECREF(new_args);
                return NULL;
            }
            if (is_duplicate)
                break;
        }
        if (!is_duplicate) {
            Py_INCREF(i_element);
            PyTuple_SET_ITEM(new_args, added_items, i_element);
            added_items++;
        }
    }
    Py_DECREF(args);
    _PyTuple_Resize(&new_args, added_items);
    return new_args;
}

static int
is_intersectionable(PyObject *obj)
{
    return (obj == Py_None ||
        PyType_Check(obj) ||
        _PyGenericAlias_Check(obj) ||
        _PyIntersection_Check(obj));
}

PyObject *
_Py_intersection_type_and(PyObject* self, PyObject* other)
{
    if (!is_intersectionable(self) || !is_intersectionable(other)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    PyObject *tuple = PyTuple_Pack(2, self, other);
    if (tuple == NULL) {
        return NULL;
    }

    PyObject *new_intersection = make_intersection(tuple);
    Py_DECREF(tuple);
    return new_intersection;
}

static int
intersection_repr_item(_PyUnicodeWriter *writer, PyObject *p)
{
    PyObject *qualname = NULL;
    PyObject *module = NULL;
    PyObject *tmp;
    PyObject *r = NULL;
    int err;

    if (p == (PyObject *)&_PyNone_Type) {
        return _PyUnicodeWriter_WriteASCIIString(writer, "None", 4);
    }

    if (_PyObject_LookupAttr(p, &_Py_ID(__origin__), &tmp) < 0) {
        goto exit;
    }

    if (tmp) {
        Py_DECREF(tmp);
        if (_PyObject_LookupAttr(p, &_Py_ID(__args__), &tmp) < 0) {
            goto exit;
        }
        if (tmp) {
            // It looks like a GenericAlias
            Py_DECREF(tmp);
            goto use_repr;
        }
    }

    if (_PyObject_LookupAttr(p, &_Py_ID(__qualname__), &qualname) < 0) {
        goto exit;
    }
    if (qualname == NULL) {
        goto use_repr;
    }
    if (_PyObject_LookupAttr(p, &_Py_ID(__module__), &module) < 0) {
        goto exit;
    }
    if (module == NULL || module == Py_None) {
        goto use_repr;
    }

    // Looks like a class
    if (PyUnicode_Check(module) &&
        _PyUnicode_EqualToASCIIString(module, "builtins"))
    {
        // builtins don't need a module name
        r = PyObject_Str(qualname);
        goto exit;
    }
    else {
        r = PyUnicode_FromFormat("%S.%S", module, qualname);
        goto exit;
    }

use_repr:
    r = PyObject_Repr(p);
exit:
    Py_XDECREF(qualname);
    Py_XDECREF(module);
    if (r == NULL) {
        return -1;
    }
    err = _PyUnicodeWriter_WriteStr(writer, r);
    Py_DECREF(r);
    return err;
}

static PyObject *
intersection_repr(PyObject *self)
{
    intersectionobject *alias = (intersectionobject *)self;
    Py_ssize_t len = PyTuple_GET_SIZE(alias->args);

    _PyUnicodeWriter writer;
    _PyUnicodeWriter_Init(&writer);
     for (Py_ssize_t i = 0; i < len; i++) {
        if (i > 0 && _PyUnicodeWriter_WriteASCIIString(&writer, " & ", 3) < 0) {
            goto error;
        }
        PyObject *p = PyTuple_GET_ITEM(alias->args, i);
        if (intersection_repr_item(&writer, p) < 0) {
            goto error;
        }
    }
    return _PyUnicodeWriter_Finish(&writer);
error:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}

static PyMemberDef intersection_members[] = {
        {"__args__", T_OBJECT, offsetof(intersectionobject, args), READONLY},
        {0}
};

static PyMethodDef intersection_methods[] = {
        {"__instancecheck__", intersection_instancecheck, METH_O},
        {"__subclasscheck__", intersection_subclasscheck, METH_O},
        {0}};


static PyObject *
intersection_getitem(PyObject *self, PyObject *item)
{
    intersectionobject *alias = (intersectionobject *)self;
    // Populate __parameters__ if needed.
    if (alias->parameters == NULL) {
        alias->parameters = _Py_make_parameters(alias->args);
        if (alias->parameters == NULL) {
            return NULL;
        }
    }

    PyObject *newargs = _Py_subs_parameters(self, alias->args, alias->parameters, item);
    if (newargs == NULL) {
        return NULL;
    }

    PyObject *res;
    Py_ssize_t nargs = PyTuple_GET_SIZE(newargs);
    if (nargs == 0) {
        res = make_intersection(newargs);
    }
    else {
        res = PyTuple_GET_ITEM(newargs, 0);
        Py_INCREF(res);
        for (Py_ssize_t iarg = 1; iarg < nargs; iarg++) {
            PyObject *arg = PyTuple_GET_ITEM(newargs, iarg);
            Py_SETREF(res, PyNumber_And(res, arg));
            if (res == NULL) {
                break;
            }
        }
    }
    Py_DECREF(newargs);
    return res;
}

static PyMappingMethods intersection_as_mapping = {
    .mp_subscript = intersection_getitem,
};

static PyObject *
intersection_parameters(PyObject *self, void *Py_UNUSED(unused))
{
    intersectionobject *alias = (intersectionobject *)self;
    if (alias->parameters == NULL) {
        alias->parameters = _Py_make_parameters(alias->args);
        if (alias->parameters == NULL) {
            return NULL;
        }
    }
    Py_INCREF(alias->parameters);
    return alias->parameters;
}

static PyGetSetDef intersection_properties[] = {
    {"__parameters__", intersection_parameters, (setter)NULL, "Type variables in the types.IntersectionType.", NULL},
    {0}
};

static PyNumberMethods intersection_as_number = {
        .nb_and = _Py_intersection_type_and, // Add __and__ function
};

static const char* const cls_attrs[] = {
        "__module__",  // Required for compatibility with typing module
        NULL,
};

static PyObject *
intersection_getattro(PyObject *self, PyObject *name)
{
    intersectionobject *alias = (intersectionobject *)self;
    if (PyUnicode_Check(name)) {
        for (const char * const *p = cls_attrs; ; p++) {
            if (*p == NULL) {
                break;
            }
            if (_PyUnicode_EqualToASCIIString(name, *p)) {
                return PyObject_GetAttr((PyObject *) Py_TYPE(alias), name);
            }
        }
    }
    return PyObject_GenericGetAttr(self, name);
}

PyTypeObject _PyIntersection_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "types.IntersectionType",
    .tp_doc = PyDoc_STR("Represent a intersection type\n"
              "\n"
              "E.g. for int & str"),
    .tp_basicsize = sizeof(intersectionobject),
    .tp_dealloc = intersectionobject_dealloc,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = intersection_traverse,
    .tp_hash = intersection_hash,
    .tp_getattro = intersection_getattro,
    .tp_members = intersection_members,
    .tp_methods = intersection_methods,
    .tp_richcompare = intersection_richcompare,
    .tp_as_mapping = &intersection_as_mapping,
    .tp_as_number = &intersection_as_number,
    .tp_repr = intersection_repr,
    .tp_getset = intersection_properties,
};

static PyObject *
make_intersection(PyObject *args)
{
    assert(PyTuple_CheckExact(args));

    args = dedup_and_flatten_args(args);
    if (args == NULL) {
        return NULL;
    }
    if (PyTuple_GET_SIZE(args) == 1) {
        PyObject *result1 = PyTuple_GET_ITEM(args, 0);
        Py_INCREF(result1);
        Py_DECREF(args);
        return result1;
    }

    intersectionobject *result = PyObject_GC_New(intersectionobject, &_PyIntersection_Type);
    if (result == NULL) {
        Py_DECREF(args);
        return NULL;
    }

    result->parameters = NULL;
    result->args = args;
    _PyObject_GC_TRACK(result);
    return (PyObject*)result;
}
