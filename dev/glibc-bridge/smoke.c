#include "elf_loader.h"

#include <stdint.h>
#include <stdio.h>

typedef int32_t (*ShEnumerateInstanceVersion)(uint32_t *version);

int main(int argument_count, char **arguments) {
    const char *path = argument_count > 1
        ? arguments[1]
        : "../arch/root/usr/lib/libvulkan.so.1";
    ShElfImage *image = sh_elf_load(path);
    if (!image) {
        fprintf(stderr, "load failed: %s\n", sh_elf_error());
        return 1;
    }

    ShEnumerateInstanceVersion enumerate =
        (ShEnumerateInstanceVersion)sh_elf_symbol(image, "vkEnumerateInstanceVersion");
    if (!enumerate) {
        fprintf(stderr, "symbol lookup failed: %s\n", sh_elf_error());
        return 1;
    }

    uint32_t version = 0;
    int32_t result = enumerate(&version);
    printf("vkEnumerateInstanceVersion: result=%d version=%u.%u.%u\n",
           result, version >> 22, (version >> 12) & 0x3ff, version & 0xfff);
    return result != 0;
}
