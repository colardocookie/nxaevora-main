/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_VALIDATION_LINES_H
#define SNX_VK_VALIDATION_LINES_H
/* What the game's log says of the validation layer under the prefix a report keeps, with the runtime's
   validation option only. Log only and pure: no Vulkan header, lines go out through a function of the caller.
   The names kept here are names of variables found by a pattern in the layer's text, not identities of images. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VALIDATION_RULES 32      /* rules listed, by whole name and number */
#define VALIDATION_RULE_NAME 127 /* a rule's name longer than this is not listed */
#define VALIDATION_NAMES 64      /* variable names listed */
#define VALIDATION_NAME 63       /* a variable's name longer than this is not listed */
#define VALIDATION_UNMATCHED 64  /* callbacks without a match of the pattern that are printed whole */
#define VALIDATION_SCAN (1u << 20) /* bytes of a callback's text that are looked through */
#define VALIDATION_LINE 1800     /* bytes of an emitted line at most, the marks written out, before the prefix */

/* Each way something can be lost. Each prints a line of its own the first time it happens. */
enum {
    VALIDATION_NO_NAME,     /* a callback whose rule has no name: it is in no rule's count */
    VALIDATION_RULE_LONG,   /* a rule's name too long to list */
    VALIDATION_RULES_FULL,  /* a rule that found no room in the table */
    VALIDATION_NAME_LONG,   /* a variable's name too long to list */
    VALIDATION_NAMES_FULL,  /* a variable's name that found no room in the table */
    VALIDATION_UNCLOSED,    /* a quoted name that does not close in what was looked through */
    VALIDATION_UNSCANNED,   /* a text not looked through to its end */
    VALIDATION_UNPRINTED,   /* a callback without a match that is not printed any more */
    VALIDATION_CUT,         /* a printed line that was cut */
    VALIDATION_SATURATED,   /* a counter that stopped at its largest value */
    VALIDATION_LOSSES
};
typedef struct {
    uint64_t callbacks, printed;   /* received, and printed whole on a line of their own */
    uint64_t unmatched, unlisted;  /* without a match of the pattern, and of a rule that is not in the table */
    uint64_t next;                 /* the count at which the next count line is due: 1, 2, 4, 8 and so on */
    unsigned rules, names;
    struct {
        char name[VALIDATION_RULE_NAME + 1];
        int32_t number;
        uint64_t count;
    } rule[VALIDATION_RULES];
    char name[VALIDATION_NAMES][VALIDATION_NAME + 1];
    uint64_t loss[VALIDATION_LOSSES]; /* how often each happened */
} ValidationLines;
typedef void (*ValidationEmit)(void* user, const char* line);

/* One byte for a log line: printable ASCII as it is, a backslash doubled, any other byte as a mark (a backslash
   with n, r or t, or with x and two hexadecimal digits). The self-test's marks. At most four bytes: how many. */
static inline size_t validation_lines_mark(unsigned char c, char* out) {
    static const char digits[] = "0123456789abcdef";
    if (c >= 0x20 && c < 0x7f && c != '\\') {
        out[0] = (char)c;
        return 1;
    }
    out[0] = '\\';
    out[1] = c == '\\' ? '\\' : c == '\n' ? 'n' : c == '\r' ? 'r' : c == '\t' ? 't' : 'x';
    if (out[1] != 'x') return 2;
    out[2] = digits[c >> 4];
    out[3] = digits[c & 15];
    return 4;
}
/* bytes of text with their marks into line from at, while a whole mark fits below room: how many bytes went in */
static inline size_t validation_lines_put(char* line, size_t* at, size_t room, const char* text, size_t length) {
    size_t i = 0;
    for (; i < length && *at + 4 < room; i++) *at += validation_lines_mark((unsigned char)text[i], line + *at);
    line[*at] = 0;
    return i;
}
/* a counter goes up by one and stops at its largest value: 1 when it stopped there now or before */
static inline int validation_lines_count(uint64_t* counter) {
    if (*counter == UINT64_MAX) return 1;
    return ++*counter == UINT64_MAX;
}
static inline const char* validation_lines_loss(int which) {
    switch (which) {
        case VALIDATION_NO_NAME: return "a callback whose rule has no name, which is in no rule's count";
        case VALIDATION_RULE_LONG: return "a rule's name too long to list";
        case VALIDATION_RULES_FULL: return "a rule that found no room in the table of rules";
        case VALIDATION_NAME_LONG: return "a variable's name too long to list";
        case VALIDATION_NAMES_FULL: return "a variable's name that found no room in the table of names";
        case VALIDATION_UNCLOSED: return "a quoted variable's name that does not close in the text looked through";
        case VALIDATION_UNSCANNED: return "a text that was not looked through to its end";
        case VALIDATION_UNPRINTED: return "a callback without a match of the pattern that is not printed any more";
        case VALIDATION_CUT: return "a printed line that was cut";
        default: return "a counter that stopped at its largest value";
    }
}
/* a loss happened: counted, and said in a line of its own the first time */
static inline void validation_lines_lost(ValidationLines* v, int which, ValidationEmit emit, void* user) {
    char line[256];
    const int first = !v->loss[which];
    const int stuck = validation_lines_count(&v->loss[which]);
    if (first) {
        snprintf(line, sizeof line, "LIMIT MET, first at callback %llu: %s. From here on the lines do not show "
                                    "every rule and every name of this run",
                 (unsigned long long)v->callbacks, validation_lines_loss(which));
        emit(user, line);
    }
    if (stuck && which != VALIDATION_SATURATED) validation_lines_lost(v, VALIDATION_SATURATED, emit, user);
}
/* a counter of the tables goes up, and a stop at its largest value is a loss */
static inline void validation_lines_up(ValidationLines* v, uint64_t* counter, ValidationEmit emit, void* user) {
    if (validation_lines_count(counter)) validation_lines_lost(v, VALIDATION_SATURATED, emit, user);
}

/* The variable names of one text by the layer's pattern, the word variable and a quoted name. 1 when a name
   was found that the table did not hold. matched: the pattern occurred at all. */
static inline int validation_lines_names(ValidationLines* v, const char* text, size_t length, int* matched,
                                         ValidationEmit emit, void* user) {
    static const char pattern[] = "variable \"";
    const size_t width = sizeof pattern - 1;
    int fresh = 0;
    *matched = 0;
    for (size_t i = 0; i + width <= length;) {
        if (memcmp(text + i, pattern, width)) {
            i++;
            continue;
        }
        *matched = 1;
        const size_t from = i + width;
        size_t to = from;
        while (to < length && text[to] != '"') to++;
        if (to == length) { /* no closing quote: nothing after it can be a name */
            validation_lines_lost(v, VALIDATION_UNCLOSED, emit, user);
            break;
        }
        i = to + 1;
        const size_t n = to - from;
        if (n > VALIDATION_NAME) {
            validation_lines_lost(v, VALIDATION_NAME_LONG, emit, user);
            continue;
        }
        unsigned k = 0;
        while (k < v->names && (strlen(v->name[k]) != n || memcmp(v->name[k], text + from, n))) k++;
        if (k < v->names) continue;
        if (k == VALIDATION_NAMES) {
            validation_lines_lost(v, VALIDATION_NAMES_FULL, emit, user);
            continue;
        }
        memcpy(v->name[k], text + from, n);
        v->name[k][n] = 0;
        v->names++;
        fresh = 1;
    }
    return fresh;
}

/* The count lines, due when the count of callbacks reaches 1, 2, 4, 8 and so on: the total, every limit met so
   far, and each listed rule's count on a line of its own. */
static inline void validation_lines_counts(ValidationLines* v, ValidationEmit emit, void* user) {
    char line[VALIDATION_LINE + 1];
    size_t at = (size_t)snprintf(line, sizeof line,
                                 "%llu callbacks so far, %llu of them printed whole, %u rules and %u variable names "
                                 "listed. Written at 1, 2, 4, 8 and so on: the last such line of a log is within a "
                                 "factor of two of the callbacks received. Limits met:",
                                 (unsigned long long)v->callbacks, (unsigned long long)v->printed, v->rules, v->names);
    int any = 0;
    for (int k = 0; k < VALIDATION_LOSSES; k++) {
        if (!v->loss[k]) continue;
        any = 1;
        at += (size_t)snprintf(line + at, sizeof line - at, " %s (%llu times).", validation_lines_loss(k),
                               (unsigned long long)v->loss[k]);
        if (at >= sizeof line) at = sizeof line - 1; /* every limit's words fit: a guard */
    }
    if (!any) snprintf(line + at, sizeof line - at, " none");
    emit(user, line);
    for (unsigned k = 0; k < v->rules; k++) {
        at = (size_t)snprintf(line, sizeof line, "at %llu callbacks, rule %u of %u: %llu callbacks of ",
                              (unsigned long long)v->callbacks, k + 1, v->rules,
                              (unsigned long long)v->rule[k].count);
        validation_lines_put(line, &at, sizeof line - 32, v->rule[k].name, strlen(v->rule[k].name));
        snprintf(line + at, sizeof line - at, " (%d)", (int)v->rule[k].number);
        emit(user, line);
    }
    if (v->unlisted) {
        snprintf(line, sizeof line, "at %llu callbacks: %llu callbacks of rules that are not listed",
                 (unsigned long long)v->callbacks, (unsigned long long)v->unlisted);
        emit(user, line);
    }
}

/* One callback of the layer. name may be NULL (a rule without a name) and so may text. Everything it prints
   goes through emit, a line at a time without a prefix or a line's end. Not safe by itself against a second
   call at the same time: validation_lines_said holds the lock. */
static inline void validation_lines_callback(ValidationLines* v, const char* name, int32_t number, const char* text,
                                             ValidationEmit emit, void* user) {
    char line[VALIDATION_LINE + 1];
    size_t length = 0, name_length = 0;
    int first = 0, matched = 0, whole = 1;
    if (!v->next) v->next = 1;
    validation_lines_up(v, &v->callbacks, emit, user);
    /* the text's length, looked for no further than what is looked through */
    while (text && length <= VALIDATION_SCAN && text[length]) length++;
    if (length > VALIDATION_SCAN) {
        length = VALIDATION_SCAN;
        whole = 0;
        validation_lines_lost(v, VALIDATION_UNSCANNED, emit, user);
    }
    /* the rule: by its whole name and its number */
    while (name && name_length <= VALIDATION_RULE_NAME && name[name_length]) name_length++;
    if (!name) {
        validation_lines_up(v, &v->unlisted, emit, user);
        validation_lines_lost(v, VALIDATION_NO_NAME, emit, user);
    } else if (name_length > VALIDATION_RULE_NAME) {
        validation_lines_up(v, &v->unlisted, emit, user);
        validation_lines_lost(v, VALIDATION_RULE_LONG, emit, user);
    } else {
        unsigned k = 0;
        while (k < v->rules && (v->rule[k].number != number || strcmp(v->rule[k].name, name))) k++;
        if (k == v->rules && k < VALIDATION_RULES) {
            memcpy(v->rule[k].name, name, name_length + 1);
            v->rule[k].number = number;
            v->rules++;
            first = 1;
        }
        if (k < VALIDATION_RULES) validation_lines_up(v, &v->rule[k].count, emit, user);
        else {
            validation_lines_up(v, &v->unlisted, emit, user);
            validation_lines_lost(v, VALIDATION_RULES_FULL, emit, user);
        }
    }
    const int fresh = validation_lines_names(v, text ? text : "", length, &matched, emit, user);
    int unmatched_shown = 0;
    if (!matched) {
        validation_lines_up(v, &v->unmatched, emit, user);
        unmatched_shown = v->unmatched <= VALIDATION_UNMATCHED;
        if (!unmatched_shown) validation_lines_lost(v, VALIDATION_UNPRINTED, emit, user);
    }
    /* Printed whole when it is the first of its rule, the first that names a variable's name not seen before,
       or has no match of the pattern (those up to a count). */
    if (first || fresh || unmatched_shown) {
        validation_lines_up(v, &v->printed, emit, user);
        size_t at = (size_t)snprintf(line, sizeof line, "callback %llu, %s%s%s%s%s, rule ",
                                     (unsigned long long)v->callbacks, first ? "the first of its rule" : "",
                                     first && (fresh || unmatched_shown) ? " and " : "",
                                     fresh ? "the first naming a variable's name not seen before" : "",
                                     fresh && unmatched_shown ? " and " : "",
                                     unmatched_shown ? "no variable's name found in it by the pattern" : "");
        if (!name) at += (size_t)snprintf(line + at, sizeof line - at, "without a name");
        else
            validation_lines_put(line, &at, sizeof line, name,
                                 name_length > VALIDATION_RULE_NAME ? VALIDATION_RULE_NAME : name_length);
        at += (size_t)snprintf(line + at, sizeof line - at, "%s (%d), %s%zu bytes of text: ",
                               name && name_length > VALIDATION_RULE_NAME ? " [the name's first 127 bytes]" : "",
                               (int)number, whole ? "" : "more than ", length);
        /* the text with its marks, as much as leaves room for the cut's own words */
        const size_t put = validation_lines_put(line, &at, sizeof line - 96, text ? text : "", length);
        if (put < length || !whole) {
            snprintf(line + at, sizeof line - at, " [CUT: the first %zu of %s%zu bytes of the text are in this line]",
                     put, whole ? "" : "more than ", length);
            validation_lines_lost(v, VALIDATION_CUT, emit, user);
        }
        emit(user, line);
    }
    if (v->callbacks == v->next) {
        v->next = v->next > UINT64_MAX / 2 ? 0 : v->next * 2;
        validation_lines_counts(v, emit, user);
    }
}

/* The lock is a word of its own beside the state, never inside it: no copy of the state ever holds a lock. It
   covers the tables, the counters and the inherited counter of messages, which goes up under it. */
static inline void validation_lines_lock(int* lock) {
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) {
    }
}
static inline void validation_lines_unlock(int* lock) { __atomic_store_n(lock, 0, __ATOMIC_RELEASE); }
static inline void validation_lines_said(int* lock, ValidationLines* v, uint32_t* inherited, const char* name,
                                         int32_t number, const char* text, ValidationEmit emit, void* user) {
    validation_lines_lock(lock);
    if (inherited) ++*inherited;
    validation_lines_callback(v, name, number, text, emit, user);
    validation_lines_unlock(lock);
}

/* The status of a run under the validation option, each fact in a line of its own. layer: the layer is on.
   checks: its synchronisation checks were asked for and the instance is made with them, else why not in words,
   with the result of the last call when one was made (has_result). callback: 1 ready, 0 could not be made
   (with its result's name and number), -1 none was asked for. */
static inline void validation_lines_status(int layer, int checks, const char* why, int has_result,
                                           const char* result, int number, int callback, const char* callback_result,
                                           int callback_number, ValidationEmit emit, void* user) {
    char line[512];
    if (!layer) {
        emit(user, "the layer VK_LAYER_KHRONOS_validation is NOT enabled: it is not installed. No message of the "
                   "layer will be seen in this run");
        return;
    }
    emit(user, "the layer VK_LAYER_KHRONOS_validation is enabled");
    if (checks) emit(user, "its synchronisation checks were asked for and the instance is made with them");
    else {
        if (has_result)
            snprintf(line, sizeof line, "its synchronisation checks were NOT asked for (VK_EXT_validation_features): "
                                        "%s (the last call's result %s, %d)", why, result, number);
        else
            snprintf(line, sizeof line, "its synchronisation checks were NOT asked for (VK_EXT_validation_features): "
                                        "%s", why);
        emit(user, line);
    }
    if (callback > 0) emit(user, "the message callback is ready");
    else if (!callback) {
        snprintf(line, sizeof line, "the message callback could NOT be made (%s, %d): no message of the layer will "
                                    "be seen in this run", callback_result, callback_number);
        emit(user, line);
    } else
        emit(user, "no message callback was asked for: no message of the layer will be seen");
}
#endif
