#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct ShElfImage ShElfImage;

ShElfImage *sh_elf_load(const char *path);
void *sh_elf_symbol(ShElfImage *image, const char *name);
const char *sh_elf_error(void);

void *sh_elf_dlopen(const char *path, int flags);
void *sh_elf_dlsym(void *handle, const char *name);
int sh_elf_dlclose(void *handle);
char *sh_elf_dlerror(void);
int sh_elf_dladdr(const void *address, void *info);

void *sh_elf_tls_get_addr(const uintptr_t index[2]);
void *sh_elf_tlsdesc_address(const void *argument);

void *sh_glibc_resolve(const char *name, const char *version, int weak);
