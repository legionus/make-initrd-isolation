#include <sys/prctl.h>
#include <sys/capability.h>
#include <sys/types.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>

#include "isolate.h"

extern int verbose;

static struct {
	const char *name;
	const cap_flag_t value;
} const cflags[] = {
	{ "effective", CAP_EFFECTIVE },
	{ "inheritable", CAP_INHERITABLE },
	{ "permitted", CAP_PERMITTED },
	{ NULL, 0 }
};

static int
cap_change_flag(cap_t caps, const char *capname, const cap_flag_value_t value)
{
	int i;
	cap_value_t cap;

	if (!strncasecmp("all", capname, 3)) {
		if (value == CAP_CLEAR) {
			if (verbose > 2)
				info("unset all capabilities");
			return cap_clear(caps);
		}

		if (verbose > 2)
			info("set all capabilities");

		for (cap = CAP_LAST_CAP; cap >= 0; cap--) {
			for (i = 0; cflags[i].name; i++) {
				if (!cap_set_flag(caps, cflags[i].value, 1, &cap, CAP_SET))
					continue;

				char *cname = cap_to_name(cap);
				info("unable to SET capability (%s): %s", cflags[i].name, cname);
				cap_free(cname);
			}
		}

		return 0;
	}

	if (cap_from_name(capname, &cap) < 0) {
		info("unknown capability: %s", capname);
		return -1;
	}

	if (verbose > 2) {
		char *cname = cap_to_name(cap);
		info("%s capability %s", (value == CAP_CLEAR ? "unset" : "set"), cname);
		cap_free(cname);
	}

	for (i = 0; cflags[i].name; i++) {
		if (cap_set_flag(caps, cflags[i].value, 1, &cap, value) < 0) {
			char *cname = cap_to_name(cap);
			info("unable to %s capability (%s): %s",
			        (value == CAP_CLEAR ? "unset" : "set"), cflags[i].name, cname);
			cap_free(cname);
			return -1;
		}
	}

	return 0;
}

int
cap_parse_arg(cap_t *caps, char *arg, const cap_flag_value_t value)
{
	int i = 0, token_offset = 0;
	char *str, *token, *saveptr;

	if (!*caps)
		*caps = cap_get_proc();

	for (i = 1, str = arg;; i++, str = NULL) {
		if (!(token = strtok_r(str, ",", &saveptr)))
			break;

		if (strlen(token) <= 0)
			continue;

		token_offset = 0;

		while (token[token_offset] && isspace(token[token_offset]))
			token_offset++;

		if (cap_change_flag(*caps, token + token_offset, value) < 0)
			return -1;
	}

	return 0;
}

int
cap_parse_capsset(cap_t *caps, char *arg)
{
	int i = 0, token_offset = 0;
	char *str, *token, *saveptr;
	cap_flag_value_t value = CAP_SET;

	if (!*caps)
		*caps = cap_get_proc();

	for (i = 1, str = arg;; i++, str = NULL) {
		if (!(token = strtok_r(str, ",", &saveptr)))
			break;

		if (strlen(token) <= 0)
			continue;

		token_offset = 0;

		while (token[token_offset] && isspace(token[token_offset]))
			token_offset++;

		switch (token[0]) {
			case '-':
				token_offset++;
				value = CAP_CLEAR;
				break;
			case '+':
				token_offset++;
				value = CAP_SET;
				break;
		}

		if (cap_change_flag(*caps, token + token_offset, value) < 0)
			return -1;
	}

	return 0;
}

void
apply_caps(cap_t caps)
{
	cap_value_t cap;

	if (!CAP_IS_SUPPORTED(CAP_SETFCAP))
		myerror(EXIT_FAILURE, 0, "the kernel does not support CAP_SETFCAP");

	for (cap = CAP_LAST_CAP; cap >= 0; cap--) {
		cap_flag_value_t v = CAP_CLEAR;

		if (cap_get_flag(caps, cap, CAP_EFFECTIVE, &v) < 0)
			myerror(EXIT_FAILURE, errno, "cap_get_flag(%d)", cap);

		if (v == CAP_SET)
			continue;

		if (prctl(PR_CAPBSET_DROP, cap, 0, 0) < 0)
			myerror(EXIT_FAILURE, errno, "prctl(PR_CAPBSET_DROP,%s)", cap_to_name(cap));
	}

	if (cap_free(caps) < 0)
		myerror(EXIT_FAILURE, errno, "cap_free");
}
