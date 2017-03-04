#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "stdlib.h"
#include "php.h"
#include "php_panda.h"
#include "panda_stack.h"
#include "ext/standard/php_array.h"

ZEND_EXTERN_MODULE_GLOBALS(panda);

#if PHP_VERSION_ID < 50500
static void panda_stack_execute (zend_op_array *ops TSRMLS_DC) {
#else
static void panda_stack_execute_ex (zend_execute_data *execute_data TSRMLS_DC) {
    zend_op_array *ops = execute_data->op_array;
#endif
    char *func_name = NULL;
    zend_execute_data *data = EG(current_execute_data);

    func_name = panda_stack_get_function_name(data);
    if (func_name == NULL) {
#if PHP_VERSION_ID < 50500
    _panda_stack_execute(ops TSRMLS_CC);
#else
    _panda_stack_execute_ex(execute_data TSRMLS_CC);
#endif
    return;
    }

    PANDA_STACK_BEGIN_PROFILING(&PANDA_G(stack_entries), func_name, data);

#if PHP_VERSION_ID < 50500
    _panda_stack_execute(ops TSRMLS_CC);
#else
    _panda_stack_execute_ex(execute_data TSRMLS_CC);
#endif
    PANDA_STACK_END_PROFILING(&PANDA_G(stack_entries), data, NULL);
    efree(func_name);

}

int panda_stack_init_globals(TSRMLS_D)
{
    PANDA_ARRAY_INIT(PANDA_G(node_stack));
    PANDA_ARRAY_INIT(PANDA_G(stack_maps));
    PANDA_G(stack_entries) = NULL;
    PANDA_G(stack_prev_entry) = NULL;
    PANDA_G(stack_count) = 0;
    PANDA_G(stack_max_level) = 0;
    PANDA_G(stack_entry_id) = -1;

    return SUCCESS;
}

int panda_stack_destroy_globals(TSRMLS_D)
{
    PANDA_ARRAY_DESTROY(PANDA_G(stack_maps));
    PANDA_ARRAY_DESTROY(PANDA_G(node_stack));
    return SUCCESS;
}

int panda_stack_begin_profiling(TSRMLS_D)
{
    PANDA_STACK_BEGIN_PROFILING(&PANDA_G(stack_entries), PANDA_STACK_MAIN_FUNCTION, NULL);
    return SUCCESS;
}

int panda_stack_end_profiling(TSRMLS_D)
{
    //Program to join the die directive. so need while
    while (PANDA_G(stack_prev_entry)) {
        PANDA_STACK_END_PROFILING(&PANDA_G(stack_entries), NULL, NULL);
    }
    return SUCCESS;
}

int panda_stack_compose_node(TSRMLS_D)
{
    zval *zv = PANDA_G(node_stack);
    zend_hash_sort(Z_ARRVAL_P(PANDA_G(stack_maps)), zend_qsort, panda_array_stack_compare, 0 TSRMLS_CC);

    Z_ADDREF_P(PANDA_G(stack_maps));
    add_assoc_zval(zv, PANDA_NODE_STACK_MAPS, PANDA_G(stack_maps));
    add_assoc_long(zv, PANDA_NODE_STACK_COUNT, PANDA_G(stack_count));
    add_assoc_long(zv, PADNA_NODE_STACK_MAX_LEVEL, PANDA_G(stack_max_level));
    return SUCCESS;
}

int panda_stack_init_hooks(TSRMLS_C)
{
#if PHP_VERSION_ID < 50500
    _panda_stack_execute = zend_execute;
    zend_execute  = panda_stack_execute;
#else
    _panda_stack_execute_ex = zend_execute_ex;
    zend_execute_ex  = panda_stack_execute_ex;
#endif
    return SUCCESS;
}

int panda_stack_destroy_hooks(TSRMLS_C)
{
#if PHP_VERSION_ID < 50500
    zend_execute = _panda_stack_execute;
#else
    zend_execute_ex = _panda_stack_execute_ex;
#endif

    return SUCCESS;
}


int panda_stack_extract_entry_data(panda_stack_entry_t *entry, zend_execute_data *execute_data, zval *expend_data TSRMLS_DC)
{
    zval *zv = PANDA_G(stack_maps);
    zval **stack;

    int status = FAILURE;
    int64 wall_time_us = PANDA_STACK_GET_ENTRY_WALL_TIME(entry);
    int64 cpu_time_us = PANDA_STACK_GET_ENTRY_CPU_TIME(entry);
    int64 memory_usage = PANDA_STACK_GET_ENTRY_MEMORY_USAGE(entry);
    int64 memory_peak_usage = PANDA_STACK_GET_ENTRY_MEMORY_PEAK_USAGE(entry);

    PANDA_G(stack_count)++;
    if (entry->level > PANDA_G(stack_max_level)) {
        PANDA_G(stack_max_level) = entry->level;
    }

    do {
        if (wall_time_us < PANDA_G(config_limit_function_time)) {
           //break;
        }

        if (entry->level > PANDA_G(config_stack_max_levels)) {
            break;
        }

        if (PANDA_G(stack_count) > PANDA_G(config_stack_max_nodes)) {
            break;
        }

        if (zend_hash_index_find(Z_ARRVAL_P(zv), (ulong)entry->id, (void **)&stack) == SUCCESS) {
            PANDA_ASSOC_ARRAY_INC_NUM(*stack, PANDA_NODE_STACK_MAPS_CALL_TIME, 1);
            PANDA_ASSOC_ARRAY_INC_NUM(*stack, PANDA_NODE_STACK_MAPS_WALL_TIME_US, wall_time_us);
            PANDA_ASSOC_ARRAY_INC_NUM(*stack, PANDA_NODE_STACK_MAPS_CPU_TIME_US, cpu_time_us);
            PANDA_ASSOC_ARRAY_INC_NUM(*stack, PANDA_NODE_STACK_MAPS_MEMORY_USAGE, memory_usage);
            PANDA_ASSOC_ARRAY_INC_NUM(*stack, PANDA_NODE_STACK_MAPS_MEMORY_PEAK_USAGE, memory_peak_usage);
            add_assoc_long(*stack, PANDA_NODE_STACK_MAPS_END_TIME_MS, entry->end_us / 1000);
        } else {
            zval *new_stack;
            PANDA_ARRAY_INIT(new_stack);
            add_assoc_string(new_stack, PANDA_NODE_STACK_MAPS_NAME, entry->name, 1);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_ID, entry->id);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_PARENT_ID, entry->parent_id);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_CALL_TIME, PANDA_STACK_ENTRY_DEFAULT_CALL_TIME);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_WALL_TIME_US, wall_time_us);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_CPU_TIME_US, cpu_time_us);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_MEMORY_USAGE, memory_usage);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_MEMORY_PEAK_USAGE, memory_peak_usage);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_LEVEL, entry->level);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_START_TIME_MS, entry->start_us / 1000);
            add_assoc_long(new_stack, PANDA_NODE_STACK_MAPS_END_TIME_MS, entry->end_us / 1000);
            if (expend_data) {
                add_assoc_zval(new_stack, PANDA_NODE_STACK_MAPS_REFRENCES, expend_data);
                Z_ADDREF_P(expend_data);
            }

            add_index_zval(zv, entry->id, new_stack);
            Z_ADDREF_P(new_stack);
            PANDA_ARRAY_DESTROY(new_stack);
        }
        status = SUCCESS;
    } while (0);

    return status;
}


uint64 panda_stack_cycle_timer(TSRMLS_D)
{
    struct timeval result;
    gettimeofday(&result, NULL);

    uint64 val;
    val = 1000000 * result.tv_sec + result.tv_usec;
    return val;
}

int panda_stack_free_entry(panda_stack_entry_t *entry TSRMLS_DC)
{
    if (PANDA_G(stack_prev_entry)) {
        PANDA_STACK_DESTROY_ENTRY(PANDA_G(stack_prev_entry));
    }

    if (entry->prev_entry == NULL) {
        PANDA_STACK_DESTROY_ENTRY(entry);
        PANDA_G(stack_prev_entry) = NULL;
    } else {
        PANDA_G(stack_prev_entry) = entry;
    }
    return PANDA_TRUE;
}

int panda_stack_get_entry_id(panda_stack_entry_t *entry, int must_new TSRMLS_DC)
{
    if (PANDA_G(config_stack_merge) && !must_new && PANDA_G(stack_prev_entry) != NULL && (PANDA_G(stack_prev_entry)->level == entry->level) &&
        (strncmp(PANDA_G(stack_prev_entry)->name, entry->name, entry->name_len) == 0)) {
        PANDA_G(stack_entry_id) = PANDA_G(stack_prev_entry)->id;
    } else {
        PANDA_G(stack_entry_id)++;
    }
    return PANDA_G(stack_entry_id);
}

void **panda_stack_get_execute_parameters(zend_execute_data *execute_data TSRMLS_DC)
{
    void **p;
    p = execute_data->function_state.arguments;
#if PHP_VERSION_ID >= 50500
    if (p == NULL) {
        p = (*execute_data).prev_execute_data->function_state.arguments;
    }
#endif
    return p;
}

int panda_stack_get_execute_paramters_count(zend_execute_data *execute_data TSRMLS_DC)
{
    void **p = panda_stack_get_execute_parameters(execute_data TSRMLS_CC);
    return (int)(zend_uintptr_t) *p;
}


zval *panda_stack_get_execute_param(zend_execute_data *execute_data, int n TSRMLS_DC)
{
    void **p = panda_stack_get_execute_parameters(execute_data TSRMLS_CC);
    int paramters_count = (int)(zend_uintptr_t) *p;
    return *(p - (paramters_count - n));
}

ulong panda_stack_get_resource_hash(const char* host, int port TSRMLS_DC)
{
    char *host_port_str;
    int host_port_str_len = spprintf(&host_port_str, 0, "%s%d", host, port);
    ulong hash = zend_inline_hash_func(host_port_str, host_port_str_len);
    efree(host_port_str);
    return hash;
}

static inline long panda_stack_get_us_interval(struct timeval *start, struct timeval *end TSRMLS_DC)
{
    return (((end->tv_sec - start->tv_sec) * 1000000) + (end->tv_usec - start->tv_usec));
}

char *panda_stack_get_function_name(zend_execute_data *execute_data TSRMLS_DC)
{
    char *function_name = NULL;
    const char *cls = NULL;
    const char *func = NULL;
    const char *sepa = PANDA_STACK_CLS_NAME_SPRA_QUOTATION;
    do {
        if (!execute_data) {
            break;
        }

        zend_function *current_func = execute_data->function_state.function;
        func = current_func->common.function_name;

        if (!func) {
            break;
        }

        if (execute_data->object) {
            cls = Z_OBJCE(*execute_data->object)->name;
        } else if (current_func->common.scope) {
            cls = current_func->common.scope->name;
        }

        if (cls) {
            spprintf(&function_name, 0, "%s%s%s", cls, sepa, func);
        } else {
            function_name = estrdup(func);
        }
    } while (0);
    return function_name;
}

static int panda_array_stack_compare(const void *a, const void *b TSRMLS_DC)
{
    Bucket *f;
    Bucket *s;
    zval result;
    zval first;
    zval second;

    f = *((Bucket **) a);
    s = *((Bucket **) b);

    if (f->nKeyLength == 0) {
        Z_TYPE(first) = IS_LONG;
        Z_LVAL(first) = f->h;
    } else {
        Z_TYPE(first) = IS_STRING;
        Z_STRVAL(first) = (char*)f->arKey;
        Z_STRLEN(first) = f->nKeyLength - 1;
    }

    if (s->nKeyLength == 0) {
        Z_TYPE(second) = IS_LONG;
        Z_LVAL(second) = s->h;
    } else {
        Z_TYPE(second) = IS_STRING;
        Z_STRVAL(second) = (char*)s->arKey;
        Z_STRLEN(second) = s->nKeyLength - 1;
    }

    if (compare_function(&result, &first, &second TSRMLS_CC) == FAILURE) {
        return 0;
    }

    if (Z_TYPE(result) == IS_DOUBLE) {
        if (Z_DVAL(result) < 0) {
            return -1;
        } else if (Z_DVAL(result) > 0) {
            return 1;
        } else {
            return 0;
        }
    }

    convert_to_long(&result);

    if (Z_LVAL(result) < 0) {
        return -1;
    } else if (Z_LVAL(result) > 0) {
        return 1;
    }

    return 0;
}

