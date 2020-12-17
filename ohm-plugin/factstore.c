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


/* factstore signal names */
#define FACT_INSERTED "inserted"
#define FACT_REMOVED  "removed"
#define FACT_UPDATED  "updated"


static void schedule_resolve(gpointer object, gpointer user_data);
static void schedule_updated(gpointer fact, gpointer name, gpointer value,
                             gpointer user_data);


static guint         update;                  /* id of next update, if any */
static OhmFactStore *store;                   /* fact store in use */




/*****************************************************************************
 *                       *** initialization & cleanup ***                    *
 *****************************************************************************/


/********************
 * factstore_init
 ********************/
static int
factstore_init(void)
{
    gpointer fs;
    
    if ((store = ohm_get_fact_store()) == NULL)
        return EINVAL;
    
    fs = G_OBJECT(store);
    
    g_signal_connect(fs, FACT_INSERTED, G_CALLBACK(schedule_resolve), NULL);
    g_signal_connect(fs, FACT_REMOVED , G_CALLBACK(schedule_resolve), NULL);
    g_signal_connect(fs, FACT_UPDATED , G_CALLBACK(schedule_updated), NULL);
    
    return 0;
}


/********************
 * factstore_exit
 ********************/
static void
factstore_exit(void)
{
    gpointer fs;

    if (store == NULL)
        return;

    fs = G_OBJECT(store);
    g_signal_handlers_disconnect_by_func(fs, schedule_resolve, NULL);
    g_signal_handlers_disconnect_by_func(fs, schedule_updated, NULL);

    store = NULL;
}


/********************
 * update_all
 ********************/
static gboolean
update_all(gpointer data)
{
    (void)data;

    OHM_DEBUG(DBG_RESOLVE, "resolving goal \"all\"...");
    dres_update_goal(dres, "all", NULL);
    update = 0;

    return FALSE;
}


/********************
 * schedule_resolve
 ********************/
static void
schedule_resolve(gpointer object, gpointer user_data)
{
    (void)object;
    (void)user_data;

    if (!update) {
        OHM_DEBUG(DBG_RESOLVE, "resolving of goal \"all\" scheduled...");
        update = g_idle_add(update_all, NULL);
    }
}


/********************
 * schedule_updated
 ********************/
static void
schedule_updated(gpointer fact, gpointer name, gpointer value,
                 gpointer user_data)
{
    (void)name;
    (void)value;

    schedule_resolve(fact, user_data);
}


/*****************************************************************************
 *                        *** rule engine / fact glue ***                    *
 *****************************************************************************/

/********************
 * object_to_fact
 ********************/
static OhmFact *
object_to_fact(char **object)
{
    OhmFact *fact;
    GValue  *value;
    char    *field, *v, *name;
    int      i, type;
    
    if (object == NULL || strcmp(object[0], "name") || object[1] == NULL)
        return NULL;
    
    if (GPOINTER_TO_INT(object[1]) != 's' || (name = object[2]) == NULL)
        return NULL;

    if ((fact = ohm_fact_new(name)) == NULL)
        return NULL;
    
    for (i = 3; object[i] != NULL; i += 3) {
        field = object[i];
        type  = GPOINTER_TO_INT(object[i+1]);
        v     = object[i+2];
        switch (type) {
        case 's': value = ohm_value_from_string(v);                  break;
        case 'i': value = ohm_value_from_int(GPOINTER_TO_INT(v));    break;
        case 'd': value = ohm_value_from_double(*(double *)v);       break;
        default:  value = ohm_value_from_string("<invalid type>");   break;
        }
        ohm_fact_set(fact, field, value);
    }

    return fact;
}


/********************
 * retval_to_facts
 ********************/
static int
retval_to_facts(char ***objects, OhmFact **facts, int max)
{
    char **object;
    int    i;
    
    for (i = 0; (object = objects[i]) != NULL && i < max; i++) {
        if ((facts[i] = object_to_fact(object)) == NULL)
            return -EINVAL;
    }
    
    return i;
}



/*****************************************************************************
 *                     *** variables / factstore glue ***                    *
 *****************************************************************************/

/*
 * this is copy-paste code from the old variables.c in the DRES library
 */

typedef struct {
    char              *name;
    char              *value;
} dres_fldsel_t;

typedef struct {
    int                count;
    dres_fldsel_t     *field;
} dres_selector_t;


static dres_selector_t *parse_selector(char *descr)
{
    dres_selector_t *selector;
    dres_fldsel_t   *field;
    char            *p, *q, c;
    char            *str;
    char            *name;
    char            *value;
    char             buf[1024];

    
    if (descr == NULL) {
        errno = 0;
        return NULL;
    }

    for (p = descr, q = buf;  (c = *p) != '\0';   p++) {
        if (c > 0x20 && c < 0x7f)
            *q++ = c;
    }
    *q = '\0';

    if ((selector = malloc(sizeof(*selector))) == NULL)
        return NULL;
    memset(selector, 0, sizeof(*selector));

    for (str = buf;   (name = strtok(str, ",")) != NULL;   str = NULL) {
        if ((p = strchr(name, ':')) == NULL)
            OHM_DEBUG(DBG_FACTS, "invalid selctor: '%s'", descr);
        else {
            *p++ = '\0';
            value = p;

            selector->count++;
            selector->field = realloc(selector->field,
                                      sizeof(dres_fldsel_t) * selector->count);

            if (selector->field == NULL)
                return NULL; /* maybe better not to attempt to free anything */
            
            field = selector->field + selector->count - 1;

            field->name  = strdup(name);
            field->value = strdup(value);
        }
    }
   
    return selector;
}

static void free_selector(dres_selector_t *selector)
{
    int i;

    if (selector != NULL) {
        for (i = 0; i < selector->count; i++) {
            free(selector->field[i].name);
            free(selector->field[i].value);
        }

        free(selector);
    }
}

static int is_matching(OhmFact *fact, dres_selector_t *selector)
{
    dres_fldsel_t *fldsel;
    GValue        *gval;
    long int       ival;
    char          *e;
    int            i;
    int            match;
  
    if (fact == NULL || selector == NULL)
        match = FALSE;
    else {
        match = TRUE;

        for (i = 0;    match && i < selector->count;    i++) {
            fldsel = selector->field + i;

            if ((gval = ohm_fact_get(fact, fldsel->name)) == NULL)
                match = FALSE;
            else {
                switch (G_VALUE_TYPE(gval)) {
                    
                case G_TYPE_STRING:
                    match = !strcmp(g_value_get_string(gval), fldsel->value);
                    break;
                    
                case G_TYPE_INT:
                    ival  = strtol(fldsel->value, &e, 10);
                    match = (*e == '\0' && g_value_get_int(gval) == ival);
                    break;

                default:
                    match = FALSE;
                    break;
                }
            }
        } /* for */
    }

    return match;
}

static int find_facts(char *name, char *select, OhmFact **facts, int max)
{
    dres_selector_t *selector = parse_selector(select);
    
    GSList            *list;
    OhmFact           *fact;
    int                flen;
    int                i;

    list   = ohm_fact_store_get_facts_by_name(ohm_fact_store_get_fact_store(),
                                              name);
    for (i = flen = 0;    list != NULL;   i++, list = g_slist_next(list)) {
        fact = (OhmFact *)list->data;

        if (!selector || is_matching(fact, selector))
            facts[flen++] = fact;

        if (flen >= max) {
            free_selector(selector);
            errno = ENOMEM;
            return -1;
        }
        
    }

    free_selector(selector);
    return flen;
}


/********************
 * set_fact
 ********************/
static void
set_fact(int cid, char *buf)
{
    GValue      *gval;
    char         selector[128];
    char        *str, *name, *member, *selfld, *value, *p, *q;
    int          n = 128, len, i;
    OhmFact     *facts[n];
    
    selector[0] = '\0';
    /*
     * here we parse command lines like:
     *    com.nokia.policy.audio_route[device:ihf].status = 0, ...
     * where
     *    'com.nokia.policy.audio_route' is a fact that has two fields:
     *    'device' and 'status'
     */
    
    for (str = buf; (name = strtok(str, ",")) != NULL; str = NULL) {
        if (*name == '$')
            name++;
        if ((p = strchr(name, '=')) != NULL) {
            *p++ = 0;
            value = p;
       
            if ((p = strrchr(name, '.')) != NULL) {
                *p++ = 0;
                member = p;
                    
                if (p[-2] == ']' && (q = strchr(name, '[')) != NULL) {
                        
                    len = p - 2 - q - 1;
                    strncpy(selector, q + 1, len);
                    selector[len] = '\0';
                        
                    *q = p[-2] = 0;
                    selfld = q + 1;
                    if ((p = strchr(selfld, ':')) == NULL) {
                        console_printf(cid, "Invalid input: %s\n", selfld);
                        continue;
                    }
                    else {
                        *p++ = 0;
                    }
                }
                    
                if ((n = find_facts(name, selector, facts, n)) < 0)
                    console_printf(cid, "no fact matches %s[%s]\n",
                                   name, selector);
                else {
                    for (i = 0; i < n; i++) {
                        if (value[1] == ':') {
                            switch (value[0]) {
                                int    ival;
                                double dval;
                                
                            case 'i':
                                ival = (int)strtol(value + 2, NULL, 10);
                                gval = ohm_value_from_int(ival);
                                break;
                                
                            case 's':
                                gval = ohm_value_from_string(value + 2);
                                break;
                                
                            case 'd':
                                dval = strtod(value + 2, NULL);
                                gval = ohm_value_from_double(dval);
                                break;

                            default:
                                gval = ohm_value_from_string(value);
                                break;
                            }
                        }
                        else
                            gval = ohm_value_from_string(value);
                        
                        ohm_fact_set(facts[i], member, gval);
                        console_printf(cid, "%s:%s = %s\n", name,
                                       member, value);
                    }
                }
            }
        }
    }
}








/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

   
