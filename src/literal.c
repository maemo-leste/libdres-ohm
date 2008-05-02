#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dres/dres.h>


/*****************************************************************************
 *                          *** literal handling ***                         *
 *****************************************************************************/

/********************
 * dres_add_literal
 ********************/
int
dres_add_literal(dres_t *dres, char *name)
{
    dres_literal_t *l;
    int             id;

    if (!REALLOC_ARR(dres->literals, dres->nliteral, dres->nliteral + 1))
        return DRES_ID_NONE;

    id = dres->nliteral++;
    l  = dres->literals + id;

    l->id   = DRES_LITERAL(id);
    l->name = STRDUP(name);

    return l->name ? l->id : DRES_ID_NONE;
}


/********************
 * dres_literal_id
 ********************/
int
dres_literal_id(dres_t *dres, char *name)
{
    dres_literal_t *l;
    int             i;

    if (name != NULL)
        for (i = 0, l = dres->literals; i < dres->nliteral; i++, l++) {
            if (!strcmp(name, l->name))
                return l->id;
        }
    
    return dres_add_literal(dres, name);
}


/********************
 * dres_free_literals
 ********************/
void
dres_free_literals(dres_t *dres)
{
    int             i;
    dres_literal_t *l;
    
    for (i = 0, l = dres->literals; i < dres->nliteral; i++, l++)
        FREE(l->name);
        
    FREE(dres->literals);

    dres->literals = NULL;
    dres->nliteral = 0;
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
