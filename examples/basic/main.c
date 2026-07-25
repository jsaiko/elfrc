#include <stdio.h>

#include "elfr/elfr.h"

int main(void) {
    size_t size;
    const void *logo = erf_resource_data("logo", &size);
    if (!logo) {
        fprintf(stderr, "logo not found: %s\n", erf_strerror(erf_last_error()));
        return 1;
    }
    printf("logo: %zu bytes\n", size);

    const ERFResource *config = erf_find("config");
    printf("config (%zu bytes): %.*s\n", config->size, (int)config->size, (const char *)config->data);

    const ERFResource *by_id = erf_find_id(100);
    printf("resource id 100 is named '%s'\n", by_id->name);

    const ERFResource *greeting = erf_find("strings");
    printf("strings (language id %u): %.*s\n", greeting->language, (int)greeting->size,
           (const char *)greeting->data);

    const ERFResource *missing = erf_find("does-not-exist");
    printf("lookup of missing resource returns %s\n", missing ? "non-NULL (bug!)" : "NULL (as expected)");

    return 0;
}
