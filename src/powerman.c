/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Andrew Uselton <uselton2@llnl.gov>
 *  UCRL-CODE-2002-008.
 *  
 *  This file is part of PowerMan, a remote power management program.
 *  For details, see <http://www.llnl.gov/linux/powerman/>.
 *  
 *  PowerMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  PowerMan is distributed in the hope that it will be useful, but WITHOUT 
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 *  for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with PowerMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#if HAVE_GENDERS_H
#include <genders.h>
#endif
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdarg.h>

#include "powerman.h"
#include "xmalloc.h"
#include "xtypes.h"
#include "xread.h"
#include "error.h"
#include "hostlist.h"
#include "client_proto.h"
#include "debug.h"
#include "argv.h"
#include "xpty.h"
#include "hprintf.h"
#include "argv.h"
#include "list.h"

#define CMD_MAGIC 0x5565aafd
typedef struct {
    int magic;
    char *fmt;
    char **argv;
    char *sendstr;
} cmd_t;

#if WITH_GENDERS
static void _push_genders_hosts(hostlist_t targets, char *s);
#endif
static int _connect_to_server_tcp(char *host, char *port);
static int _connect_to_server_pipe(char *server_path, char *config_path);
static void _usage(void);
static void _license(void);
static void _version(void);
static void _getline(int fd, char *str, int len);
static int _process_line(int fd);
static void _expect(int fd, char *str);
static int _process_response(int fd);
static void _process_version(int fd);
static void _cmd_create(List cl, char *fmt, char *arg, bool prepend);
static void _cmd_destroy(cmd_t *cp);
static void _cmd_append(cmd_t *cp, char *arg);
static void _cmd_prepare(cmd_t *cp, bool genders);
static int _cmd_execute(cmd_t *cp, int fd);
static void _cmd_print(cmd_t *cp);

static char *prog;

#define OPTIONS "0:1:c:r:f:u:B:blQ:qN:nP:tD:dTxgh:S:C:VLZ"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long(ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"on",          required_argument,  0, '1'},
    {"off",         required_argument,  0, '0'},
    {"cycle",       required_argument,  0, 'c'},
    {"reset",       required_argument,  0, 'r'},
    {"flash",       required_argument,  0, 'f'},
    {"unflash",     required_argument,  0, 'u'},
    {"beacon",      required_argument,  0, 'B'},
    {"beacon-all",  no_argument,        0, 'b'},
    {"list",        no_argument,        0, 'l'},
    {"query",       required_argument,  0, 'Q'},
    {"query-all",   no_argument,        0, 'q'},
    {"soft",        required_argument,  0, 'N'},
    {"soft-all",    no_argument,        0, 'n'},
    {"temp",        required_argument,  0, 'P'},
    {"temp-all",    no_argument,        0, 't'},
    {"device",      required_argument,  0, 'D'},
    {"device-all",  no_argument,        0, 'd'},
    {"telemetry",   no_argument,        0, 'T'},
    {"exprange",    no_argument,        0, 'x'},
    {"genders",     no_argument,        0, 'g'},
    {"server-host", required_argument,  0, 'h'},
    {"server-path", required_argument,  0, 'S'},
    {"config-path", required_argument,  0, 'C'},
    {"version",     no_argument,        0, 'V'},
    {"license",     no_argument,        0, 'L'},
    {"dump-cmds",   no_argument,        0, 'Z'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt(ac,av,opt)
#endif

int main(int argc, char **argv)
{
    int c;
    int res = 0;
    int server_fd;
    char *port = DFLT_PORT;
    char *host = DFLT_HOSTNAME;
    bool genders = FALSE;
    bool dumpcmds = FALSE;
    char *server_path = NULL;
    char *config_path = NULL;
    List commands;  /* list-o-cmd_t's */
    ListIterator itr;
    cmd_t *cp; 

    prog = basename(argv[0]);
    err_init(prog);
    commands = list_create((ListDelF)_cmd_destroy);

    /* Parse options.
     */
    opterr = 0;
    while ((c = GETOPT(argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
        case '1':              /* --on */
            _cmd_create(commands, CP_ON, optarg, FALSE);
            break;
        case '0':              /* --off */
            _cmd_create(commands, CP_OFF, optarg, FALSE);
            break;
        case 'c':              /* --cycle */
            _cmd_create(commands, CP_CYCLE, optarg, FALSE);
            break;
        case 'r':              /* --reset */
            _cmd_create(commands, CP_RESET, optarg, FALSE);
            break;
        case 'l':              /* --list */
            _cmd_create(commands, CP_NODES, NULL, FALSE);
            break;
        case 'Q':              /* --query */
            _cmd_create(commands, CP_STATUS, optarg, FALSE);
            break;
        case 'q':              /* --query-all */
            _cmd_create(commands, CP_STATUS_ALL, NULL, FALSE);
            break;
        case 'f':              /* --flash */
            _cmd_create(commands, CP_BEACON_ON, optarg, FALSE);
            break;
        case 'u':              /* --unflash */
            _cmd_create(commands, CP_BEACON_OFF, optarg, FALSE);
            break;
        case 'B':              /* --beacon */
            _cmd_create(commands, CP_BEACON, optarg, FALSE);
            break;
        case 'b':              /* --beacon-all */
            _cmd_create(commands, CP_BEACON_ALL, NULL, FALSE);
            break;
        case 'N':              /* --node */
            _cmd_create(commands, CP_SOFT, optarg, FALSE);
            break;
        case 'n':              /* --node-all */
            _cmd_create(commands, CP_SOFT_ALL, NULL, FALSE);
            break;
        case 'P':              /* --temp */
            _cmd_create(commands, CP_TEMP, optarg, FALSE);
            break;
        case 't':              /* --temp-all */
            _cmd_create(commands, CP_TEMP_ALL, NULL, FALSE);
            break;
        case 'D':              /* --device */
            _cmd_create(commands, CP_DEVICE, optarg, FALSE);
            break;
        case 'd':              /* --device-all */
            _cmd_create(commands, CP_DEVICE_ALL, NULL, FALSE);
            break;
        case 'h':              /* --server-host host[:port] */
            if ((port = strchr(optarg, ':')))
                *port++ = '\0';  
            host = optarg;
            break;
        case 'L':              /* --license */
            _license();
            /*NOTREACHED*/
            break;
        case 'V':              /* --version */
            _version();
            /*NOTREACHED*/
            break;
        case 'T':              /* --telemetry */
            _cmd_create(commands, CP_TELEMETRY, NULL, TRUE);
            break;
        case 'x':              /* --exprange */
            _cmd_create(commands, CP_EXPRANGE, NULL, TRUE);
            break;
        case 'g':              /* --genders */
#if WITH_GENDERS
            genders = TRUE;
#else
            err_exit(FALSE, "not configured with genders support");
#endif
            break;
        case 'S':              /* --server-path */
            server_path = optarg;
            break;
        case 'C':              /* --config-path */
            config_path = optarg;
            break;
        case 'Z':              /* --dump-cmds */
            dumpcmds = TRUE;
            break;
        default:
            _usage();
            /*NOTREACHED*/
            break;
        }
    }
    if (list_is_empty(commands))
        _usage();

    /* For backwards compat with powerman 2.0 and earlier,
     * if there is only one command, any additional arguments are more targets.
     */
    if (optind < argc) {
        if (list_count(commands) > 1)
            err_exit(FALSE, "error: multiple commands + dangling target args");
        cp = list_peek(commands); 
        while (optind < argc) {
            _cmd_append(cp, argv[optind]);
            optind++;
        }
    }

    /* Prepare commands for processing.
     */
    itr = list_iterator_create(commands);
    while ((cp = list_next(itr)))
        _cmd_prepare(cp, genders);
    list_iterator_destroy(itr);

    /* Dump commands and exit if requested.
     */
    if (dumpcmds) {
        itr = list_iterator_create(commands);
        while ((cp = list_next(itr)))
            _cmd_print(cp);
        list_iterator_destroy(itr);
        exit(0);
    }

    /* Establish connection to server and start protocol.
     */
    if (server_path)
        server_fd = _connect_to_server_pipe(server_path, config_path);
    else
        server_fd = _connect_to_server_tcp(host, port);
    _process_version(server_fd);
    _expect(server_fd, CP_PROMPT);

    /* Execute the commands.
     */
    itr = list_iterator_create(commands);
    while ((cp = list_next(itr))) {
        res = _cmd_execute(cp, server_fd);
        if (res != 0)
            break;
    }
    list_iterator_destroy(itr);
    list_destroy(commands);

    /* Disconnect from server.
     */
    hfdprintf(server_fd, "%s%s", CP_QUIT, CP_EOL);
    _expect(server_fd, CP_RSP_QUIT);

    exit(res);
}

/* Display powerman usage and exit.
 */
static void _usage(void)
{
    printf("Usage: %s [action] [targets]\n", prog);
    printf("-1,--on targets      Power on targets\n");
    printf("-0,--off targets     Power off targets\n");
    printf("-c,--cycle targets   Power cycle targets\n");
    printf("-q,--query-all       Query power state of all targets\n");
    printf("-Q,--query targets   Query power state of specific targets\n");
    exit(1);
}

/* Display powerman license and exit.
 */
static void _license(void)
{
    printf(
 "Copyright (C) 2001-2008 The Regents of the University of California.\n"
 "Produced at Lawrence Livermore National Laboratory.\n"
 "Written by Andrew Uselton <uselton2@llnl.gov>.\n"
 "http://www.llnl.gov/linux/powerman/\n"
 "UCRL-CODE-2002-008\n\n"
 "PowerMan is free software; you can redistribute it and/or modify it\n"
 "under the terms of the GNU General Public License as published by\n"
 "the Free Software Foundation.\n");
    exit(1);
}

/* Display powerman version and exit.
 */
static void _version(void)
{
    printf("%s\n", VERSION);
    exit(1);
}

#if WITH_GENDERS
static void _push_genders_hosts(hostlist_t targets, char *s)
{
    genders_t g;
    char **nodes;
    int len, n, i;

    if (strlen(s) == 0)
        return;
    if (!(g = genders_handle_create()))
        err_exit(FALSE, "genders_handle_create failed");
    if (genders_load_data(g, NULL) < 0)
        err_exit(FALSE, "genders_load_data: %s", genders_errormsg(g));
    if ((len = genders_nodelist_create(g, &nodes)) < 0)
        err_exit(FALSE, "genders_nodelist_create: %s", genders_errormsg(g));
    if ((n = genders_query(g, nodes, len, s)) < 0)
        err_exit(FALSE, "genders_query: %s", genders_errormsg(g));
    genders_handle_destroy(g);
    if (n == 0)
        err_exit(FALSE, "genders expression did not match any nodes");
    for (i = 0; i < n; i++) {
        if (!hostlist_push(targets, nodes[i]))
            err_exit(FALSE, "hostlist error");
    }
}
#endif

static void _cmd_create(List cl, char *fmt, char *arg, bool prepend)
{
    cmd_t *cp = (cmd_t *)xmalloc(sizeof(cmd_t));

    cp->magic = CMD_MAGIC;
    cp->fmt = fmt;
    cp->argv = NULL;
    cp->sendstr = NULL;
    if (arg)
        cp->argv = argv_create(arg, "");
    if (prepend)
        list_prepend(cl, cp);
    else
        list_append(cl, cp);
}

static void _cmd_destroy(cmd_t *cp)
{
    assert(cp->magic == CMD_MAGIC);

    cp->magic = 0;
    if (cp->sendstr)
        xfree(cp->sendstr);
    if (cp->argv)
        argv_destroy(cp->argv);
    xfree(cp);
}

static void _cmd_append(cmd_t *cp, char *arg)
{
    assert(cp->magic == CMD_MAGIC);
    if (cp->argv == NULL)
        err_exit(FALSE, "option takes no arguments");
    cp->argv = argv_append(cp->argv, arg);
}

static void _cmd_prepare(cmd_t *cp, bool genders)
{
    char tmpstr[CP_LINEMAX];
    hostlist_t hl;
    int i;

    assert(cp->magic == CMD_MAGIC);
    assert(cp->sendstr == NULL);

    tmpstr[0] = '\0';
    if (cp->argv) {
        hl = hostlist_create(NULL);
        for (i = 0; i < argv_length(cp->argv); i++) {
            if (genders) {
#if WITH_GENDERS
                _push_genders_hosts(hl, cp->argv[i]);
#endif
            } else {
                if (hostlist_push(hl, cp->argv[i]) == 0)
                    err_exit(FALSE, "hostlist error");
            }
        }
        if (hostlist_ranged_string(hl, sizeof(tmpstr), tmpstr) == -1)
            err_exit(FALSE, "hostlist error");
        hostlist_destroy(hl);
    }
    cp->sendstr = hsprintf(cp->fmt, tmpstr);
}

static int _cmd_execute(cmd_t *cp, int fd)
{
    int res;

    assert(cp->magic == CMD_MAGIC);
    assert(cp->sendstr != NULL);

    hfdprintf(fd, "%s%s", cp->sendstr, CP_EOL);
    res = _process_response(fd);
    _expect(fd, CP_PROMPT);

    return res;
}

static void _cmd_print(cmd_t *cp)
{
    assert(cp->magic == CMD_MAGIC);
    assert(cp->sendstr != NULL);

    printf("%s%s", cp->sendstr, CP_EOL);
}

static int _connect_to_server_pipe(char *server_path, char *config_path)
{
    int saved_stderr;
    char cmd[128];
    char **argv;
    pid_t pid;
    int fd;

    saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr < 0)
        err_exit(TRUE, "dup stderr");
    snprintf(cmd, sizeof(cmd), "powermand -sRf -c %s", config_path);
    argv = argv_create(cmd, "");
    pid = xforkpty(&fd, NULL, 0);
    switch (pid) {
        case -1:
            err_exit(TRUE, "forkpty error");
        case 0: /* child */
            if (dup2(saved_stderr, STDERR_FILENO) < 0)
                err_exit(TRUE, "dup2 stderr");
            xcfmakeraw(STDIN_FILENO);
            execv(server_path, argv);
            err_exit(TRUE, "exec %s", server_path);
        default: /* parent */ 
            break;
    }
    argv_destroy(argv);
    return fd;
}

static int _connect_to_server_tcp(char *host, char *port)
{
    struct addrinfo hints, *addrinfo;
    int n;
    int fd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((n = getaddrinfo(host, port, &hints, &addrinfo)) != 0)
        err_exit(FALSE, "getaddrinfo %s: %s", host, gai_strerror(n));

    fd = socket(addrinfo->ai_family, addrinfo->ai_socktype,
                       addrinfo->ai_protocol);
    if (fd < 0)
        err_exit(TRUE, "socket");

    if (connect(fd, addrinfo->ai_addr, addrinfo->ai_addrlen) < 0)
        err_exit(TRUE, "connect");
    freeaddrinfo(addrinfo);
    return fd;
}


/* Return true if response should be suppressed.
 */
static bool _supress(int num)
{
    char *ignoreme[] = { CP_RSP_QRY_COMPLETE, CP_RSP_TELEMETRY, 
        CP_RSP_EXPRANGE };
    bool res = FALSE;
    int i;

    for (i = 0; i < sizeof(ignoreme)/sizeof(char *); i++) {
        if (strtol(ignoreme[i], (char **) NULL, 10) == num) {
            res = TRUE;
            break;
        }
    }

    return res;
}

/* Read a line of data terminated with \r\n or just \n.
 */
static void _getline(int fd, char *buf, int size)
{
    while (size > 1) {          /* leave room for terminating null */
        if (xread(fd, buf, 1) <= 0)
            err_exit(TRUE, "lost connection with server");
        if (*buf == '\r')
            continue;
        if (*buf == '\n')
            break;
        size--;
        buf++;
    }
    *buf = '\0';
}

/* Get a line from the socket and display on stdout.
 * Return the numerical portion of the repsonse.
 */
static int _process_line(int fd)
{
    char buf[CP_LINEMAX];
    long int num;

    _getline(fd, buf, CP_LINEMAX);

    num = strtol(buf, (char **) NULL, 10);
    if (num == LONG_MIN || num == LONG_MAX)
        num = -1;
    if (strlen(buf) > 4) {
        if (!_supress(num))
            printf("%s\n", buf + 4);
    } else
        err_exit(FALSE, "unexpected response from server");
    return num;
}

/* Read version and warn if it doesn't match the client's.
 */
static void _process_version(int fd)
{
    char buf[CP_LINEMAX], vers[CP_LINEMAX];

    _getline(fd, buf, CP_LINEMAX);
    if (sscanf(buf, CP_VERSION, vers) != 1)
        err_exit(FALSE, "unexpected response from server");
    if (strcmp(vers, VERSION) != 0)
        err(FALSE, "warning: server version (%s) != client (%s)",
                vers, VERSION);
}

static int _process_response(int fd)
{
    int num;

    do {
        num = _process_line(fd);
    } while (!CP_IS_ALLDONE(num));
    return (CP_IS_FAILURE(num) ? num : 0);
}

/* Read strlen(str) bytes from file descriptor and exit if
 * it doesn't match 'str'.
 */
static void _expect(int fd, char *str)
{
    char buf[CP_LINEMAX];
    int len = strlen(str);
    char *p = buf;
    int res;

    assert(len < sizeof(buf));
    do {
        res = xread(fd, p, len);
        if (res < 0)
            err_exit(TRUE, "lost connection with server");
        p += res;
        *p = '\0';
        len -= res;
    } while (strcmp(str, buf) != 0 && len > 0);

    /* Shouldn't happen.  We are not handling the general case of the server
     * returning the wrong response.  Read() loop above may hang in that case.
     */
    if (strcmp(str, buf) != 0)
        err_exit(FALSE, "unexpected response from server");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
