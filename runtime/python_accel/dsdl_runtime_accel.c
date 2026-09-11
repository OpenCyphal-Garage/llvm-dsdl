//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// CPython accelerator module for generated Python DSDL runtime operations.
///
/// This module provides optional C-backed bit/number/float primitives using
/// runtime/dsdl_runtime.h while preserving the pure-Python API contract.
///
//===----------------------------------------------------------------------===//

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdbool.h>
#include <stdint.h>

#include "dsdl_runtime.h"

static int get_readonly_bytes(PyObject* obj, const uint8_t** outData, Py_ssize_t* outSize, PyObject** outOwner)
{
    if (PyBytes_Check(obj))
    {
        *outOwner = obj;
        Py_INCREF(*outOwner);
        *outData = (const uint8_t*) PyBytes_AS_STRING(obj);
        *outSize = PyBytes_GET_SIZE(obj);
        return 1;
    }
    if (PyByteArray_Check(obj))
    {
        *outOwner = obj;
        Py_INCREF(*outOwner);
        *outData = (const uint8_t*) PyByteArray_AS_STRING(obj);
        *outSize = PyByteArray_GET_SIZE(obj);
        return 1;
    }
    if (PyMemoryView_Check(obj))
    {
        const Py_buffer* const view = PyMemoryView_GET_BUFFER(obj);
        if (!PyBuffer_IsContiguous(view, 'C'))
        {
            PyErr_SetString(PyExc_TypeError, "expected a contiguous memoryview");
            return 0;
        }
        *outOwner = obj;
        Py_INCREF(*outOwner);
        *outData = (const uint8_t*) view->buf;
        *outSize = view->len;
        return 1;
    }

    PyObject* bytesObj = PyBytes_FromObject(obj);
    if (bytesObj == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "expected bytes-like object");
        return 0;
    }
    *outOwner = bytesObj;
    *outData  = (const uint8_t*) PyBytes_AS_STRING(bytesObj);
    *outSize  = PyBytes_GET_SIZE(bytesObj);
    return 1;
}

static int get_mutable_bytearray(PyObject* obj, uint8_t** outData, Py_ssize_t* outSize)
{
    if (PyMemoryView_Check(obj))
    {
        const Py_buffer* const view = PyMemoryView_GET_BUFFER(obj);
        if (view->readonly || !PyBuffer_IsContiguous(view, 'C'))
        {
            PyErr_SetString(PyExc_TypeError, "expected a writable contiguous memoryview");
            return 0;
        }
        *outData = (uint8_t*) view->buf;
        *outSize = view->len;
        return 1;
    }
    if (!PyByteArray_Check(obj))
    {
        PyErr_SetString(PyExc_TypeError, "expected bytearray or writable memoryview");
        return 0;
    }
    *outData = (uint8_t*) PyByteArray_AS_STRING(obj);
    *outSize = PyByteArray_GET_SIZE(obj);
    return 1;
}

static int to_u8_len_bits(unsigned long long lenBits, uint8_t* out)
{
    if (lenBits > 64ULL)
    {
        PyErr_SetString(PyExc_ValueError, "len_bits must be <= 64");
        return 0;
    }
    *out = (uint8_t) lenBits;
    return 1;
}

static PyObject* py_byte_length_for_bits(PyObject* self, PyObject* args)
{
    (void) self;
    long long totalBits = 0;
    if (!PyArg_ParseTuple(args, "L", &totalBits))
    {
        return NULL;
    }
    if (totalBits <= 0)
    {
        return PyLong_FromLong(0);
    }
    const unsigned long long out = ((unsigned long long) totalBits + 7ULL) / 8ULL;
    return PyLong_FromUnsignedLongLong(out);
}

static PyObject* py_set_bit(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          dstObj  = NULL;
    unsigned long long offBits = 0;
    int                value   = 0;
    if (!PyArg_ParseTuple(args, "OKp", &dstObj, &offBits, &value))
    {
        return NULL;
    }

    uint8_t*   dst     = NULL;
    Py_ssize_t dstSize = 0;
    if (!get_mutable_bytearray(dstObj, &dst, &dstSize))
    {
        return NULL;
    }

    const int8_t rc = dsdl_runtime_set_bit(dst, (size_t) dstSize, (size_t) offBits, value != 0);
    if (rc < 0)
    {
        PyErr_SetString(PyExc_ValueError, "set_bit failed: serialisation buffer too small");
        return NULL;
    }
    return PyLong_FromLong(0);
}

static PyObject* py_get_bit(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          srcObj  = NULL;
    unsigned long long offBits = 0;
    if (!PyArg_ParseTuple(args, "OK", &srcObj, &offBits))
    {
        return NULL;
    }

    const uint8_t* src      = NULL;
    Py_ssize_t     srcSize  = 0;
    PyObject*      srcOwner = NULL;
    if (!get_readonly_bytes(srcObj, &src, &srcSize, &srcOwner))
    {
        return NULL;
    }

    const bool out = dsdl_runtime_get_bit(src, (size_t) srcSize, (size_t) offBits);
    Py_DECREF(srcOwner);
    if (out)
    {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject* py_copy_bits(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          dstObj     = NULL;
    unsigned long long dstOffBits = 0;
    PyObject*          srcObj     = NULL;
    unsigned long long srcOffBits = 0;
    unsigned long long lenBits    = 0;
    if (!PyArg_ParseTuple(args, "OKOKK", &dstObj, &dstOffBits, &srcObj, &srcOffBits, &lenBits))
    {
        return NULL;
    }

    uint8_t*   dst     = NULL;
    Py_ssize_t dstSize = 0;
    if (!get_mutable_bytearray(dstObj, &dst, &dstSize))
    {
        return NULL;
    }

    const uint8_t* src      = NULL;
    Py_ssize_t     srcSize  = 0;
    PyObject*      srcOwner = NULL;
    if (!get_readonly_bytes(srcObj, &src, &srcSize, &srcOwner))
    {
        return NULL;
    }

    if (dstOffBits + lenBits > (unsigned long long) dstSize * 8ULL)
    {
        Py_DECREF(srcOwner);
        PyErr_SetString(PyExc_ValueError, "destination range exceeds destination buffer");
        return NULL;
    }
    if (srcOffBits + lenBits > (unsigned long long) srcSize * 8ULL)
    {
        Py_DECREF(srcOwner);
        PyErr_SetString(PyExc_ValueError, "source range exceeds source buffer");
        return NULL;
    }

    dsdl_runtime_copy_bits(dst, (size_t) dstOffBits, (size_t) lenBits, src, (size_t) srcOffBits);
    Py_DECREF(srcOwner);
    Py_RETURN_NONE;
}

static PyObject* py_extract_bits(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          srcObj     = NULL;
    unsigned long long srcOffBits = 0;
    unsigned long long lenBits    = 0;
    if (!PyArg_ParseTuple(args, "OKK", &srcObj, &srcOffBits, &lenBits))
    {
        return NULL;
    }

    const uint8_t* src      = NULL;
    Py_ssize_t     srcSize  = 0;
    PyObject*      srcOwner = NULL;
    if (!get_readonly_bytes(srcObj, &src, &srcSize, &srcOwner))
    {
        return NULL;
    }

    const size_t outSize = ((size_t) lenBits + 7U) / 8U;
    PyObject*    out     = PyBytes_FromStringAndSize(NULL, (Py_ssize_t) outSize);
    if (out == NULL)
    {
        Py_DECREF(srcOwner);
        return NULL;
    }

    uint8_t* outData = (uint8_t*) PyBytes_AS_STRING(out);
    if (outSize > 0U)
    {
        memset(outData, 0, outSize);
    }

    dsdl_runtime_get_bits(outData, src, (size_t) srcSize, (size_t) srcOffBits, (size_t) lenBits);
    Py_DECREF(srcOwner);
    return out;
}

static int read_i64(PyObject* valueObj, long long* outValue, int* outOverflow)
{
    int       overflow = 0;
    long long v        = PyLong_AsLongLongAndOverflow(valueObj, &overflow);
    if (PyErr_Occurred())
    {
        return 0;
    }
    *outValue    = v;
    *outOverflow = overflow;
    return 1;
}

static PyObject* py_write_unsigned(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          dstObj     = NULL;
    unsigned long long offBits    = 0;
    unsigned long long lenBitsRaw = 0;
    PyObject*          valueObj   = NULL;
    int                saturating = 0;
    if (!PyArg_ParseTuple(args, "OKKOi", &dstObj, &offBits, &lenBitsRaw, &valueObj, &saturating))
    {
        return NULL;
    }

    uint8_t lenBits = 0;
    if (!to_u8_len_bits(lenBitsRaw, &lenBits))
    {
        return NULL;
    }

    uint8_t*   dst     = NULL;
    Py_ssize_t dstSize = 0;
    if (!get_mutable_bytearray(dstObj, &dst, &dstSize))
    {
        return NULL;
    }

    long long signedValue = 0;
    int       overflow    = 0;
    if (!read_i64(valueObj, &signedValue, &overflow))
    {
        return NULL;
    }
    // A value past the signed range is read again as unsigned: a 64-bit field holds up to
    // UINT64_MAX, which the plan hands over as the value it is.
    uint64_t wide      = 0;
    int      wideValid = 0;
    if (overflow > 0)
    {
        wide = PyLong_AsUnsignedLongLong(valueObj);
        if (PyErr_Occurred())
        {
            PyErr_Clear();
        }
        else
        {
            wideValid = 1;
        }
    }

    const uint64_t mask = (lenBits == 64U) ? UINT64_MAX : ((UINT64_C(1) << lenBits) - UINT64_C(1));
    uint64_t       out  = 0;
    if (saturating)
    {
        if (overflow < 0 || (overflow == 0 && signedValue < 0))
        {
            out = 0;
        }
        else if (overflow > 0)
        {
            out = (wideValid && wide <= mask) ? wide : mask;
        }
        else
        {
            out = ((uint64_t) signedValue > mask) ? mask : (uint64_t) signedValue;
        }
    }
    else
    {
        if (overflow > 0 && wideValid)
        {
            out = wide & mask;
        }
        else if (overflow != 0)
        {
            PyErr_SetString(PyExc_OverflowError, "write_unsigned value does not fit into 64 bits");
            return NULL;
        }
        else
        {
            out = ((uint64_t) signedValue) & mask;
        }
    }

    const int8_t rc = dsdl_runtime_set_uxx(dst, (size_t) dstSize, (size_t) offBits, out, lenBits);
    if (rc < 0)
    {
        PyErr_SetString(PyExc_ValueError, "write_unsigned failed: serialisation buffer too small");
        return NULL;
    }

    return PyLong_FromLong(0);
}

static PyObject* py_write_signed(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          dstObj     = NULL;
    unsigned long long offBits    = 0;
    unsigned long long lenBitsRaw = 0;
    PyObject*          valueObj   = NULL;
    int                saturating = 0;
    if (!PyArg_ParseTuple(args, "OKKOi", &dstObj, &offBits, &lenBitsRaw, &valueObj, &saturating))
    {
        return NULL;
    }

    uint8_t lenBits = 0;
    if (!to_u8_len_bits(lenBitsRaw, &lenBits))
    {
        return NULL;
    }

    uint8_t*   dst     = NULL;
    Py_ssize_t dstSize = 0;
    if (!get_mutable_bytearray(dstObj, &dst, &dstSize))
    {
        return NULL;
    }

    long long signedValue = 0;
    int       overflow    = 0;
    if (!read_i64(valueObj, &signedValue, &overflow))
    {
        return NULL;
    }

    int64_t outValue = 0;
    if (saturating)
    {
        const int64_t minValue = (lenBits == 64U) ? INT64_MIN : -((int64_t) UINT64_C(1) << (lenBits - 1U));
        const int64_t maxValue = (lenBits == 64U) ? INT64_MAX : (((int64_t) UINT64_C(1) << (lenBits - 1U)) - 1);
        if (overflow < 0)
        {
            outValue = minValue;
        }
        else if (overflow > 0)
        {
            outValue = maxValue;
        }
        else if ((int64_t) signedValue < minValue)
        {
            outValue = minValue;
        }
        else if ((int64_t) signedValue > maxValue)
        {
            outValue = maxValue;
        }
        else
        {
            outValue = (int64_t) signedValue;
        }
    }
    else
    {
        if (overflow != 0)
        {
            PyErr_SetString(PyExc_OverflowError, "write_signed value does not fit into signed 64-bit range");
            return NULL;
        }
        outValue = (int64_t) signedValue;
    }

    const int8_t rc = dsdl_runtime_set_ixx(dst, (size_t) dstSize, (size_t) offBits, outValue, lenBits);
    if (rc < 0)
    {
        PyErr_SetString(PyExc_ValueError, "write_signed failed: serialisation buffer too small");
        return NULL;
    }

    return PyLong_FromLong(0);
}

static PyObject* py_read_unsigned(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          srcObj     = NULL;
    unsigned long long offBits    = 0;
    unsigned long long lenBitsRaw = 0;
    if (!PyArg_ParseTuple(args, "OKK", &srcObj, &offBits, &lenBitsRaw))
    {
        return NULL;
    }

    uint8_t lenBits = 0;
    if (!to_u8_len_bits(lenBitsRaw, &lenBits))
    {
        return NULL;
    }

    const uint8_t* src      = NULL;
    Py_ssize_t     srcSize  = 0;
    PyObject*      srcOwner = NULL;
    if (!get_readonly_bytes(srcObj, &src, &srcSize, &srcOwner))
    {
        return NULL;
    }

    const uint64_t out = dsdl_runtime_get_u64(src, (size_t) srcSize, (size_t) offBits, lenBits);
    Py_DECREF(srcOwner);
    return PyLong_FromUnsignedLongLong(out);
}

static PyObject* py_read_signed(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          srcObj     = NULL;
    unsigned long long offBits    = 0;
    unsigned long long lenBitsRaw = 0;
    if (!PyArg_ParseTuple(args, "OKK", &srcObj, &offBits, &lenBitsRaw))
    {
        return NULL;
    }

    uint8_t lenBits = 0;
    if (!to_u8_len_bits(lenBitsRaw, &lenBits))
    {
        return NULL;
    }

    const uint8_t* src      = NULL;
    Py_ssize_t     srcSize  = 0;
    PyObject*      srcOwner = NULL;
    if (!get_readonly_bytes(srcObj, &src, &srcSize, &srcOwner))
    {
        return NULL;
    }

    const int64_t out = dsdl_runtime_get_i64(src, (size_t) srcSize, (size_t) offBits, lenBits);
    Py_DECREF(srcOwner);
    return PyLong_FromLongLong((long long) out);
}

static PyObject* py_write_float(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          dstObj     = NULL;
    unsigned long long offBits    = 0;
    unsigned long long lenBitsRaw = 0;
    double             value      = 0.0;
    if (!PyArg_ParseTuple(args, "OKKd", &dstObj, &offBits, &lenBitsRaw, &value))
    {
        return NULL;
    }

    uint8_t*   dst     = NULL;
    Py_ssize_t dstSize = 0;
    if (!get_mutable_bytearray(dstObj, &dst, &dstSize))
    {
        return NULL;
    }

    int8_t rc = 0;
    if (lenBitsRaw == 16ULL)
    {
        rc = dsdl_runtime_set_f16(dst, (size_t) dstSize, (size_t) offBits, (float) value);
    }
    else if (lenBitsRaw == 32ULL)
    {
        rc = dsdl_runtime_set_f32(dst, (size_t) dstSize, (size_t) offBits, (float) value);
    }
    else if (lenBitsRaw == 64ULL)
    {
        rc = dsdl_runtime_set_f64(dst, (size_t) dstSize, (size_t) offBits, value);
    }
    else
    {
        PyErr_SetString(PyExc_ValueError, "unsupported float bit length");
        return NULL;
    }

    if (rc < 0)
    {
        PyErr_SetString(PyExc_ValueError, "write_float failed: serialisation buffer too small");
        return NULL;
    }
    return PyLong_FromLong(0);
}

static PyObject* py_read_float(PyObject* self, PyObject* args)
{
    (void) self;
    PyObject*          srcObj     = NULL;
    unsigned long long offBits    = 0;
    unsigned long long lenBitsRaw = 0;
    if (!PyArg_ParseTuple(args, "OKK", &srcObj, &offBits, &lenBitsRaw))
    {
        return NULL;
    }

    const uint8_t* src      = NULL;
    Py_ssize_t     srcSize  = 0;
    PyObject*      srcOwner = NULL;
    if (!get_readonly_bytes(srcObj, &src, &srcSize, &srcOwner))
    {
        return NULL;
    }

    double out = 0.0;
    if (lenBitsRaw == 16ULL)
    {
        out = (double) dsdl_runtime_get_f16(src, (size_t) srcSize, (size_t) offBits);
    }
    else if (lenBitsRaw == 32ULL)
    {
        out = (double) dsdl_runtime_get_f32(src, (size_t) srcSize, (size_t) offBits);
    }
    else if (lenBitsRaw == 64ULL)
    {
        out = dsdl_runtime_get_f64(src, (size_t) srcSize, (size_t) offBits);
    }
    else
    {
        Py_DECREF(srcOwner);
        PyErr_SetString(PyExc_ValueError, "unsupported float bit length");
        return NULL;
    }

    Py_DECREF(srcOwner);
    return PyFloat_FromDouble(out);
}

static PyMethodDef ModuleMethods[] = {
    {"byte_length_for_bits", py_byte_length_for_bits, METH_VARARGS, "Returns ceil(bit_count / 8)."},
    {"set_bit", py_set_bit, METH_VARARGS, "Sets one bit in a destination buffer."},
    {"get_bit", py_get_bit, METH_VARARGS, "Reads one bit from a source buffer."},
    {"copy_bits", py_copy_bits, METH_VARARGS, "Copies bit ranges between buffers."},
    {"extract_bits", py_extract_bits, METH_VARARGS, "Extracts bit ranges into a fresh bytes object."},
    {"write_unsigned", py_write_unsigned, METH_VARARGS, "Writes an unsigned integer fragment."},
    {"write_signed", py_write_signed, METH_VARARGS, "Writes a signed integer fragment."},
    {"read_unsigned", py_read_unsigned, METH_VARARGS, "Reads an unsigned integer fragment."},
    {"read_signed", py_read_signed, METH_VARARGS, "Reads a signed integer fragment."},
    {"write_float", py_write_float, METH_VARARGS, "Writes a floating-point fragment."},
    {"read_float", py_read_float, METH_VARARGS, "Reads a floating-point fragment."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef ModuleDef = {
    PyModuleDef_HEAD_INIT,
    "_dsdl_runtime_accel",
    "llvmdsdl CPython runtime accelerator module",
    -1,
    ModuleMethods,
    NULL,
    NULL,
    NULL,
    NULL,
};

PyMODINIT_FUNC PyInit__dsdl_runtime_accel(void)
{
    PyObject* module = PyModule_Create(&ModuleDef);
    if (module == NULL)
    {
        return NULL;
    }
    if (PyModule_AddStringConstant(module, "BACKEND", "accel") < 0)
    {
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
