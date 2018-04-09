#include <linux/seccomp.h>
#include <sys/prctl.h>

#include <stdlib.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>

#include <kafel.h>

#include "isolate.h"

void
load_seccomp(FILE *fd)
{
	struct sock_fprog prog;
	kafel_ctxt_t ctx = kafel_ctxt_create();

	kafel_set_input_file(ctx, fd);

	if (kafel_compile(ctx, &prog))
		error(EXIT_FAILURE, errno, "policy compilation failed: %s", kafel_error_msg(ctx));

	kafel_ctxt_destroy(&ctx);

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) < 0)
		error(EXIT_FAILURE, errno, "prctl(PR_SET_SECCOMP)");

	xfree(prog.filter);
}
