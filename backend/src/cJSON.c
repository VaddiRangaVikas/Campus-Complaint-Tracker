#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>
#include "cJSON.h"

static const char *global_ep = NULL;

const char *cJSON_GetErrorPtr(void)
{
    return global_ep;
}

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

static char* cJSON_strdup(const char* str)
{
    size_t len;
    char* copy;

    if (!str) return NULL;
    len = strlen(str) + 1;
    if (!(copy = (char*)cJSON_malloc(len))) return NULL;
    memcpy(copy, str, len);
    return copy;
}

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks) {
        cJSON_malloc = malloc;
        cJSON_free = free;
        return;
    }
    cJSON_malloc = (hooks->malloc_fn)?hooks->malloc_fn:malloc;
    cJSON_free   = (hooks->free_fn)?hooks->free_fn:free;
}

static cJSON *cJSON_New_Item(void)
{
    cJSON* node = (cJSON*)cJSON_malloc(sizeof(cJSON));
    if (node) memset(node, 0, sizeof(cJSON));
    return node;
}

void cJSON_Delete(cJSON *c)
{
    cJSON *next;
    while (c) {
        next = c->next;
        if (!(c->type & cJSON_IsReference) && c->child) cJSON_Delete(c->child);
        if (!(c->type & cJSON_IsReference) && c->valuestring) cJSON_free(c->valuestring);
        if (!(c->type & cJSON_StringIsConst) && c->string) cJSON_free(c->string);
        cJSON_free(c);
        c = next;
    }
}

static const char *parse_value(cJSON *item, const char *value);
static char *print_value(const cJSON *item, int depth, int fmt);
static const char *parse_array(cJSON *item, const char *value);
static char *print_array(const cJSON *item, int depth, int fmt);
static const char *parse_object(cJSON *item, const char *value);
static char *print_object(const cJSON *item, int depth, int fmt);

static const char *skip(const char *in) {
    while (in && *in && ((unsigned char)*in <= 32)) in++;
    return in;
}

cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, int require_null_terminated)
{
    const char *end = 0;
    cJSON *c = cJSON_New_Item();
    global_ep = 0;
    if (!c) return 0;

    end = parse_value(c, skip(value));
    if (!end) { cJSON_Delete(c); return 0; }

    if (require_null_terminated) {
        end = skip(end);
        if (*end) { cJSON_Delete(c); global_ep = end; return 0; }
    }
    if (return_parse_end) *return_parse_end = end;
    return c;
}

cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);
}

char *cJSON_Print(const cJSON *item)
{
    return print_value(item, 0, 1);
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    return print_value(item, 0, 0);
}

static const char *parse_number(cJSON *item, const char *num)
{
    double n = 0;
    char *endptr;
    n = strtod(num, &endptr);
    if (num == endptr) return 0;
    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return endptr;
}

static char *print_number(const cJSON *item)
{
    char *str = (char*)cJSON_malloc(64);
    if (!str) return 0;
    if (fabs(((double)item->valueint) - item->valuedouble) <= DBL_EPSILON && item->valuedouble >= INT_MIN && item->valuedouble <= INT_MAX)
        snprintf(str, 64, "%d", item->valueint);
    else
        snprintf(str, 64, "%f", item->valuedouble);
    return str;
}

static const char *parse_string(cJSON *item, const char *str)
{
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;
    if (*str != '\"') { global_ep = str; return 0; }

    while (*ptr != '\"' && *ptr && ++len) {
        if (*ptr++ == '\\') ptr++;
    }

    out = (char*)cJSON_malloc(len + 1);
    if (!out) return 0;

    ptr = str + 1;
    ptr2 = out;
    while (*ptr != '\"' && *ptr) {
        if (*ptr != '\\') {
            *ptr2++ = *ptr++;
        } else {
            ptr++;
            switch (*ptr) {
                case 'b': *ptr2++ = '\b'; break;
                case 'f': *ptr2++ = '\f'; break;
                case 'n': *ptr2++ = '\n'; break;
                case 'r': *ptr2++ = '\r'; break;
                case 't': *ptr2++ = '\t'; break;
                case '\"': case '\\': case '/': *ptr2++ = *ptr; break;
                default: *ptr2++ = *ptr; break;
            }
            ptr++;
        }
    }
    *ptr2 = 0;
    if (*ptr == '\"') ptr++;
    item->valuestring = out;
    item->type = cJSON_String;
    return ptr;
}

static char *print_string_ptr(const char *str)
{
    const char *ptr;
    char *ptr2, *out;
    int len = 0, flag = 0;
    unsigned char token;

    if (!str) return cJSON_strdup("\"\"");
    ptr = str;
    while ((token = *ptr) && ++len) {
        if (strchr("\"\\\b\f\n\r\t", token)) len++;
        else if (token < 32) flag = 1;
        ptr++;
    }
    if (flag) len += 5;

    out = (char*)cJSON_malloc(len + 3);
    if (!out) return 0;

    ptr2 = out;
    *ptr2++ = '\"';
    ptr = str;
    while (*ptr) {
        if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\') {
            *ptr2++ = *ptr++;
        } else {
            *ptr2++ = '\\';
            switch (token = *ptr++) {
                case '\\': *ptr2++ = '\\'; break;
                case '\"': *ptr2++ = '\"'; break;
                case '\b': *ptr2++ = 'b'; break;
                case '\f': *ptr2++ = 'f'; break;
                case '\n': *ptr2++ = 'n'; break;
                case '\r': *ptr2++ = 'r'; break;
                case '\t': *ptr2++ = 't'; break;
                default: snprintf(ptr2, 6, "u%04x", token); ptr2 += 5; break;
            }
        }
    }
    *ptr2++ = '\"';
    *ptr2++ = 0;
    return out;
}

static char *print_string(const cJSON *item)
{
    return print_string_ptr(item->valuestring);
}

static const char *parse_value(cJSON *item, const char *value)
{
    if (!value) return 0;
    if (!strncmp(value, "null", 4)) { item->type = cJSON_NULL; return value + 4; }
    if (!strncmp(value, "false", 5)) { item->type = cJSON_False; return value + 5; }
    if (!strncmp(value, "true", 4)) { item->type = cJSON_True; item->valueint = 1; return value + 4; }
    if (*value == '\"') { return parse_string(item, value); }
    if (*value == '-' || (*value >= '0' && *value <= '9')) { return parse_number(item, value); }
    if (*value == '[') { return parse_array(item, value); }
    if (*value == '{') { return parse_object(item, value); }

    global_ep = value;
    return 0;
}

static char *print_value(const cJSON *item, int depth, int fmt)
{
    char *out = 0;
    if (!item) return 0;
    switch ((item->type) & 255) {
        case cJSON_NULL: out = cJSON_strdup("null"); break;
        case cJSON_False: out = cJSON_strdup("false"); break;
        case cJSON_True: out = cJSON_strdup("true"); break;
        case cJSON_Number: out = print_number(item); break;
        case cJSON_String: out = print_string(item); break;
        case cJSON_Array: out = print_array(item, depth, fmt); break;
        case cJSON_Object: out = print_object(item, depth, fmt); break;
        case cJSON_Raw: out = cJSON_strdup(item->valuestring ? item->valuestring : ""); break;
    }
    return out;
}

static const char *parse_array(cJSON *item, const char *value)
{
    cJSON *child;
    if (*value != '[') { global_ep = value; return 0; }

    item->type = cJSON_Array;
    value = skip(value + 1);
    if (*value == ']') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child) return 0;
    value = skip(parse_value(child, skip(value)));
    if (!value) return 0;

    while (*value == ',') {
        cJSON *new_item;
        if (!(new_item = cJSON_New_Item())) return 0;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) return 0;
    }

    if (*value == ']') return value + 1;
    global_ep = value;
    return 0;
}

static char *print_array(const cJSON *item, int depth, int fmt)
{
    char **entries;
    char *out = 0, *ptr, *ret;
    int len = 5;
    cJSON *child = item->child;
    int numentries = 0, i = 0, fail = 0;

    while (child) numentries++, child = child->next;
    if (!numentries) {
        out = (char*)cJSON_malloc(3);
        if (out) strcpy(out, "[]");
        return out;
    }
    entries = (char**)cJSON_malloc(numentries * sizeof(char*));
    if (!entries) return 0;
    memset(entries, 0, numentries * sizeof(char*));

    child = item->child;
    while (child) {
        ret = print_value(child, depth + 1, fmt);
        entries[i++] = ret;
        if (ret) len += (int)strlen(ret) + 2 + (fmt ? 1 : 0); else fail = 1;
        child = child->next;
    }

    if (!fail) out = (char*)cJSON_malloc(len);
    if (!out) fail = 1;

    if (fail) {
        for (i = 0; i < numentries; i++) if (entries[i]) cJSON_free(entries[i]);
        cJSON_free(entries);
        return 0;
    }

    *out = '[';
    ptr = out + 1;
    *ptr = 0;
    for (i = 0; i < numentries; i++) {
        strcpy(ptr, entries[i]);
        ptr += strlen(entries[i]);
        if (i != numentries - 1) { *ptr++ = ','; if (fmt) *ptr++ = ' '; *ptr = 0; }
        cJSON_free(entries[i]);
    }
    cJSON_free(entries);
    *ptr++ = ']';
    *ptr++ = 0;
    return out;
}

static const char *parse_object(cJSON *item, const char *value)
{
    cJSON *child;
    if (*value != '{') { global_ep = value; return 0; }

    item->type = cJSON_Object;
    value = skip(value + 1);
    if (*value == '}') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child) return 0;
    value = skip(parse_string(child, skip(value)));
    if (!value) return 0;
    child->string = child->valuestring;
    child->valuestring = 0;
    if (*value != ':') { global_ep = value; return 0; }
    value = skip(parse_value(child, skip(value + 1)));
    if (!value) return 0;

    while (*value == ',') {
        cJSON *new_item;
        if (!(new_item = cJSON_New_Item())) return 0;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_string(child, skip(value + 1)));
        if (!value) return 0;
        child->string = child->valuestring;
        child->valuestring = 0;
        if (*value != ':') { global_ep = value; return 0; }
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) return 0;
    }

    if (*value == '}') return value + 1;
    global_ep = value;
    return 0;
}

static char *print_object(const cJSON *item, int depth, int fmt)
{
    char **entries = 0, **names = 0;
    char *out = 0, *ptr, *ret, *str;
    int len = 7, i = 0, j;
    cJSON *child = item->child;
    int numentries = 0, fail = 0;

    while (child) numentries++, child = child->next;
    if (!numentries) {
        out = (char*)cJSON_malloc(3);
        if (out) strcpy(out, "{}");
        return out;
    }
    entries = (char**)cJSON_malloc(numentries * sizeof(char*));
    if (!entries) return 0;
    names = (char**)cJSON_malloc(numentries * sizeof(char*));
    if (!names) { cJSON_free(entries); return 0; }
    memset(entries, 0, sizeof(char*) * numentries);
    memset(names, 0, sizeof(char*) * numentries);

    child = item->child;
    depth++;
    while (child) {
        names[i] = str = print_string_ptr(child->string);
        entries[i++] = ret = print_value(child, depth, fmt);
        if (str && ret) len += (int)strlen(ret) + (int)strlen(str) + 2 + (fmt ? 2 : 0); else fail = 1;
        child = child->next;
    }

    if (!fail) out = (char*)cJSON_malloc(len);
    if (!out) fail = 1;

    if (fail) {
        for (i = 0; i < numentries; i++) {
            if (names[i]) cJSON_free(names[i]);
            if (entries[i]) cJSON_free(entries[i]);
        }
        cJSON_free(names);
        cJSON_free(entries);
        return 0;
    }

    *out = '{';
    ptr = out + 1;
    *ptr = 0;
    for (i = 0; i < numentries; i++) {
        strcpy(ptr, names[i]);
        ptr += strlen(names[i]);
        *ptr++ = ':';
        if (fmt) *ptr++ = ' ';
        strcpy(ptr, entries[i]);
        ptr += strlen(entries[i]);
        if (i != numentries - 1) *ptr++ = ',';
        if (fmt) *ptr++ = ' ';
        *ptr = 0;
        cJSON_free(names[i]);
        cJSON_free(entries[i]);
    }
    cJSON_free(names);
    cJSON_free(entries);
    *ptr++ = '}';
    *ptr++ = 0;
    return out;
}

int cJSON_GetArraySize(const cJSON *array)
{
    cJSON *c = array ? array->child : 0;
    int i = 0;
    while (c) i++, c = c->next;
    return i;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    cJSON *c = array ? array->child : 0;
    while (c && index > 0) index--, c = c->next;
    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    cJSON *c = object ? object->child : 0;
    while (c && c->string && strcasecmp(c->string, string)) c = c->next;
    return c;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    cJSON *c = object ? object->child : 0;
    while (c && c->string && strcmp(c->string, string)) c = c->next;
    return c;
}

int cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

cJSON *cJSON_CreateNull(void)   { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_NULL; return item; }
cJSON *cJSON_CreateTrue(void)   { cJSON *item = cJSON_New_Item(); if (item) { item->type = cJSON_True; item->valueint = 1; } return item; }
cJSON *cJSON_CreateFalse(void)  { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_False; return item; }
cJSON *cJSON_CreateBool(int b)  { return b ? cJSON_CreateTrue() : cJSON_CreateFalse(); }
cJSON *cJSON_CreateNumber(double num) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}
cJSON *cJSON_CreateString(const char *string) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_String;
        item->valuestring = cJSON_strdup(string);
    }
    return item;
}
cJSON *cJSON_CreateRaw(const char *raw) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_Raw;
        item->valuestring = cJSON_strdup(raw);
    }
    return item;
}
cJSON *cJSON_CreateArray(void)  { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Array; return item; }
cJSON *cJSON_CreateObject(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Object; return item; }

void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *c;
    if (!item) return;
    if (!array->child) {
        array->child = item;
    } else {
        c = array->child;
        while (c && c->next) c = c->next;
        c->next = item;
        item->prev = c;
    }
}

void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    if (!item) return;
    if (item->string) cJSON_free(item->string);
    item->string = cJSON_strdup(string);
    cJSON_AddItemToArray(object, item);
}

void cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item)
{
    cJSON_AddItemToObject(object, string, item);
}

cJSON *cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item)
{
    if (!parent || !item) return 0;
    if (item->prev) item->prev->next = item->next;
    if (item->next) item->next->prev = item->prev;
    if (item == parent->child) parent->child = item->next;
    item->prev = item->next = 0;
    return item;
}

cJSON *cJSON_DetachItemFromArray(cJSON *array, int which)
{
    cJSON *c = array ? array->child : 0;
    while (c && which > 0) c = c->next, which--;
    if (!c) return 0;
    return cJSON_DetachItemViaPointer(array, c);
}

void cJSON_DeleteItemFromArray(cJSON *array, int which)
{
    cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}

cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *string)
{
    cJSON *to_detach = cJSON_GetObjectItem(object, string);
    return cJSON_DetachItemViaPointer(object, to_detach);
}

void cJSON_DeleteItemFromObject(cJSON *object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObject(object, string));
}

cJSON *cJSON_Duplicate(const cJSON *item, int recurse)
{
    cJSON *newitem, *cptr, *nptr = 0, *newchild;
    if (!item) return 0;
    newitem = cJSON_New_Item();
    if (!newitem) return 0;
    newitem->type = item->type & (~cJSON_IsReference);
    newitem->valueint = item->valueint;
    newitem->valuedouble = item->valuedouble;
    if (item->valuestring) {
        newitem->valuestring = cJSON_strdup(item->valuestring);
        if (!newitem->valuestring) { cJSON_Delete(newitem); return 0; }
    }
    if (item->string) {
        newitem->string = cJSON_strdup(item->string);
        if (!newitem->string) { cJSON_Delete(newitem); return 0; }
    }
    if (!recurse) return newitem;
    cptr = item->child;
    while (cptr) {
        newchild = cJSON_Duplicate(cptr, 1);
        if (!newchild) { cJSON_Delete(newitem); return 0; }
        if (nptr) { nptr->next = newchild; newchild->prev = nptr; nptr = newchild; }
        else { newitem->child = newchild; nptr = newchild; }
        cptr = cptr->next;
    }
    return newitem;
}

const char *cJSON_Version(void) { return "1.7.15"; }
