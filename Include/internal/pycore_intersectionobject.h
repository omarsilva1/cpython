#ifndef Py_INTERNAL_INTERSECTIONOBJECT_H
#define Py_INTERNAL_INTERSECTIONOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

extern PyTypeObject _PyIntersection_Type;
#define _PyIntersection_Check(op) Py_IS_TYPE(op, &_PyIntersection_Type)
extern PyObject *_Py_intersection_type_and(PyObject *, PyObject *);

#define _PyGenericAlias_Check(op) PyObject_TypeCheck(op, &Py_GenericAliasType)
extern PyObject *_Py_subs_parameters(PyObject *, PyObject *, PyObject *, PyObject *);
extern PyObject *_Py_make_parameters(PyObject *);

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_INTERSECTIONOBJECT_H */
