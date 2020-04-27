// %%cpp c_api_module.c
// %// Собираем модуль - динамическую библиотеку. Включаем нужные пути для инклюдов и динамические библиотеки
// %run clang -Wall c_api_module.c $(python3-config --includes --ldflags) -shared -fPIC -o c_api_module.so
#include <Python.h>

// Парсинг позиционных аргументов в лоб
static PyObject* func_1(PyObject* self, PyObject* args) {
    if (PyTuple_Size(args) != 2) {
        PyErr_SetString(PyExc_TypeError, "func_ret_str args error");
        return NULL;
    }
    int val_i; char *val_s;
    // i - long int, s - char*
    PyArg_ParseTuple(args, "is", &val_i, &val_s);
    printf("func1: int - %d, string - %s\n", val_i, val_s);
    return Py_BuildValue("is", val_i, val_s);
}

// Умный парсинг args и kwargs
static PyObject* func_2(PyObject* self, PyObject* args, PyObject* kwargs) {
    static const char* kwlist[] = {"val_i", "val_s", NULL};
    int val_i = 0; char* val_s = ""; size_t val_s_len = 0;
    // до | обязательные аргументы, i - long int, z# - char* + size_t
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|z#", (char**)kwlist, &val_i, &val_s, &val_s_len)) {
        return NULL;
    }
    printf("func2: int - %d, string - %s, string_len = %zu\n", val_i, val_s, val_s_len);
    return Py_BuildValue("is", val_i, val_s);
}

// Список функций модуля
static PyMethodDef methods[] = {
    {"func_1", func_1, METH_VARARGS, "func_1"},
    // METH_KEYWORDS - принимает еще и именованные аргументы
    {"func_2", (PyCFunction)func_2, METH_VARARGS | METH_KEYWORDS, "func_2"},
    {NULL, NULL, 0, NULL}
};

// Описание модуля
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "c_api_module", "Test module", -1, methods
};

// Инициализация модуля
PyMODINIT_FUNC PyInit_c_api_module(void) {
    PyObject* mod = PyModule_Create(&module);
    return mod;
}

