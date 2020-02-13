// Copyright 2019 - Omar Sandoval
// SPDX-License-Identifier: GPL-3.0+

#include "drgnpy.h"

static void StackTrace_dealloc(StackTrace *self)
{
	drgn_stack_trace_destroy(self->trace);
	Py_XDECREF(self->prog);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *StackTrace_str(StackTrace *self)
{
	struct drgn_error *err;
	PyObject *ret;
	char *str;

	err = drgn_pretty_print_stack_trace(self->trace, &str);
	if (err)
		return set_drgn_error(err);

	ret = PyUnicode_FromString(str);
	free(str);
	return ret;
}

static Py_ssize_t StackTrace_length(StackTrace *self)
{
	return drgn_stack_trace_num_frames(self->trace);
}

static StackFrame *StackTrace_item(StackTrace *self, Py_ssize_t i)
{
	StackFrame *ret;

	if (i < 0 || i >= drgn_stack_trace_num_frames(self->trace)) {
		PyErr_SetString(PyExc_IndexError,
				"stack frame index out of range");
		return NULL;
	}
	ret = (StackFrame *)StackFrame_type.tp_alloc(&StackFrame_type, 0);
	if (!ret)
		return NULL;

	ret->frame = drgn_stack_trace_get_frame(self->trace, i);
	ret->trace = self;
	Py_INCREF(self);
	return ret;
}

static PySequenceMethods StackTrace_as_sequence = {
	.sq_length = (lenfunc)StackTrace_length,
	.sq_item = (ssizeargfunc)StackTrace_item,
};

PyTypeObject StackTrace_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "_drgn.StackTrace",
	.tp_basicsize = sizeof(StackTrace),
	.tp_dealloc = (destructor)StackTrace_dealloc,
	.tp_as_sequence = &StackTrace_as_sequence,
	.tp_str = (reprfunc)StackTrace_str,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = drgn_StackTrace_DOC,
};

static void StackFrame_dealloc(StackFrame *self)
{
	Py_XDECREF(self->trace);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *StackFrame_symbol(StackFrame *self)
{
	struct drgn_error *err;
	struct drgn_symbol *sym;
	PyObject *ret;

	err = drgn_stack_frame_symbol(self->frame, &sym);
	if (err)
		return set_drgn_error(err);
	ret = Symbol_wrap(sym, self->trace->prog);
	if (!ret) {
		drgn_symbol_destroy(sym);
		return NULL;
	}
	return ret;
}

static PyObject *StackFrame_register(StackFrame *self, PyObject *arg)
{
	struct drgn_error *err;
	uint64_t value;

	if (PyUnicode_Check(arg)) {
		err = drgn_stack_frame_register_by_name(self->frame,
							PyUnicode_AsUTF8(arg),
							&value);
	} else {
		struct index_arg number = {};

		if (PyObject_TypeCheck(arg, &Register_type))
			arg = PyStructSequence_GET_ITEM(arg, 1);
		if (!index_converter(arg, &number))
			return NULL;
		err = drgn_stack_frame_register(self->frame, number.uvalue,
						&value);
	}
	if (err)
		return set_drgn_error(err);
	return PyLong_FromUnsignedLongLong(value);
}

static PyObject *StackFrame_registers(StackFrame *self)
{
	struct drgn_error *err;
	PyObject *dict;
	const struct drgn_platform *platform;
	size_t num_registers, i;

	dict = PyDict_New();
	if (!dict)
		return NULL;
	platform = drgn_program_platform(&self->trace->prog->prog);
	num_registers = drgn_platform_num_registers(platform);
	for (i = 0; i < num_registers; i++) {
		const struct drgn_register *reg;
		uint64_t value;
		PyObject *value_obj;
		int ret;

		reg = drgn_platform_register(platform, i);
		err = drgn_stack_frame_register(self->frame,
						drgn_register_number(reg),
						&value);
		if (err) {
			drgn_error_destroy(err);
			continue;
		}
		value_obj = PyLong_FromUnsignedLongLong(value);
		if (!value_obj) {
			Py_DECREF(dict);
			return NULL;
		}
		ret = PyDict_SetItemString(dict, drgn_register_name(reg),
					   value_obj);
		Py_DECREF(value_obj);
		if (ret == -1) {
			Py_DECREF(dict);
			return NULL;
		}
	}
	return dict;
}

static PyObject *StackFrame_get_pc(StackFrame *self, void *arg)
{
	return PyLong_FromUnsignedLongLong(drgn_stack_frame_pc(self->frame));
}

static PyObject *StackFrame_variables(StackFrame *self, PyObject *arg)
{
	struct drgn_error *err;
	const char *name = "test";
	int ret = 0;
	uint64_t value = 1234;

	err = drgn_stack_frame_variable(self->frame, PyUnicode_AsUTF8(arg),
					&name,
					&ret,
					&value);

	if (err) {
		return PyUnicode_FromString(name);
		//return PyLong_FromLongLong(ret);
	}

	return PyLong_FromLongLong(value);
	//return PyUnicode_FromString(name);
}

static Py_ssize_t StackFrame_length(StackFrame *self)
{
	struct drgn_error *err;
	size_t num_funcs;

	err = drgn_stack_frame_num_funcs(self->frame, &num_funcs);
	if (err)
		return -1;

	return num_funcs;
}

static StackFunc *StackFrame_item(StackFrame *self, Py_ssize_t i)
{
	struct drgn_error *err;
	struct drgn_stack_func *func;
	size_t num_funcs;
	StackFunc *ret;

	err = drgn_stack_frame_num_funcs(self->frame, &num_funcs);
	if (err)
		return NULL;

	if (i < 0 || i >= num_funcs) {
		PyErr_SetString(PyExc_IndexError,
				"stack func index out of range");
		return NULL;
	}
	ret = (StackFunc *)StackFunc_type.tp_alloc(&StackFunc_type, 0);
	if (!ret)
		return NULL;

	err = drgn_stack_frame_get_func(self->frame, i, &func);
	if (err)
		return NULL;

	ret->func = func;
	ret->trace = self->trace;
	Py_INCREF(self->trace);
	return ret;
}

static PySequenceMethods StackFrame_as_sequence = {
	.sq_length = (lenfunc)StackFrame_length,
	.sq_item = (ssizeargfunc)StackFrame_item,
};

static PyMethodDef StackFrame_methods[] = {
	{"symbol", (PyCFunction)StackFrame_symbol, METH_NOARGS,
	 drgn_StackFrame_symbol_DOC},
	{"register", (PyCFunction)StackFrame_register,
	 METH_O, drgn_StackFrame_register_DOC},
	{"registers", (PyCFunction)StackFrame_registers,
	 METH_NOARGS, drgn_StackFrame_registers_DOC},
	{"variables", (PyCFunction)StackFrame_variables, METH_O,
	drgn_StackFrame_registers_DOC},
	{},
};

static PyGetSetDef StackFrame_getset[] = {
	{"pc", (getter)StackFrame_get_pc, NULL, drgn_StackFrame_pc_DOC},
	{},
};

PyTypeObject StackFrame_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "_drgn.StackFrame",
	.tp_basicsize = sizeof(StackFrame),
	.tp_dealloc = (destructor)StackFrame_dealloc,
	.tp_as_sequence = &StackFrame_as_sequence,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = drgn_StackFrame_DOC,
	.tp_methods = StackFrame_methods,
	.tp_getset = StackFrame_getset,
};

static void StackFunc_dealloc(StackFunc *self)
{
	Py_XDECREF(self->trace);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *StackFunc_name(StackFunc *self)
{
	return PyUnicode_FromString("test");
}

static DrgnObject *StackFunc_subscript(StackFunc *self, PyObject *key)
{
	struct drgn_error *err;
	const char *name;
	bool clear;
	DrgnObject *ret;

	if (!PyUnicode_Check(key)) {
		PyErr_SetObject(PyExc_KeyError, key);
		return NULL;
	}

	name = PyUnicode_AsUTF8(key);
	if (!name)
		return NULL;

	ret = DrgnObject_alloc(self->trace->prog);
	if (!ret)
		return NULL;

	clear = set_drgn_in_python();
	err = drgn_stack_func_get_var(self->func, name, &ret->obj);
	if (clear)
		clear_drgn_in_python();

	if (err) {
		Py_DECREF(ret);
		set_drgn_error(err);
	} else {
		return ret;
	}

	return NULL;
	//return PyUnicode_FromString(name);
}

static PyMethodDef StackFunc_methods[] = {
	{"__getitem__", (PyCFunction)StackFunc_subscript, METH_O | METH_COEXIST,
	 drgn_StackFunc___getitem___DOC},
	{"name", (PyCFunction)StackFunc_name, METH_NOARGS,
	 drgn_StackFunc_name_DOC},
	{},
};

static PyMappingMethods StackFunc_as_mapping = {
	.mp_subscript = (binaryfunc)StackFunc_subscript,
};

PyTypeObject StackFunc_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "_drgn.StackFunc",
	.tp_basicsize = sizeof(StackFunc),
	.tp_dealloc = (destructor)StackFunc_dealloc,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = drgn_StackFunc_DOC,
	.tp_methods = StackFunc_methods,
	.tp_as_mapping = &StackFunc_as_mapping,
};
