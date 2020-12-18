/*************************************************************************
This file is part of dres the resource policy dependency resolver.

Copyright (C) 2010 Nokia Corporation.

This library is free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#define MAX_CMDARGS 32                      /* to dres as local variables */
#define REDO_LAST   "!"


/* console interface */
OHM_IMPORTABLE(int, console_open  , (char  *address,
                                     void (*opened)(int, struct sockaddr *,int),
                                     void (*closed)(int),
                                     void (*input)(int, char *, void *),
                                     void  *data, int multiple));
OHM_IMPORTABLE(int, console_close , (int id));
OHM_IMPORTABLE(int, console_grab  , (int id, int fd));
OHM_IMPORTABLE(int, console_ungrab, (int id, int fd));


/* console event handlers */
static void console_opened (int id, struct sockaddr *peer, int peerlen);
static void console_closed (int id);
static void console_input  (int id, char *buf, void *data);

static int parse_dres_args(char *input, char **args, int narg);

static command_t *find_command(char *name);

static void command_help   (int id, char *input);
static void command_dump   (int id, char *input);
static void command_set    (int id, char *input);
static void command_resolve(int id, char *input);
static void command_prolog (int id, char *input);
#if 0
static void command_query  (int id, char *input);
static void command_eval   (int id, char *input);
#endif
static void command_bye    (int id, char *input);
static void command_quit   (int id, char *input);
static void command_grab   (int id, char *input);
static void command_release(int id, char *input);
static void command_debug  (int id, char *input);
static void command_log    (int id, char *input);
static void command_statistics(int id, char *input);

typedef struct {
    char  *name;
    void (*handler)(char *);
} extension_t;

static void extension_init(void);
static void extension_exit(void);
static extension_t *extension_find(char *name);
static void         extension_run(int id, extension_t *ext, char *command);

static extension_t *extensions = NULL;
static int          nextension = 0;
static int          grabbed    = FALSE;



#define COMMAND(c, a, d) {                                      \
    name:       #c,                                             \
    args:        a,                                             \
    description: d,                                             \
    handler:     command_##c,                                   \
}
#define END { name: NULL }

static command_t commands[] = {
    COMMAND(help   , NULL       , "Get help on the available commands."     ),
    COMMAND(dump   , "[var]"    , "Dump a given or all factstore variables."),
    COMMAND(set    , "var value", "Set/change a given fact store variable." ),
    COMMAND(resolve, "[goal arg1=val1,...]", "Run the dependency resolver for a goal." ),
    COMMAND(prolog , NULL       , "Start an interactive prolog shell."      ),
    COMMAND(bye    , NULL       , "Close the resolver terminal session."    ),
    COMMAND(quit   , NULL       , "Close the resolver terminal session."    ),
    COMMAND(grab   , NULL       , "Grab stdout and stderr to this terminal."),
    COMMAND(release, NULL       , "Release any previous grabs."             ),
    COMMAND(debug  , "list|set|rule...", "Configure runtime debugging/tracing."  ),
    COMMAND(log    , "[+|-]{error,info,warning}", "Configure logging level."  ),
    COMMAND(statistics, NULL, "Print rule evaluation statistics."),
    END
};

static int console;
static char prefix[128];



/********************
 * console_init
 ********************/
static int
console_init(char *address)
{
    char *signature;
    
#define IMPORT(name, ptr) ({                                            \
            signature = (char *)ptr##_SIGNATURE;                        \
            ohm_module_find_method((name), &signature, (void *)&(ptr)); \
        })

    extension_init();
    
    if (!strcmp(address, "disabled")) {
        OHM_INFO("resolver: console disabled");
        return 0;
    }
    
    if (!IMPORT("console.open", console_open)) {
        OHM_INFO("resolver: no console methods available, console disabled");
        return 0;
    }
    
    IMPORT("console.close" , console_close);
    IMPORT("console.printf", console_printf);
    IMPORT("console.grab"  , console_grab);
    IMPORT("console.ungrab", console_ungrab);

    if (console_close == NULL || console_printf == NULL ||
        console_grab == NULL || console_ungrab == NULL) {
        OHM_WARNING("resolver: missing console methods, console disabled");
        return 0;
    }
    
    
    OHM_INFO("resolver: using console %s", address);
    
    console = console_open(address,
                           console_opened, console_closed, console_input,
                           NULL, FALSE);
    
    return console < 0 ? EINVAL : 0;
#undef IMPORT    
}


/********************
 * console_exit
 ********************/
static void
console_exit(void)
{
#if 0
    if (console > 0 && console_close)
        console_close(console);
#endif
    
    console = 0;

    extension_exit();
}



/*****************************************************************************
 *                         *** console event handlers ***                    *
 *****************************************************************************/

/********************
 * console_opened
 ********************/
static void
console_opened(int id, struct sockaddr *peer, int peerlen)
{
    (void)peer;
    (void)peerlen;

    OHM_INFO("new console 0x%x opened", id);

    console_printf(id, "OHM Policy Debug Console\n");
    console_printf(id, "Type help to get a list of available commands.\n\n");
    console_printf(id, CONSOLE_PROMPT);
}


/********************
 * console_closed
 ********************/
static void
console_closed(int id)
{
    OHM_INFO("console 0x%x closed", id);
}


/********************
 * console_input
 ********************/
static void
console_input(int id, char *input, void *data)
{
    static char last[256] = "\0";

    command_t    *command;
    extension_t  *extension;
    char          name[64], *args, *s, *d;
    unsigned int  n;

    (void)data;

    if (!input[0]) {
        console_printf(id, CONSOLE_PROMPT);
        return;
    }

    if (!strcmp(input, REDO_LAST) && last[0] != '\0')
        input = last;
    
    n = 0;
    s = input;
    d = name;
    while (*s && *s != ' ' && n < sizeof(name) - 1) {
        *d++ = *s++;
        n++;
    }
    *d = '\0';

    args = s;
    while (*args == ' ' || *args == '\t')
        args++;

    if ((command = find_command(name)) != NULL)
        command->handler(id, args);
    else {
        if ((extension = extension_find(name)) != NULL)
            extension_run(id, extension, args);
        else
            console_printf(id, "unknown console command \"%s\"\n", input);
    }
    
    if (strcmp(input, REDO_LAST)) {
        strncpy(last, input, sizeof(last) - 1);
        last[sizeof(last) - 1] = '\0';
    }

    console_printf(id, CONSOLE_PROMPT);
}



/*****************************************************************************
 *                       *** console command handlers ***                    *
 *****************************************************************************/

/********************
 * extension_init
 ********************/
static void
extension_init(void)
{
    extensions = NULL;
    nextension = 0;
}


/********************
 * extension_exit
 ********************/
static void
extension_exit(void)
{
    int i;

    for (i = 0; i < nextension; i++)
        if (extensions[i].name != NULL)
            FREE(extensions[i].name);
    
    FREE(extensions);
    extensions = NULL;
    nextension = 0;
}


/********************
 * extension_find
 ********************/
static extension_t *
extension_find(char *name)
{
    int i;
    
    for (i = 0; i < nextension; i++)
        if (extensions[i].name != NULL && !strcmp(extensions[i].name, name))
            return extensions + i;

    return NULL;
}


/********************
 * extension_add
 ********************/
static int
extension_add(char *name, void (*handler)(char *))
{

    if (extension_find(name) != NULL || find_command(name) != NULL)
        return FALSE;
    
    if (REALLOC_ARR(extensions, nextension, nextension + 1) == NULL)
        return FALSE;
    else {
        extensions[nextension].name    = STRDUP(name);
        extensions[nextension].handler = handler;
        nextension++;
        return TRUE;
    }
}


/********************
 * extension_del
 ********************/
static int
extension_del(char *name, void (*handler)(char *))
{
    extension_t *extension;

    if ((extension = extension_find(name)) != NULL) {
        if (extension->handler == handler) {
            FREE(extension->name);
            extension->name    = NULL;
            extension->handler = NULL;
            return TRUE;
        }
    }

    return FALSE;
}


/********************
 * extension_run
 ********************/
static void
extension_run(int id, extension_t *extension, char *command)
{
    int release;

    if (!grabbed) {
        command_grab(id, "");
        release = TRUE;
    }
    else
        release = FALSE;

    extension->handler(command);

    if (release)
        command_release(id, "");
}


/********************
 * add_command
 ********************/
OHM_EXPORTABLE(int, add_command, (char *name, void (*handler)(char *)))
{
    return extension_add(name, handler);
}


/********************
 * del_command
 ********************/
OHM_EXPORTABLE(int, del_command, (char *name, void (*handler)(char *)))
{
    return extension_del(name, handler);
}


/********************
 * command_bye
 ********************/
static void
command_bye(int id, char *input)
{
    (void)input;

    console_close(id);
}


/********************
 * command_quit
 ********************/
static void
command_quit(int id, char *input)
{
    (void)input;

    console_close(id);
}


/********************
 * command_dump
 ********************/
static void
command_dump(int id, char *input)
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    OhmFact      *fact;
    GSList       *list;
    char          factname[128], *p, *q, *dump;

    if (!strcmp(input, "prefix")) {
        if (prefix[0])
            console_printf(id, "current prefix: \"%s\"\n", prefix);
        else
            console_printf(id, "no prefix set\n");
        return;
    }
    
    p = input;
    if (!*p)
        p = "all";
    while (*p) {
        while (*p == ' ' || *p == ',')
            p++;
        if (*p == '$')
            p++;
        q = factname;
        if (strchr(input, '.') == NULL && prefix[0])
            q += snprintf(factname, 128, "%s.", prefix);
        for ( ; *p && *p != ','; *q++ = *p++)
            ;
        *q = '\0';
        if (!strcmp(factname, "all")) {
            dump = ohm_fact_store_to_string(fs);
            console_printf(id, "fact store: %s\n", dump);
            g_free(dump);
        }
        else if (!strcmp(factname, "targets")) {
            dres_dump_targets(dres);
        }
        else {
            console_printf(id, "current facts for \"%s\"\n", factname);
            for (list = ohm_fact_store_get_facts_by_name(fs, factname);
                 list != NULL;
                 list = g_slist_next(list)) {
                fact = (OhmFact *)list->data;
                dump = ohm_structure_to_string(OHM_STRUCTURE(fact));
                console_printf(id, "%s\n", dump ?: "");
                g_free(dump);
            }
        }
    }
}


/********************
 * command_set
 ********************/
static void
command_set(int id, char *input)
{
    int   max = sizeof(prefix) - 1;
    int   len = sizeof("prefix ") - 1;
    char *p;

    if (!strncmp(input, "prefix ", len)) {
        strncpy(prefix, input + len, max);
        prefix[max] = '\0';
        len = strlen(prefix);
        for (p = prefix + len - 1; *p == '.' && p > prefix; *p-- = '\0')
            ;
        console_printf(id, "prefix set to \"%s\"\n", prefix);
    }
    else
        set_fact(id, input);
}


/********************
 * command_resolve
 ********************/
static void
command_resolve(int id, char *input)
{
    char *goal;
    char *args[MAX_CMDARGS * 3 + 1], *t;
    int   i;

    if (!input[0]) {
        goal    = "all";
        args[0] = NULL;
    }
    else {
        goal = input;
        while (*input && *input != ' ' && *input != '\t')
            input++;
        if (*input)
            *input++ = '\0';
        if (parse_dres_args(input, args, MAX_ARGS) != 0) {
            console_printf(id, "failed to parse arguments\n");
            return;
        }
    }
    
    console_printf(id, "updating goal \"%s\"\n", goal);
    if (args[0]) {
        console_printf(id, "with arguments:");
        for (i = 0, t = " "; args[i] != NULL; i += 3, t = ", ") {
            char *var, *val;
            int   type;
            
            var  = args[i];
            type = GPOINTER_TO_INT(args[i + 1]);
            val  = args[i + 2];
            
            switch (type) {
            case 's':
                console_printf(id, "%s%s='%s'", t, var, val);
                break;
            case 'i':
                console_printf(id, "%s%s=%d", t, var, GPOINTER_TO_INT(val));
                break;
            case 'd':
                console_printf(id, "%s%s=%f", t, var, *(double *)val);
                break;
            default:
                console_printf(id, "%s<unknown type 0x%x>", t, type);
                break;
            }
        }
        console_printf(id, "\n");
    }
    
    dres_update_goal(dres, goal, args);
}


/********************
 * command_prolog
 ********************/
static void
command_prolog(int id, char *input)
{
    (void)id;
    (void)input;

    rules_prompt();
}


/********************
 * command_grab
 ********************/
static void
command_grab(int id, char *input)
{
    (void)input;

    if (!grabbed) {
        console_grab(id, 0);
        console_grab(id, 1);
        console_grab(id, 2);
        grabbed = TRUE;
    }
}

/********************
 * command_release
 ********************/
static void
command_release(int id, char *input)
{
    (void)input;

    if (grabbed) {
        console_ungrab(id, 0);
        console_ungrab(id, 1);
        console_ungrab(id, 2);
        grabbed = FALSE;
    }
}


/********************
 * command_debug
 ********************/
static void
command_debug(int id, char *input)
{
    char buf[8*1024];

    if (!strcmp(input, "list") || !strcmp(input, "help")) {
        trace_show(TRACE_DEFAULT_NAME, buf, sizeof(buf),
                         "  %-25.25F %-30.30d [%-3.3s]");
#if 0
        console_printf(id, "The available debug flags are:\n%s\n", buf);
#endif
    }
    else if (!strcmp(input, "disable") || !strcmp(input, "off")) {
        trace_context_disable(TRACE_DEFAULT_CONTEXT);
        console_printf(id, "Debugging is now turned off.\n");
    }
    else if (!strcmp(input, "enable") || !strcmp(input, "on")) {
        trace_context_enable(TRACE_DEFAULT_CONTEXT);
        console_printf(id, "Debugging is now turned on.\n");
    }
    else if (!strncmp(input, "set ", 4)) {
        if (trace_configure(input + 4))
            console_printf(id, "failed to parse debugging flags.\n");
        else
            console_printf(id, "Debugging configuration updated.\n");
    }
    else if (!strncmp(input, "rule ", 5)) {
        rules_trace(input + 5);
    }
}


/********************
 * command_log
 ********************/
static void
command_log(int id, char *input)
{
    const char  *level, *next;
    int          length, e, w, i, d, off;
    OhmLogLevel  l;
    
    (void)id;
    
    e = ohm_log_disable(OHM_LOG_ERROR);
    w = ohm_log_disable(OHM_LOG_WARNING);
    i = ohm_log_disable(OHM_LOG_INFO);
    d = ohm_log_disable(OHM_LOG_DEBUG);
    
    for (level = input; level && *level; level = next) {
        while (*level == ',' || *level == ' ')
            level++;

        if ((next = strchr(level, ',')) != NULL)
            length = (int)(next - level);
        else
            length = strlen(level);

        while (level[length - 1] == ' ' && length > 0)
            length--;
        
        if (length) {
            off = FALSE;
            if (*level == '-' || *level == '+') {
                length--;
                off = (*level == '-');
                level++;
            }
            
            if      (!strncmp(level, "error"  , length)) l = OHM_LOG_ERROR;
            else if (!strncmp(level, "warning", length)) l = OHM_LOG_WARNING;
            else if (!strncmp(level, "info"   , length)) l = OHM_LOG_INFO;
            else if (!strncmp(level, "debug"  , length)) l = OHM_LOG_DEBUG;
            else                                         goto restore;
            
            (off ? ohm_log_disable : ohm_log_enable)(l);
        }
    }
    
    return;
    
 restore:
    (e ? ohm_log_enable : ohm_log_disable)(OHM_LOG_ERROR);
    (w ? ohm_log_enable : ohm_log_disable)(OHM_LOG_WARNING);
    (i ? ohm_log_enable : ohm_log_disable)(OHM_LOG_INFO);
    (d ? ohm_log_enable : ohm_log_disable)(OHM_LOG_DEBUG);
}


/********************
 * command_statistics
 ********************/
static void
command_statistics(int id, char *input)
{
    (void)id;
    
    rule_statistics(input);
}


/********************
 * command_help
 ********************/
static void
command_help(int id, char *input)
{
    command_t *c;
    char       syntax[128];
    int        i, release = FALSE;

    (void)input;

    console_printf(id, "Available commands:\n");
    for (c = commands; c->name != NULL; c++) {
        sprintf(syntax, "%s%s%s", c->name, c->args ? " ":"", c->args ?: ""); 
        console_printf(id, "    %-30.30s %s\n", syntax, c->description);
    }

    if (nextension > 0) {
        console_printf(id, "Additional commands:\n");

        if (!grabbed) {
            command_grab(id, "");
            release = TRUE;
        }
        
        for (i = 0; i < nextension; i++) {
            if (extensions[i].name != NULL) {
                console_printf(id, "%s:\n", extensions[i].name);
                extensions[i].handler("help");
            }
        }
        
        if (release)
            command_release(id, "");
    }
}


/********************
 * find_command
 ********************/
static command_t *
find_command(char *name)
{
    command_t *c;

    for (c = commands; c->name != NULL; c++)
        if (!strcmp(c->name, name))
            return c;
    
    return NULL;
}


/********************
 * parse_dres_args
 ********************/
static int
parse_dres_args(char *input, char **args, int narg)
{
    static double dbl;                       /* ouch.... */

    char  *next, *var, *val, **arg;
    int     i, ndbl = 0;

    next = input;
    arg  = args;
    for (i = 0; next && *next && i < narg; i++) {
        while (*next == ' ')
            next++;

        var = next;
        val = strchr(next, '=');

        if (!*var)
            break;
        
        if (val == NULL)
            return EINVAL;

        *val++ = '\0';

        while (*val == ' ')
            val++;
        
        if (!*val)
            return EINVAL;
        
        if ((next = strchr(val, ',')) != NULL)
            *next++ = '\0';

        *arg++ = var;
        
        if (val[1] == ':') {
            switch (val[0]) {
            case 's':
                *arg++ = (char *)'s';
                i++;
                *arg++ = val + 2;
                break;
            case 'i':
                *arg++ = (char *)'i';
                i++;
                *arg++ = (char *)strtoul(val + 2, NULL, 10);
                break;
            case 'd':
                *arg++ = (char *)'d';
                dbl = strtod(val + 2, NULL);
                *arg++ = (char *)(void *)&dbl;
                ndbl++;
                if (ndbl > 1) {
                    console_printf(console,
                                   "This test code is unable to pass multiple "
                                   "doubles (variable %s) to resolver.", var);
                    return EINVAL;
                }
                break;
            default:
                goto oldstring;
            }
        }
        else {
        oldstring:
            *arg++ = (char *)'s';
            i++;
            *arg++ = val;
        }
    }
    *arg = NULL;

    return 0;
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

