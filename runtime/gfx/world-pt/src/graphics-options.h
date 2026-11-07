#pragma once
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline bool arceus_ray_tracing_requested(void) {
    const char *value = getenv("ARCEUS_RAY_TRACING");
    return value && !strcmp(value, "1");
}

static inline bool arceus_dlss_requested(void) {
    const char *value = getenv("ARCEUS_NGX");
    if (value && *value) return atoi(value) != 0;
    value = getenv("ARCEUS_UPSCALER");
    return value && !strcmp(value, "DLSS");
}

static inline bool arceus_upscaling_requested(void) {
    const char *value = getenv("ARCEUS_UPSCALER");
    return arceus_ray_tracing_requested() || arceus_dlss_requested() ||
           (value && !strcmp(value, "FSR"));
}
