#include <stdio.h>
#include "test.h"

int TEST_PRINT_PASS = 0;
int TEST_PRINT_FAIL = 1;

static int __STATUS     = 0;
static int __TESTS      = 0;
static int __ASSERTIONS = 0;
static int __FAILURES   = 0;

/**********************************************************/

static inline void __test_failed(void)
{
	++__FAILURES;
	__STATUS = 0;
}

static int num_test_suites = 0;
static struct test_suite *test_suites = NULL;

/**********************************************************/

int add_test_suite(const char *name, test_suite_f runner, int active)
{
	struct test_suite *ts;

	num_test_suites++;
	test_suites = realloc(test_suites, (num_test_suites * sizeof(struct test_suite)));
	if (!test_suites) {
		fprintf(stderr, "Failed to register suite '%s'\n", name);
		exit(99);
	}
	ts = test_suites + (num_test_suites - 1);
	ts->name   = name;
	ts->runner = runner;
	ts->active = active;
	return 0;
}

int activate_test(const char *name)
{
	int i;

	for (i = 0; i < num_test_suites; i++) {
		if (strcmp(test_suites[i].name, name) == 0) {
			test_suites[i].active = 1;
			return 1;
		}
	}
	return 0;
}

int run_active_tests(void)
{
	int i;

	for (i = 0; i < num_test_suites; i++) {
		if (test_suites[i].active) {
			(*(test_suites[i].runner))();
		}
	}
	return 0;
}

int run_all_tests(void)
{
	int i;

	for (i = 0; i < num_test_suites; i++) {
		(*(test_suites[i].runner))();
	}
	return 0;
}

void test(const char *s)
{
	__STATUS = 1;
	++__TESTS;
	printf("%s\n", s);
}

int test_status(void)
{
	printf("\n"
	       "--------------------\n"
	       "TEST RESULTS SUMMARY\n"
	       "--------------------\n");
	printf("%4i test(s)\n"
	       "%4i assertion(s)\n"
	       "\n"
	       "%4i FAILURE(S)\n",
	       __TESTS, __ASSERTIONS, __FAILURES);

	return __FAILURES;
}

int test_setup(int argc, char **argv)
{
	/* only works with NULL-terminated argv */
	while (*(++argv)) {
		if (strcmp(*argv, "-v") == 0) {
			TEST_PRINT_PASS = 1;
			TEST_PRINT_FAIL = 1;
		} else if (strcmp(*argv, "-q") == 0) {
			TEST_PRINT_PASS = 0;
			TEST_PRINT_FAIL = 0;
		}
	}
	return 0;
}

/** ASSERTIONS **/

void assert_fail(const char *s)
{
	++__ASSERTIONS;
	__test_failed();
	if (TEST_PRINT_FAIL) { printf(" - %s: FAIL\n", s); }
}

void assert_pass(const char *s)
{
	++__ASSERTIONS;
	if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
}

void assert_true(const char *s, int value)
{
	++__ASSERTIONS;
	(value ? assert_pass(s) : assert_fail(s));
}

void assert_false(const char *s, int value)
{
	assert_true(s, !value);
}

void assert_not_null(const char *s, const void *ptr)
{
	++__ASSERTIONS;
	if (ptr != NULL) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %p is NULL\n", s, ptr);
		}
	}
}

void assert_null(const char *s, const void *ptr)
{
	++__ASSERTIONS;
	if (ptr == NULL) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %p is not NULL\n", s, ptr);
		}
	}
}

#define _assert_numeric_equals(s, fmt, expected, actual) do {\
	++__ASSERTIONS; \
	if (expected == actual) { \
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); } \
	} else { \
		__test_failed(); \
		if (TEST_PRINT_FAIL) { \
			printf(" - %s: FAIL:\n" \
			       "\t\texpected " fmt "\n" \
			       "\t\t but got " fmt "\n", \
			       s, expected, actual); \
		} \
	} \
} while (0)

void assert_unsigned_equals(const char *s, unsigned long int expected, unsigned long int actual)
{
	_assert_numeric_equals(s, "%lu", expected, actual);
}

void assert_signed_equals(const char *s, signed long int expected, signed long int actual)
{
	_assert_numeric_equals(s, "%li", expected, actual);
}

void assert_ptr(const char *s, const void *expected, const void *actual)
{
	_assert_numeric_equals(s, "0x%lx", (unsigned long)expected, (unsigned long)actual);
}

void assert_ptr_ne(const char *s, const void *unexpected, const void *actual)
{
	++__ASSERTIONS;
	if (actual != unexpected) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL:\n"
			       "\t\t0x%lx == 0x%lx",
			       s, (unsigned long)unexpected, (unsigned long)actual);
		}
	}
}

void assert_int_equals(const char *s, int expected, int actual)
{
	++__ASSERTIONS;
	if (expected == actual) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i != %i\n", s, actual, expected);
		}
	}
}

void assert_int_not_equal(const char *s, int actual, int unexpected)
{
	++__ASSERTIONS;
	if (unexpected == actual) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i == %i\n", s, actual, unexpected);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}

void assert_int_greater_than(const char *s, int actual, int threshold)
{
	++__ASSERTIONS;
	if (actual <= threshold) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i <= %i\n", s, actual, threshold);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}

void assert_int_greater_than_or_equal(const char *s, int actual, int threshold)
{
	++__ASSERTIONS;
	if (actual < threshold) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i < %i\n", s, actual, threshold);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}

void assert_int_less_than(const char *s, int actual, int threshold)
{
	++__ASSERTIONS;
	if (actual >= threshold) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i >= %i\n", s, actual, threshold);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}

void assert_int_less_than_or_equal(const char *s, int actual, int threshold)
{
	++__ASSERTIONS;
	if (actual > threshold) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %i > %i\n", s, actual, threshold);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}

void assert_str_equals(const char *s, const char *actual, const char *expected)
{
	++__ASSERTIONS;
	if (expected == NULL) { expected = "(null)"; }
	if (actual == NULL)   { actual   = "(null)"; }

	if (strcmp(expected, actual) == 0) {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	} else {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL:\n\t\"%s\" !=\n\t\"%s\"\n", s, actual, expected);
		}
	}
}

void assert_str_not_equal(const char *s, const char *actual, const char *unexpected)
{
	++__ASSERTIONS;
	if (unexpected == NULL) { unexpected = "(null)"; }
	if (actual == NULL)     { actual     = "(null)"; }

	if (strcmp(unexpected, actual) == 0) {
		__test_failed();
		if (TEST_PRINT_FAIL) {
			printf(" - %s: FAIL: %s == %s\n", s, actual, unexpected);
		}
	} else {
		if (TEST_PRINT_PASS) { printf(" - %s: PASS\n", s); }
	}
}

