/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Models of the C library imports game code calls (imports.h). Each follows the C standard on guest memory, with
   the AAPCS64 arguments in X0-X7 and D0-D7 and the result in X0 or D0, then returns through X30. */
#include "imports.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define R_AARCH64_JUMP_SLOT 1026
#define STRING_MAX 4096 /* longer guest strings fail the run: the fixtures make short ones */

static uint64_t rd64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
static uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint64_t adrp_page(uint32_t w, uint64_t pc) {
    int64_t imm = (int64_t)(((w >> 29) & 3) | (((w >> 5) & 0x7FFFF) << 2));
    if (imm & (1 << 20)) imm -= 1 << 21;
    return (pc & ~0xFFFull) + (uint64_t)(imm << 12);
}

int import_name_at(const uint8_t* image, size_t size, uint64_t offset, char* name, size_t name_size) {
    if (offset + 16 > size) return 0;
    const uint32_t w0 = rd32(image + offset), w1 = rd32(image + offset + 4), w3 = rd32(image + offset + 12);
    if ((w0 & 0x9F00001F) != 0x90000010 || (w1 & 0xFFC003FF) != 0xF9400211 || w3 != 0xD61F0220) return 0;
    const uint64_t slot = adrp_page(w0, offset) + ((w1 >> 10) & 0xFFF) * 8;
    const uint32_t mod0 = rd32(image + 4);
    const uint64_t dynamic = mod0 + (uint64_t)(int64_t)(int32_t)rd32(image + mod0 + 4);
    uint64_t symtab = 0, strtab = 0, jmprel = 0, pltrelsz = 0;
    for (uint64_t d = dynamic; d + 16 <= size && rd64(image + d); d += 16) {
        const uint64_t tag = rd64(image + d), value = rd64(image + d + 8);
        if (tag == 6) symtab = value;
        if (tag == 5) strtab = value;
        if (tag == 23) jmprel = value;
        if (tag == 2) pltrelsz = value;
    }
    for (uint64_t p = jmprel; p + 24 <= jmprel + pltrelsz && p + 24 <= size; p += 24) {
        const uint64_t info = rd64(image + p + 8);
        if (rd64(image + p) != slot || (info & 0xFFFFFFFF) != R_AARCH64_JUMP_SLOT) continue;
        const uint64_t sym = symtab + 24 * (info >> 32);
        if (sym + 4 > size) return 0;
        const uint64_t s = strtab + rd32(image + sym);
        size_t n = 0;
        while (s + n < size && image[s + n] && n + 1 < name_size) {
            name[n] = (char)image[s + n];
            n++;
        }
        name[n] = 0;
        return 1;
    }
    return 0;
}

static int mapped(const ImportEnv* env, GAddr a, uint64_t n) { return a >= env->lo && a <= env->hi && n <= env->hi - a; }

/* The guest string at a, NUL included, copied to buf: its length, or -1 when it is unmapped or too long */
static long guest_string(const ImportEnv* env, GAddr a, char* buf) {
    for (long n = 0; n < STRING_MAX; n++) {
        if (!mapped(env, a + (GAddr)n, 1)) return -1;
        buf[n] = (char)env->g.mem[a + (GAddr)n];
        if (!buf[n]) return n;
    }
    return -1;
}

static const char* finish(CPU* c) {
    c->pc = c->x[30];
    return NULL;
}

static const char* m_strlen(CPU* c, const ImportEnv* env) {
    static char s[STRING_MAX];
    const long n = guest_string(env, c->x[0], s);
    if (n < 0) return "strlen of an unmapped or unterminated string";
    c->x[0] = (uint64_t)n;
    return finish(c);
}

static const char* m_strchr(CPU* c, const ImportEnv* env) {
    static char s[STRING_MAX];
    const long n = guest_string(env, c->x[0], s);
    if (n < 0) return "strchr of an unmapped or unterminated string";
    const char* hit = memchr(s, (char)c->x[1], (size_t)n + 1);
    c->x[0] = hit ? c->x[0] + (uint64_t)(hit - s) : 0;
    return finish(c);
}

static const char* m_strpbrk(CPU* c, const ImportEnv* env) {
    static char s[STRING_MAX], accept[STRING_MAX];
    if (guest_string(env, c->x[0], s) < 0 || guest_string(env, c->x[1], accept) < 0)
        return "strpbrk of an unmapped or unterminated string";
    const char* hit = strpbrk(s, accept);
    c->x[0] = hit ? c->x[0] + (uint64_t)(hit - s) : 0;
    return finish(c);
}

static const char* m_strcpy(CPU* c, const ImportEnv* env) {
    static char s[STRING_MAX];
    const long n = guest_string(env, c->x[1], s);
    if (n < 0 || !mapped(env, c->x[0], (uint64_t)n + 1)) return "strcpy with unmapped memory";
    memmove(env->g.mem + c->x[0], s, (size_t)n + 1);
    return finish(c);
}

static const char* m_strtod(CPU* c, const ImportEnv* env) {
    static char s[STRING_MAX];
    if (guest_string(env, c->x[0], s) < 0) return "strtod of an unmapped or unterminated string";
    char* end;
    const double v = strtod(s, &end);
    if (c->x[1]) {
        if (!mapped(env, c->x[1], 8)) return "strtod with an unmapped endptr";
        g_st64(&env->g, c->x[1], c->x[0] + (uint64_t)(end - s));
    }
    memcpy(&c->v[0].d[0], &v, 8);
    return finish(c);
}

static const char* m_localeconv(CPU* c, const ImportEnv* env) {
    c->x[0] = env->lconv;
    return finish(c);
}

static const char* m_memset(CPU* c, const ImportEnv* env) {
    if (!mapped(env, c->x[0], c->x[2])) return "memset of unmapped memory";
    memset(env->g.mem + c->x[0], (int)(uint8_t)c->x[1], (size_t)c->x[2]);
    return finish(c);
}

static const char* m_memcpy(CPU* c, const ImportEnv* env) {
    if (!mapped(env, c->x[0], c->x[2]) || !mapped(env, c->x[1], c->x[2])) return "memcpy of unmapped memory";
    memmove(env->g.mem + c->x[0], env->g.mem + c->x[1], (size_t)c->x[2]);
    return finish(c);
}

static const char* m_memcmp(CPU* c, const ImportEnv* env) {
    if (!mapped(env, c->x[0], c->x[2]) || !mapped(env, c->x[1], c->x[2])) return "memcmp of unmapped memory";
    const int r = memcmp(env->g.mem + c->x[0], env->g.mem + c->x[1], (size_t)c->x[2]);
    c->x[0] = (uint32_t)(r < 0 ? -1 : r > 0);
    return finish(c);
}

static const char* m_fmod(CPU* c, const ImportEnv* env) {
    (void)env;
    double a, b;
    memcpy(&a, &c->v[0].d[0], 8);
    memcpy(&b, &c->v[1].d[0], 8);
    const double r = fmod(a, b);
    memcpy(&c->v[0].d[0], &r, 8);
    return finish(c);
}

static const char* m_sched_yield(CPU* c, const ImportEnv* env) {
    (void)env;
    c->x[0] = 0;
    return finish(c);
}

static const char* m_abort(CPU* c, const ImportEnv* env) {
    (void)c;
    (void)env;
    return "abort was called";
}

static const struct {
    const char* name;
    ImportModel model;
} models[] = {
    {"strlen", m_strlen},   {"strchr", m_strchr}, {"strpbrk", m_strpbrk}, {"strcpy", m_strcpy},
    {"strtod", m_strtod},   {"localeconv", m_localeconv}, {"memset", m_memset}, {"memcpy", m_memcpy},
    {"memmove", m_memcpy},  {"memcmp", m_memcmp}, {"fmod", m_fmod}, {"sched_yield", m_sched_yield},
    {"abort", m_abort},
};

ImportModel import_model(const char* name) {
    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++)
        if (!strcmp(models[i].name, name)) return models[i].model;
    return NULL;
}

/* struct lconv starts with char *decimal_point, then thousands_sep and grouping: ".", "" and "" */
void import_env_init(ImportEnv* env, GAddr area) {
    env->lconv = area;
    memset(env->g.mem + area, 0, 64);
    g_st64(&env->g, area, area + 48);
    g_st64(&env->g, area + 8, area + 50);
    g_st64(&env->g, area + 16, area + 50);
    env->g.mem[area + 48] = '.';
}
