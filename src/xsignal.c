#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <signal.h>

#include "xtypes.h"
#include "error.h"
#include "xsignal.h"

xsigfunc_t *xsignal(int signo, xsigfunc_t *func)
{
    struct sigaction act, oact;
    int n;

    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (signo == SIGALRM) {
#ifdef  SA_INTERRUPT
        act.sa_flags |= SA_INTERRUPT;   /* SunOS 4.x */
#endif
    } else {
#ifdef  SA_RESTART
        act.sa_flags |= SA_RESTART;     /* SVR4, 44BSD */
#endif
    }
    n = sigaction(signo, &act, &oact);
    if (n < 0)
        err_exit(TRUE, "sigaction");

    return (oact.sa_handler);
}