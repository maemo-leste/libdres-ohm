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


#ifndef __DRES_VM_H__
#define __DRES_VM_H__

#include <string.h>
#include <setjmp.h>
#include <stdint.h>

#include <ohm/ohm-fact.h>


/*
 * miscallaneous macros
 */

#define VM_ALIGN_TO(n, a) (((n) + ((a)-1)) & ~((a)-1))
#define VM_ALIGNED(n, a)  (!((n) & ((a) - 1)))

#define VM_ALIGNMENT     (sizeof(void *))
#define VM_ALIGNED_OK(n) VM_ALIGNED(n, VM_ALIGNMENT)

#define VM_ALIGN_TO_INSTR(n)    (VM_ALIGN_TO(n, sizeof(uintptr_t)) / sizeof(uintptr_t))

/*
 * VM logging
 */

typedef enum {
    VM_LOG_FATAL   = 0,
    VM_LOG_ERROR   = 1,
    VM_LOG_WARNING = 2,
    VM_LOG_NOTICE  = 3,
    VM_LOG_INFO    = 4,
} vm_log_level_t;


#define VM_ERROR(fmt, args...)   vm_log(VM_LOG_ERROR, fmt , ## args)
#define VM_WARNING(fmt, args...) vm_log(VM_LOG_WARNING, fmt , ## args)
#define VM_INFO(fmt, args...)    vm_log(VM_LOG_INFO, fmt , ## args)


/*
 * VM stack
 */

typedef enum {
    VM_TYPE_UNKNOWN = 0,
    VM_TYPE_NIL,                              /* unset */
    VM_TYPE_INTEGER,                          /* signed 32-bit integer */
    VM_TYPE_DOUBLE,                           /* double prec. floating */
    VM_TYPE_FLOAT = VM_TYPE_DOUBLE,           /* our foating is double */
    VM_TYPE_STRING,                           /* a \0-terminated string */
    VM_TYPE_LOCAL,                            /* local variables */
    VM_TYPE_FACTS,                            /* an array of facts */
    VM_TYPE_GLOBAL = VM_TYPE_FACTS,           /* globals are facts */
} vm_type_t;


#define VM_UNNAMED_GLOBAL "__vm_global"       /* an unnamed global */
#define VM_GLOBAL_IS_NAME(g) ((g)->name != NULL && (g)->nfact == 0)
#define VM_GLOBAL_IS_ORPHAN(g)                                     \
    ((g)->nfact == 1 &&                                            \
     !strcmp(ohm_structure_get_name(OHM_STRUCTURE((g)->facts[0])), \
             VM_UNNAMED_GLOBAL))

typedef struct vm_global_s {
    char    *name;                            /* for free-hanging facts */
    int      nfact;
    OhmFact *facts[0];
} vm_global_t;


typedef union vm_value_s {
    double       d;                           /* VM_TYPE_DOUBLE  */
    int          i;                           /* VM_TYPE_INTEGER */
    char        *s;                           /* VM_TYPE_STRING */
    vm_global_t *g;                           /* VM_TYPE_GLOBAL */
} vm_value_t;


typedef struct vm_stack_entry_s {
    vm_value_t v;                             /* actual value on the stack */
    int        type;                          /* type of the value */
} vm_stack_entry_t;


typedef struct vm_stack_s {
    vm_stack_entry_t *entries;                /* actual stack entries */
    int               nentry;                 /* top of the stack */
    int               nalloc;                 /* size of the stack */
} vm_stack_t;


#define VM_LOCAL_INDEX(id) ((id)&0x00ffffff)  /* XXX TODO DRES_INDEX(id) */


typedef struct vm_scope_s vm_scope_t;

struct vm_scope_s {
    vm_scope_t       *parent;                 /* parent scope */
    unsigned int      nvariable;              /* number of variables */
    vm_stack_entry_t  variables[0];           /* variable table */
};


/*
 * VM instructions
 */

typedef enum {
    VM_OP_UNKNOWN = 0,
    VM_OP_PUSH,                               /* push a value or scope */
    VM_OP_POP,                                /* pop a value or scope */
    VM_OP_FILTER,                             /* global filtering */
    VM_OP_UPDATE,                             /* global updating */
    VM_OP_SET,                                /* global assignment */
    VM_OP_GET,                                /* global/local evaluation */ 
    VM_OP_CREATE,                             /* global creation */
    VM_OP_CALL,                               /* function call */
    VM_OP_CMP,                                /* relation operators, not */
    VM_OP_BRANCH,                             /* branch */
    VM_OP_DEBUG,                              /* VM debugging */
    VM_OP_HALT,                               /* stop VM execution */
    VM_OP_REPLACE,                            /* global replacement */
    VM_OP_MAXCODE = 0xff
} vm_opcode_t;


#define VM_OP_CODE(instr)      ((instr) & 0xff)
#define VM_OP_ARGS(instr)      ((instr) >> 8)
#define VM_INSTR(opcode, args) ((opcode) | ((args) << 8))


/*
 * PUSH instructions
 */

#define VM_PUSH_TYPE(instr) (VM_OP_ARGS(instr) & 0xff)
#define VM_PUSH_DATA(instr) (VM_OP_ARGS(instr) >> 8)
#define VM_PUSH_INSTR(t, d)  VM_INSTR(VM_OP_PUSH, (((d) << 8) | ((t) & 0xff)))

#define VM_INSTR_PUSH_INT(c, errlbl, ec, val) do {                      \
        if (0 <= val && val < 0xfffe) {                                 \
            uintptr_t instr;                                            \
            instr = VM_PUSH_INSTR(VM_TYPE_INTEGER, val + 1);            \
            ec = vm_chunk_add(c, &instr, 1, sizeof(instr));             \
            if (ec)                                                     \
                goto errlbl;                                            \
        }                                                               \
        else {                                                          \
            uintptr_t instr[2];                                         \
            instr[0] = VM_PUSH_INSTR(VM_TYPE_INTEGER, 0);               \
            instr[1] = val;                                             \
            ec = vm_chunk_add(c, instr, 1, sizeof(instr));              \
            if (ec)                                                     \
                goto errlbl;                                            \
        }                                                               \
    } while (0)

#define VM_INSTR_PUSH_DOUBLE(c, errlbl, ec, val) do {                   \
        uintptr_t instr[1 + VM_ALIGN_TO_INSTR(sizeof(double))];         \
        double   *dp = (double *)&instr[1];                             \
        instr[0] = VM_PUSH_INSTR(VM_TYPE_DOUBLE, 0);                    \
        *dp      = val;                                                 \
        ec       = vm_chunk_add(c, instr, 1, sizeof(instr));            \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_PUSH_STRING(c, errlbl, ec, val) do {                   \
        int           len = strlen(val) + 1;                            \
        int           n   = VM_ALIGN_TO_INSTR(len);                     \
        uintptr_t     instr[1 + n];                                     \
        instr[0] = VM_PUSH_INSTR(VM_TYPE_STRING, len);                  \
        strcpy((char *)(instr + 1), val);                               \
        /* could pad here with zeros if (len & 0x3) */                  \
        ec = vm_chunk_add(c, instr, 1, sizeof(instr));                  \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_PUSH_GLOBAL(c, errlbl, ec, val) do {                   \
        int           len = strlen(val) + 1;                            \
        int           n   = VM_ALIGN_TO_INSTR(len);                     \
        uintptr_t     instr[1 + n];                                     \
        instr[0] = VM_PUSH_INSTR(VM_TYPE_GLOBAL, len);                  \
        strcpy((char *)(instr + 1), val);                               \
        /* could pad here with zeros if (len & 0x3) */                  \
        ec = vm_chunk_add(c, instr, 1, sizeof(instr));                  \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_PUSH_LOCALS(c, errlbl, ec, nvar) do {                  \
        uintptr_t instr;                                                \
        instr = VM_PUSH_INSTR(VM_TYPE_LOCAL, nvar);                     \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)
    

/*
 * POP instructions
 */

enum {
    VM_POP_LOCALS  = 0,
    VM_POP_DISCARD = 1,
};

#define VM_INSTR_POP_LOCALS(c, errlbl, ec) do {                         \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_POP, VM_POP_LOCALS);                     \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


#define VM_INSTR_POP_DISCARD(c, errlbl, ec) do {                        \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_POP, VM_POP_DISCARD);                    \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * FILTER instructions
 */

#define VM_FILTER_NFIELD(instr) VM_OP_ARGS(instr)

#define VM_INSTR_FILTER(c, errlbl, ec, n) do {                          \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_FILTER, n);                              \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * UPDATE instructions
 */

#define VM_ASSIGN_PARTIAL 0x80
#define VM_UPDATE_NFIELD(instr)  (VM_OP_ARGS(instr) & ~VM_ASSIGN_PARTIAL)
#define VM_UPDATE_PARTIAL(instr) (VM_OP_ARGS(instr) & VM_ASSIGN_PARTIAL)

#define VM_INSTR_UPDATE(c, errlbl, ec, n, partial) do {                 \
        uintptr_t instr;                                                \
        uintptr_t mod = n | (partial ? VM_ASSIGN_PARTIAL : 0);          \
        instr = VM_INSTR(VM_OP_UPDATE, mod);                            \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * REPLACE instructions
 */

#define VM_REPLACE_NFIELD(instr) (VM_OP_ARGS(instr))
#define VM_INSTR_REPLACE(c, errlbl, ec, n) do {                         \
        uintptr_t instr;                                                \
        uintptr_t mod = n;                                              \
        instr = VM_INSTR(VM_OP_REPLACE, mod);                           \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * CREATE instructions
 */

#define VM_CREATE_NFIELD(instr) VM_OP_ARGS(instr)

#define VM_INSTR_CREATE(c, errlbl, ec, n) do {                          \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_CREATE, n);                              \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * SET instructions
 */

enum {
    VM_SET_NONE  = 0x0,
    VM_SET_FIELD = 0x1,
};

#define VM_INSTR_SET(c, errlbl, ec) do {                                \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_SET, VM_SET_NONE);                       \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_SET_FIELD(c, errlbl, ec) do {                          \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_SET, VM_SET_FIELD);                      \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * GET instructions
 */

enum {
    VM_GET_NONE  = 0x00000000,
    VM_GET_FIELD = 0x00800000,
    VM_GET_LOCAL = 0x00400000,
};

#define VM_INSTR_GET_FIELD(c, errlbl, ec) do {                          \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_GET, VM_GET_FIELD);                      \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_GET_LOCAL(c, errlbl, ec, idx) do {                     \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_GET, VM_GET_LOCAL | idx);                \
        ec    = vm_chunk_add(c, &instr, 1, sizeof(instr));              \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * CALL instructions
 */

#define VM_INSTR_CALL(c, errlbl, ec, narg) do {                         \
        uintptr_t instr;                                                \
        instr = VM_INSTR(VM_OP_CALL, narg);                             \
        ec    = vm_chunk_add(c, &instr, 1, sizeof(instr));              \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * CMP instruction
 */

typedef enum {
    VM_RELOP_UNKNOWN = 0,
    VM_RELOP_EQ,
    VM_RELOP_NE,
    VM_RELOP_LT,
    VM_RELOP_LE,
    VM_RELOP_GT,
    VM_RELOP_GE,
    VM_RELOP_NOT,
    VM_RELOP_OR,
    VM_RELOP_AND
} vm_relop_t;

#define VM_CMP_RELOP(instr) ((vm_relop_t)VM_OP_ARGS(instr))

#define VM_INSTR_CMP(c, errlbl, ec, op) do {                    \
        uintptr_t instr;                                        \
        instr = VM_INSTR(VM_OP_CMP, (vm_relop_t)op);            \
        ec    = vm_chunk_add(c, &instr, 1, sizeof(instr));      \
        if (ec)                                                 \
            goto errlbl;                                        \
    } while (0)


/*
 * BRANCH instruction
 */

typedef enum {
    VM_BRANCH = 0x0,                /* unconditional branch */
    VM_BRANCH_EQ,                   /* branch if top of stack non-zero */
    VM_BRANCH_NE,                   /* branch if top of stack zero */
} vm_branch_t;

#define VM_BRANCH_TYPE(instr) (VM_OP_ARGS(instr) >> 22)
#define VM_BRANCH_DIFF(instr) ({                                \
    intptr_t __sign, __diff;                                    \
    __sign = VM_OP_ARGS(instr) & (0x1 << 21);                   \
    __diff = VM_OP_ARGS(instr) & (0xffffff >> 2);               \
    if (__sign)                                                 \
        __diff = -__diff;                                       \
    __diff; })

#define VM_INSTR_BRANCH(c, errlbl, ec, type, diff) ({           \
        uintptr_t instr;                                        \
        intptr_t  __d, __t;                                     \
        intptr_t  __offs;                                       \
        __d = (diff);                                           \
        if (__d < 0)                                            \
            __d = (0x1 << 21) | (-__d & (0xffffff >> 3));       \
        else                                                    \
            __d &= 0xffffff >> 3;                               \
        __t = (type) << 22;                                     \
        instr = VM_INSTR(VM_OP_BRANCH, __t | __d);              \
        ec    = vm_chunk_add(c, &instr, 1, sizeof(instr));      \
        if (ec)                                                 \
            goto errlbl;                                        \
        __offs = (c)->nsize / sizeof(uintptr_t) - 1;            \
        __offs;                                                 \
        })

#define VM_BRANCH_PATCH(c, offs, errlbl, ec, type, diff) do {   \
        uintptr_t *instr = (c)->instrs + (offs);                \
        intptr_t  __d, __t;                                     \
        __d = (diff);                                           \
        if (__d < 0)                                            \
            __d = (0x1 << 21) | (-__d & (0xffffff >> 3));       \
        else                                                    \
            __d &= 0xffffff >> 3;                               \
        __t = (type) << 22;                                     \
        *instr = VM_INSTR(VM_OP_BRANCH, __t | __d);             \
    } while (0)

#define VM_CHUNK_OFFSET(c) ((c)->nsize / sizeof(uintptr_t))

/*
 * DEBUG instructions
 */

#define VM_DEBUG_LEN(instr) VM_OP_ARGS(instr)
#define VM_DEBUG_INSTR(len) VM_INSTR(VM_OP_DEBUG, len)

#define VM_INSTR_DEBUG(c, errlbl, ec, val) do {                         \
        int           len = strlen(val) + 1;                            \
        int           n   = VM_ALIGN_TO_INSTR(len);                     \
        uintptr_t     instr[1 + n];                                     \
        instr[0] = VM_DEBUG_INSTR(len);                                 \
        strcpy((char *)(instr + 1), val);                               \
        /* could pad here with zeros if (len & 0x3) */                  \
        ec = vm_chunk_add(c, instr, 1, sizeof(instr));                  \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)



/*
 * HALT instruction
 */

#define VM_INSTR_HALT(c, errlbl, ec) do {                       \
        uintptr_t instr;                                        \
        instr = VM_INSTR(VM_OP_HALT, 0);                        \
        ec    = vm_chunk_add(c, &instr, 1, sizeof(instr));      \
        if (ec)                                                 \
            goto errlbl;                                        \
    } while (0)



/*
 * a chunk of VM instructions
 */

typedef struct vm_chunk_s {
    uintptr_t    *instrs;                    /* actual VM instructions */
    int           ninstr;                    /* number of instructions */
    int           nsize;                     /* code size in bytes */
    int           nleft;                     /* number of bytes free */
} vm_chunk_t;


/*
 * VM function calls
 */

typedef int (*vm_action_t)(void *data, char *name,
                           vm_stack_entry_t *args, int narg,
                           vm_stack_entry_t *retval);

typedef struct vm_method_s {
    char        *name;                       /* function name */
    int          id;                         /* function ID */
    vm_action_t  handler;                    /* function handler */
    void        *data;                       /* opaque user data */
} vm_method_t;


/*
 * VM exceptions
 *
 * Notes: Handling exceptions with setjmp/longjmp for our primitive language /
 *        virtual machine is arguably an overkill. However it should keep some
 *        of the underlying code cleaner and easier to extend.
 *
 * Notes: As a convention, methods return a negative error code to signal
 *        an error that should raise an exception. To fail silently without
 *        any exception shown FALSE is returned. Any other return value is
 *        interpreted as sucessful evaluation of the method.
 */

typedef struct {
    int         error;                         /* error code */
    char        message[256];                  /* error message */
    const char *context;                       /* exception context */
} vm_exception_t;

#define VM_RESET_EXCEPTION(e) do { \
        (e)->error      = 0;       \
        (e)->message[0] = '\0';    \
        (e)->context    = NULL;    \
    } while (0)

typedef struct vm_catch_s vm_catch_t;
struct vm_catch_s {
    jmp_buf         location;                  /* catch exceptions here */
    int             depth;                     /* stack depth upon entry */
    vm_scope_t     *scope;                     /* locals upon entry */
    vm_exception_t  exception;                 /* exception details */
    vm_catch_t     *prev;                      /* previous entry if any */
};



/*
 * Notes: This macro implements the calling conventions of the top-level
 *        VM interface vm_exec. A negative status indicates a VM exception.
 *        This will result in an error message and the rollback of the current
 *        DRES transaction. A zero status indicates (silent) failure resulting
 *        in the rollback of the current transaction. Any other status is
 *        interpreted as TRUE, and results in the continued evaluation of the
 *        current DRES goal.
 *
 *        This convention is directly visible at the VM/DRES method handler
 *        level. The handler return value should be crafted according to the
 *        rules above.
 */

#define VM_TRY(vm) ({                                                   \
        vm_catch_t __catch;                                             \
        int        __status;                                            \
                                                                        \
        VM_RESET_EXCEPTION(&__catch.exception);                         \
        __catch.prev  = vm->catch;                                      \
        __catch.depth = vm->stack ? vm->stack->nentry : 0;              \
        __catch.scope = vm->scope;                                      \
        vm->catch     = &__catch;                                       \
                                                                        \
        if ((__status = setjmp(__catch.location)) != 0) {               \
            vm_exception_t *e = &__catch.exception;                     \
                                                                        \
            if (e->error != 0) {                                        \
                int         i, type;                                    \
                vm_value_t  v;                                          \
                char       *name;                                       \
                                                                        \
                VM_ERROR("VM exception %d", __status);                  \
                if (e->message[0])                                      \
                    VM_ERROR("  %s", e->message);                       \
                if (e->context)                                         \
                    VM_ERROR("  while excecuting %s", e->context);      \
                                                                        \
                VM_ERROR("  local variables:");                         \
                for (i = 0; i < vm->nlocal; i++) {                      \
                    type = vm_scope_get(vm->scope, i, &v);              \
                    name = vm->names && vm->names[i] ?                  \
                        vm->names[i] : "<unknown>";                     \
                    switch (type) {                                     \
                    case VM_TYPE_UNKNOWN:                               \
                    case VM_TYPE_NIL:                                   \
                        /* VM_ERROR("    0x%x (%s) is unset", i, name);*/ \
                        break;                                          \
                    case VM_TYPE_INTEGER:                               \
                        VM_ERROR("    0x%x (%s): %d", i, name, v.i);    \
                        break;                                          \
                    case VM_TYPE_DOUBLE:                                \
                        VM_ERROR("    0x%x (%s): %f", i, name, v.d);    \
                        break;                                          \
                    case VM_TYPE_STRING:                                \
                        VM_ERROR("    0x%x (%s): '%s'", i, name,        \
                                 v.s && v.s[0] ? v.s : "");             \
                        break;                                          \
                    default:                                            \
                        VM_ERROR("    0x%x (%s): ???", i, name);        \
                        break;                                          \
                    }                                                   \
                }                                                       \
            }                                                           \
            fflush(stdout);                                             \
                                                                        \
            if (vm->stack->nentry > __catch.depth) {                    \
                VM_INFO("cleaning up the stack...");                    \
                vm_stack_cleanup(vm->stack,                             \
                                 vm->stack->nentry - __catch.depth);    \
            }                                                           \
            if (vm->scope != __catch.scope) {                           \
                VM_INFO("cleaning up the local/scope stack...");        \
                while (vm->scope && vm->scope != __catch.scope)         \
                    vm_scope_pop(vm);                                   \
            }                                                           \
                                                                        \
            vm->catch = vm->catch->prev;                                \
            __status = e->error;                                        \
            if (__status > 0)                                           \
                __status = -__status;                                   \
        }                                                               \
        else {                                                          \
            __status = vm_run(vm);             /* __status = 0 */       \
            vm->catch = vm->catch->prev;                                \
            __status = TRUE;                                            \
        }                                                               \
        __status;                                                       \
    })

/* macro for VM failure with an exception */
#define VM_RAISE(vm, err, fmt, args...) do {                            \
        vm_exception_t *e = &vm->catch->exception;                      \
                                                                        \
        e->error = err;                                                 \
        snprintf(e->message, sizeof(e->message), "%s: VM error: "fmt,   \
                 __FUNCTION__, ## args);                                \
        e->context = vm->info;                                          \
        longjmp(vm->catch->location, err);                              \
    } while (0)

/* macro for silent VM failure without an exception (just rollback) */
#define VM_FAIL(vm, fmt, args...) do {                                  \
        vm_exception_t *e = &vm->catch->exception;                      \
                                                                        \
        e->error = 0;                                                   \
        snprintf(e->message, sizeof(e->message), "%s: VM failure: "fmt, \
                 __FUNCTION__, ## args);                                \
        e->context = vm->info;                                          \
        longjmp(vm->catch->location, 1);                                \
    } while (0)


/*
 * VM flags
 */
#define VM_TST_FLAG(vm, f) ((vm)->flags &   VM_FLAG_##f)
#define VM_SET_FLAG(vm, f) ((vm)->flags |=  VM_FLAG_##f)
#define VM_CLR_FLAG(vm, f) ((vm)->flags &= ~VM_FLAG_##f)

enum {
    VM_FLAG_UNKNOWN  = 0x0,
    VM_FLAG_COMPILED = 0x1,                   /* loaded as precompiled */
};




/*
 * VM state
 */

typedef struct vm_state_s {
    vm_stack_t    *stack;                     /* VM stack */

    vm_chunk_t    *chunk;                     /* code being executed */
    uintptr_t     *pc;                        /* program counter */
    int            ninstr;                    /* # of instructions left */
    int            nsize;                     /* of code left */

    vm_method_t   *methods;                   /* action handlers */
    int            nmethod;                   /* number of actions */
    vm_scope_t    *scope;                     /* current local variables */
    int            nlocal;                    /* number of local variables */
    char         **names;                     /* names of local variables */

    vm_catch_t    *catch;                     /* catch exceptions here */
    int            flags;

    const char    *info;                      /* debug info for current pc */
} vm_state_t;




/* vm-stack.c */
vm_stack_t *vm_stack_new (int size);
void        vm_stack_del (vm_stack_t *s);
int         vm_stack_grow(vm_stack_t *s, int n);
int         vm_stack_trim(vm_stack_t *s, int n);
void        vm_stack_cleanup(vm_stack_t *s, int narg);


int vm_type       (vm_stack_t *s);
int vm_push       (vm_stack_t *s, int type, vm_value_t value);
int vm_push_int   (vm_stack_t *s, int i);
int vm_push_double(vm_stack_t *s, double d);
int vm_push_string(vm_stack_t *s, char *str);
int vm_push_global(vm_stack_t *s, vm_global_t *g);

vm_stack_entry_t *vm_args(vm_stack_t *s, int narg);

int         vm_pop (vm_stack_t *s, vm_value_t *value);
int         vm_peek(vm_stack_t *s, int idx, vm_value_t *value);

int         vm_pop_int   (vm_stack_t *s);
double      vm_pop_double(vm_stack_t *s);
char        *vm_pop_string(vm_stack_t *s);
vm_global_t *vm_pop_global(vm_stack_t *s);


/* vm-instr.c */
vm_chunk_t   *vm_chunk_new (int ninstr);
void          vm_chunk_del (vm_chunk_t *chunk);
uintptr_t    *vm_chunk_grow(vm_chunk_t *c, int nsize);
int           vm_chunk_add (vm_chunk_t *c,
                            uintptr_t *code, int ninstr, int nsize);

int vm_run(vm_state_t *vm);


/* vm-method.c */
int          vm_method_add    (vm_state_t *vm,
                               char *name, vm_action_t handler, void *data);
int          vm_method_del    (vm_state_t *vm, char *name, vm_action_t handler);
int          vm_method_set    (vm_state_t *vm, char *name,
                               vm_action_t handler, void *data);
vm_method_t *vm_method_lookup (vm_state_t *vm, char *name);
vm_method_t *vm_method_by_id  (vm_state_t *vm, int id);
int          vm_method_id     (vm_state_t *vm, char *name);
vm_action_t  vm_method_default(vm_state_t *vm, vm_action_t handler,
                               void **data);
int          vm_method_call   (vm_state_t *vm,
                               char *name, vm_method_t *m, int narg);
void         vm_free_methods  (vm_state_t *vm);

/* vm.c */
int  vm_init(vm_state_t *vm, int stack_size);
void vm_exit(vm_state_t *vm);
int  vm_exec(vm_state_t *vm, vm_chunk_t *code);


/* vm-global.c */
int          vm_global_lookup(char *name, vm_global_t **gp);
vm_global_t *vm_global_name  (char *name);
vm_global_t *vm_global_alloc (int nfact);

void         vm_global_free  (vm_global_t *g);
void         vm_global_print (FILE *fp, vm_global_t *g);


GSList      *vm_fact_lookup(char *name);
void         vm_fact_reset (OhmFact *fact);
OhmFact     *vm_fact_dup   (OhmFact *src, char *name);
OhmFact     *vm_fact_copy  (OhmFact *dst, OhmFact *src);
OhmFact     *vm_fact_update(OhmFact *dst, OhmFact *src);

void         vm_fact_remove(char *name);
void         vm_fact_remove_instance(OhmFact *fact);

void         vm_fact_insert(OhmFact *fact);

int          vm_fact_set_field  (vm_state_t *vm, OhmFact *fact, char *field,
                                 int type, vm_value_t *value);
int          vm_fact_get_field  (vm_state_t *vm, OhmFact *fact, char *field,
                                 vm_value_t *value);
int          vm_fact_match_field(vm_state_t *vm, OhmFact *fact, char *field,
                                 GValue *gval, int type, vm_value_t *value);

int          vm_fact_collect_fields(OhmFact *f, char **fields, int nfield,
                                    GValue **values);
int          vm_fact_matches       (OhmFact *f, char **fields, GValue **values,
                                    int nfield);
int          vm_global_find_first(vm_global_t *g,
                                  char **fields, GValue **values, int nfield);
int          vm_global_find_next(vm_global_t *g, int idx,
                                 char **fields, GValue **values, int nfield);

void vm_fact_print(FILE *fp, OhmFact *fact);


/* vm-local.c */
int vm_scope_push(vm_state_t *vm);
int vm_scope_pop (vm_state_t *vm);

int vm_scope_set(vm_scope_t *scope, int id, int type, vm_value_t value);
int vm_scope_get(vm_scope_t *scope, int id, vm_value_t *value);

int  vm_set_varname  (vm_state_t *vm, int id, const char *name);
void vm_free_varnames(vm_state_t *vm);


/* vm-debug.c */
int vm_dump_chunk(vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_instr(uintptr_t **pc, char *buf, size_t size, int indent);

/* vm-log.c */
void vm_set_logger(void (*logger)(vm_log_level_t, const char *, va_list));
void vm_log(vm_log_level_t level, const char *format, ...);
vm_log_level_t vm_set_log_level(vm_log_level_t level);



#endif /* __DRES_VM_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
