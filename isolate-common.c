#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <error.h>
#include <limits.h>

#include "isolate.h"

#include <stdarg.h>

int use_syslog = 0;

static void
myerror_default_progname(char **out)
{
	xasprintf(out, "%s: ", program_invocation_short_name);
}

void
    __attribute__((format(printf, 3, 4)))
    myerror(const int exitnum, const int errnum, const char *fmt, ...)
{
	va_list ap, ap1;
	size_t sz = 0;
	size_t msgsz = 0;
	char *msg = NULL;

	if (!myerror_progname)
		myerror_progname = myerror_default_progname;

	myerror_progname(&msg);
	msgsz = strlen(msg) * sizeof(char);

	va_start(ap, fmt);

	va_copy(ap1, ap);
	sz = (size_t) vsnprintf(NULL, 0, fmt, ap1) + 1;
	va_end(ap1);

	msg = realloc(msg, msgsz + sz);
	if (!msg)
		error(EXIT_FAILURE, errno, "realloc: allocating %lu bytes", msgsz + sz);

	vsnprintf(msg + msgsz, sz, fmt, ap);
	va_end(ap);

	msgsz += sz;

	if (errnum > 0) {
		char *s = strerror(errnum);
		sz = strlen(s) * sizeof(char);

		msg = realloc(msg, msgsz + sz + 2);
		if (!msg)
			error(EXIT_FAILURE, errno, "realloc: allocating %lu bytes", msgsz + sz);

		strcpy(msg + msgsz - 1, ": ");
		strcpy(msg + msgsz - 1 + 2, s);

		msgsz += sz + 2;

		msg[msgsz] = '\0';
	}

	if (use_syslog)
		syslog(LOG_INFO, "%s", msg);
	else
		fprintf(stderr, "%s\n", msg);

	xfree(msg);

	if (errnum > 0)
		exit(exitnum);
}

void *
xmalloc(size_t size)
{
	void *r = malloc(size);

	if (!r)
		myerror(EXIT_FAILURE, errno, "malloc: allocating %lu bytes",
		        (unsigned long) size);
	return r;
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void *r = calloc(nmemb, size);

	if (!r)
		myerror(EXIT_FAILURE, errno, "calloc: allocating %lu*%lu bytes",
		        (unsigned long) nmemb, (unsigned long) size);
	return r;
}

void *
xrealloc(void *ptr, size_t nmemb, size_t elem_size)
{
	if (nmemb && ULONG_MAX / nmemb < elem_size)
		myerror(EXIT_FAILURE, 0, "realloc: nmemb*size > ULONG_MAX");

	size_t size = nmemb * elem_size;
	void *r = realloc(ptr, size);

	if (!r)
		myerror(EXIT_FAILURE, errno, "realloc: allocating %lu*%lu bytes",
		        (unsigned long) nmemb, (unsigned long) elem_size);
	return r;
}

char *
xstrdup(const char *s)
{
	size_t len = strlen(s);
	char *r = xmalloc(len + 1);

	memcpy(r, s, len + 1);
	return r;
}

int __attribute__((__format__(__printf__, 2, 3)))
__attribute__((__nonnull__(2)))
xasprintf(char **ptr, const char *fmt, ...)
{
	int ret;
	va_list arg;

	va_start(arg, fmt);
	if ((ret = vasprintf(ptr, fmt, arg)) < 0)
		myerror(EXIT_FAILURE, errno, "vasprintf");
	va_end(arg);

	return ret;
}

void *
xfree(void *ptr)
{
	if (ptr)
		free(ptr);
	return NULL;
}
