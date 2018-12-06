#include <linux/seccomp.h>
#include <sys/prctl.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <kafel.h>

#include "isolate.h"

extern int verbose;

void
load_seccomp(FILE *fd, const char *filename)
{
	struct sock_fprog prog;

	if (verbose > 1)
		info("applying seccomp-filter: %s", filename);

	kafel_ctxt_t ctx = kafel_ctxt_create();
	kafel_set_input_file(ctx, fd);

	if (kafel_compile(ctx, &prog))
		myerror(EXIT_FAILURE, errno, "policy compilation failed: %s", kafel_error_msg(ctx));

	kafel_ctxt_destroy(&ctx);

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) < 0)
		myerror(EXIT_FAILURE, errno, "prctl(PR_SET_SECCOMP)");

	xfree(prog.filter);
}
