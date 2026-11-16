/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors
   Copyright 2026 the NXAevora contributors */
/* Synthetic plain RomFS images for the tests that read a game's romfs.bin: a blob, its header, tables and files.
   Nothing from a console and no encryption. The includer defines CHECK(value). */
#ifndef SNX_TEST_ROMFS_SYNTH_H
#define SNX_TEST_ROMFS_SYNTH_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ROMFS_HEADER_SIZE 0x00000050
#define ROMFS_ENTRY_EMPTY 0xFFFFFFFF

typedef struct {
    unsigned char* data;
    size_t size;
} blob_t;

static inline void* zalloc(size_t size) {
    void* p = calloc(1, size ? size : 1);
    CHECK(p != NULL);
    return p;
}

static inline size_t align_up(size_t value, size_t alignment) { return (value + alignment - 1) / alignment * alignment; }

static inline void put32(unsigned char* p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i));
}

static inline void put64(unsigned char* p, uint64_t v) {
    for (unsigned i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * i));
}

/* A file of a RomFS. */
typedef struct {
    const char* name;
    blob_t content;
} romfs_file_t;

/* A RomFS of a root directory and its files. */
static inline blob_t romfs_build(const romfs_file_t* files, unsigned count) {
    size_t meta = 0;
    for (unsigned i = 0; i < count; i++) meta += 0x20 + align_up(strlen(files[i].name), 4);
    size_t data_offset = align_up(0x70 + meta, 0x10);
    size_t size = data_offset;
    for (unsigned i = 0; i < count; i++) size = align_up(size, 0x10) + files[i].content.size;
    blob_t romfs = {zalloc(size), size};
    unsigned char* h = romfs.data;
    put64(h, ROMFS_HEADER_SIZE);
    put64(h + 0x08, 0x50); /* directory hashes */
    put64(h + 0x10, 4);
    put64(h + 0x18, 0x54); /* directories */
    put64(h + 0x20, 0x18);
    put64(h + 0x28, 0x6C); /* file hashes */
    put64(h + 0x30, 4);
    put64(h + 0x38, 0x70); /* files */
    put64(h + 0x40, meta);
    put64(h + 0x48, data_offset);
    put32(h + 0x50, 0);
    put32(h + 0x54 + 0x4, ROMFS_ENTRY_EMPTY); /* root: no sibling, no child, files from 0 */
    put32(h + 0x54 + 0x8, ROMFS_ENTRY_EMPTY);
    put32(h + 0x54 + 0xC, count ? 0 : ROMFS_ENTRY_EMPTY);
    put32(h + 0x54 + 0x10, ROMFS_ENTRY_EMPTY);
    put32(h + 0x6C, 0);
    size_t entry = 0, data = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned char* f = h + 0x70 + entry;
        size_t name = strlen(files[i].name);
        size_t next = entry + 0x20 + align_up(name, 4);
        data = align_up(data, 0x10);
        put32(f + 0x4, i + 1 < count ? (uint32_t)next : ROMFS_ENTRY_EMPTY);
        put64(f + 0x8, data);
        put64(f + 0x10, files[i].content.size);
        put32(f + 0x18, ROMFS_ENTRY_EMPTY);
        put32(f + 0x1C, (uint32_t)name);
        memcpy(f + 0x20, files[i].name, name);
        memcpy(h + data_offset + data, files[i].content.data, files[i].content.size);
        data += files[i].content.size;
        entry = next;
    }
    return romfs;
}

#endif
