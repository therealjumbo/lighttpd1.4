#include "first.h"

#include "array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute_cold__
static data_unset *data_integer_copy(const data_unset *s) {
	data_integer *src = (data_integer *)s;
	data_integer *ds = data_integer_init();

	if (!buffer_is_empty(&src->key)) buffer_copy_buffer(&ds->key, &src->key);
	ds->value = src->value;
	return (data_unset *)ds;
}

static void data_integer_free(data_unset *d) {
	data_integer *ds = (data_integer *)d;

	free(ds->key.ptr);

	free(d);
}

__attribute_cold__
static int data_integer_insert_dup(data_unset *dst, data_unset *src) {
	UNUSED(dst);

	src->fn->free(src);

	return 0;
}

__attribute_cold__
static void data_integer_print(const data_unset *d, int depth) {
	data_integer *ds = (data_integer *)d;
	UNUSED(depth);

	fprintf(stdout, "%d", ds->value);
}


data_integer *data_integer_init(void) {
	static const struct data_methods fn = {
		data_integer_copy,
		data_integer_free,
		data_integer_insert_dup,
		data_integer_print,
	};
	data_integer *ds;

	ds = calloc(1, sizeof(*ds));
	force_assert(NULL != ds);

	ds->value = 0;

	ds->type = TYPE_INTEGER;
	ds->fn = &fn;

	return ds;
}
