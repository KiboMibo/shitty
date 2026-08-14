#define _GNU_SOURCE

#include "elf_loader.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DT_RELR
#define DT_RELR 36
#define DT_RELRSZ 35
#define DT_RELRENT 37
#endif

#ifndef SHT_RELR
#define SHT_RELR 19
#endif

#ifndef R_X86_64_IRELATIVE
#define R_X86_64_IRELATIVE 37
#endif

#ifndef R_X86_64_TLSDESC
#define R_X86_64_TLSDESC 36
#endif

#ifndef STT_GNU_IFUNC
#define STT_GNU_IFUNC 10
#endif

enum {
    SH_MAX_IMAGES = 128,
    SH_MAX_DEPENDENCIES = 64,
    SH_MAX_TLS_MODULES = 128,
    SH_MAX_VERSION_INDEX = 256,
    SH_MAX_DEFERRED_RELOCS = 512,
};

struct ShElfImage {
    char *path;
    const char *soname;
    uintptr_t base;
    uintptr_t map_start;
    size_t map_size;
    Elf64_Phdr *phdrs;
    size_t phnum;

    Elf64_Dyn *dynamic;
    const char *strtab;
    size_t strsz;
    Elf64_Sym *symtab;
    uint32_t *gnu_hash;
    Elf64_Half *versym;
    const char *version_names[SH_MAX_VERSION_INDEX];
    struct ShElfImage *dependencies[SH_MAX_DEPENDENCIES];
    size_t dependency_count;

    Elf64_Rela *rela;
    size_t rela_count;
    Elf64_Rela *jmprel;
    size_t jmprel_count;
    Elf64_Addr *relr;
    size_t relr_count;

    uintptr_t init;
    uintptr_t init_array;
    size_t init_array_count;
    uintptr_t relro_start;
    size_t relro_size;

    size_t tls_module;
    uintptr_t tls_template;
    size_t tls_file_size;
    size_t tls_memory_size;
    size_t tls_alignment;

    int relocating;
    int relocated;
    int initialized;
};

typedef struct {
    ShElfImage *image;
    const Elf64_Rela *relocation;
} ShDeferredRelocation;

static ShElfImage *sh_images[SH_MAX_IMAGES];
static size_t sh_image_count;
static ShElfImage *sh_tls_modules[SH_MAX_TLS_MODULES];
static size_t sh_tls_module_count;
static _Thread_local void *sh_thread_tls[SH_MAX_TLS_MODULES];
static char sh_library_directory[PATH_MAX];
static _Thread_local char sh_error_buffer[512];
static _Thread_local int sh_have_dlerror;

typedef struct {
    uintptr_t address;
    ShElfImage *image;
    Elf64_Sym *symbol;
} ShSymbolDefinition;

typedef struct {
    uintptr_t module;
    uintptr_t offset;
} ShTlsDescArgument;

extern uintptr_t sh_elf_tlsdesc_entry(void);

static ShSymbolDefinition sh_find_global_symbol(const char *name, const char *version);

static uintptr_t sh_align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1);
}

static uintptr_t sh_align_up(uintptr_t value, uintptr_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void sh_set_error(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(sh_error_buffer, sizeof(sh_error_buffer), format, arguments);
    va_end(arguments);
    sh_have_dlerror = 1;
}

const char *sh_elf_error(void) {
    return sh_error_buffer[0] ? sh_error_buffer : "no error";
}

char *sh_elf_dlerror(void) {
    if (!sh_have_dlerror) {
        return NULL;
    }
    sh_have_dlerror = 0;
    return sh_error_buffer;
}

static int sh_pread_all(int fd, void *buffer, size_t size, off_t offset) {
    unsigned char *cursor = buffer;
    while (size != 0) {
        ssize_t result = pread(fd, cursor, size, offset);
        if (result <= 0) {
            if (result < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += (size_t)result;
        size -= (size_t)result;
        offset += result;
    }
    return 0;
}

static int sh_segment_protection(uint32_t flags) {
    int protection = 0;
    if (flags & PF_R) protection |= PROT_READ;
    if (flags & PF_W) protection |= PROT_WRITE;
    if (flags & PF_X) protection |= PROT_EXEC;
    return protection;
}

static void sh_parse_versions(ShElfImage *image, uintptr_t verneed_address,
                              size_t verneed_count, uintptr_t verdef_address,
                              size_t verdef_count) {
    if (verneed_address) {
        Elf64_Verneed *need = (Elf64_Verneed *)(image->base + verneed_address);
        for (size_t n = 0; n < verneed_count; ++n) {
            Elf64_Vernaux *aux = (Elf64_Vernaux *)((char *)need + need->vn_aux);
            for (size_t a = 0; a < need->vn_cnt; ++a) {
                unsigned index = aux->vna_other & 0x7fff;
                if (index < SH_MAX_VERSION_INDEX && aux->vna_name < image->strsz) {
                    image->version_names[index] = image->strtab + aux->vna_name;
                }
                if (!aux->vna_next) break;
                aux = (Elf64_Vernaux *)((char *)aux + aux->vna_next);
            }
            if (!need->vn_next) break;
            need = (Elf64_Verneed *)((char *)need + need->vn_next);
        }
    }

    if (verdef_address) {
        Elf64_Verdef *definition = (Elf64_Verdef *)(image->base + verdef_address);
        for (size_t index = 0; index < verdef_count; ++index) {
            Elf64_Verdaux *aux = (Elf64_Verdaux *)((char *)definition + definition->vd_aux);
            unsigned version_index = definition->vd_ndx & 0x7fff;
            if (version_index < SH_MAX_VERSION_INDEX && aux->vda_name < image->strsz) {
                image->version_names[version_index] = image->strtab + aux->vda_name;
            }
            if (!definition->vd_next) break;
            definition = (Elf64_Verdef *)((char *)definition + definition->vd_next);
        }
    }
}

static int sh_parse_dynamic(ShElfImage *image) {
    uintptr_t verneed = 0;
    size_t verneed_count = 0;
    uintptr_t verdef = 0;
    size_t verdef_count = 0;
    size_t rela_size = 0;
    size_t rela_entry_size = sizeof(Elf64_Rela);
    size_t jmprel_size = 0;
    size_t relr_size = 0;
    size_t relr_entry_size = sizeof(Elf64_Addr);

    for (Elf64_Dyn *entry = image->dynamic; entry->d_tag != DT_NULL; ++entry) {
        switch (entry->d_tag) {
        case DT_STRTAB: image->strtab = (char *)(image->base + entry->d_un.d_ptr); break;
        case DT_STRSZ: image->strsz = entry->d_un.d_val; break;
        case DT_SYMTAB: image->symtab = (Elf64_Sym *)(image->base + entry->d_un.d_ptr); break;
        case DT_GNU_HASH: image->gnu_hash = (uint32_t *)(image->base + entry->d_un.d_ptr); break;
        case DT_VERSYM: image->versym = (Elf64_Half *)(image->base + entry->d_un.d_ptr); break;
        case DT_VERNEED: verneed = entry->d_un.d_ptr; break;
        case DT_VERNEEDNUM: verneed_count = entry->d_un.d_val; break;
        case DT_VERDEF: verdef = entry->d_un.d_ptr; break;
        case DT_VERDEFNUM: verdef_count = entry->d_un.d_val; break;
        case DT_RELA: image->rela = (Elf64_Rela *)(image->base + entry->d_un.d_ptr); break;
        case DT_RELASZ: rela_size = entry->d_un.d_val; break;
        case DT_RELAENT: rela_entry_size = entry->d_un.d_val; break;
        case DT_JMPREL: image->jmprel = (Elf64_Rela *)(image->base + entry->d_un.d_ptr); break;
        case DT_PLTRELSZ: jmprel_size = entry->d_un.d_val; break;
        case DT_RELR: image->relr = (Elf64_Addr *)(image->base + entry->d_un.d_ptr); break;
        case DT_RELRSZ: relr_size = entry->d_un.d_val; break;
        case DT_RELRENT: relr_entry_size = entry->d_un.d_val; break;
        case DT_INIT: image->init = image->base + entry->d_un.d_ptr; break;
        case DT_INIT_ARRAY: image->init_array = image->base + entry->d_un.d_ptr; break;
        case DT_INIT_ARRAYSZ: image->init_array_count = entry->d_un.d_val / sizeof(uintptr_t); break;
        case DT_SONAME: image->soname = image->strtab ? image->strtab + entry->d_un.d_val : NULL; break;
        default: break;
        }
    }

    if (!image->strtab || !image->symtab || !image->gnu_hash) {
        sh_set_error("%s: missing dynamic string, symbol, or GNU hash table", image->path);
        return -1;
    }
    if (rela_size && rela_entry_size != sizeof(Elf64_Rela)) {
        sh_set_error("%s: unsupported RELA entry size %zu", image->path, rela_entry_size);
        return -1;
    }
    if (relr_size && relr_entry_size != sizeof(Elf64_Addr)) {
        sh_set_error("%s: unsupported RELR entry size %zu", image->path, relr_entry_size);
        return -1;
    }
    image->rela_count = rela_size / sizeof(Elf64_Rela);
    image->jmprel_count = jmprel_size / sizeof(Elf64_Rela);
    image->relr_count = relr_size / sizeof(Elf64_Addr);
    /* DT_SONAME may precede DT_STRTAB. Resolve it in a second pass. */
    for (Elf64_Dyn *entry = image->dynamic; entry->d_tag != DT_NULL; ++entry) {
        if (entry->d_tag == DT_SONAME) image->soname = image->strtab + entry->d_un.d_val;
    }
    sh_parse_versions(image, verneed, verneed_count, verdef, verdef_count);
    return 0;
}

static const char *sh_symbol_version(const ShElfImage *image, size_t symbol_index) {
    if (!image->versym) return NULL;
    unsigned index = image->versym[symbol_index] & 0x7fff;
    if (index < 2 || index >= SH_MAX_VERSION_INDEX) return NULL;
    return image->version_names[index];
}

static uintptr_t sh_resolve_relocation_symbol(ShElfImage *image, size_t symbol_index,
                                               int *is_ifunc,
                                               ShElfImage **definition_image,
                                               Elf64_Sym **definition_symbol) {
    Elf64_Sym *symbol = &image->symtab[symbol_index];
    const char *name = image->strtab + symbol->st_name;
    *is_ifunc = 0;
    *definition_image = image;
    *definition_symbol = symbol;

    if (symbol->st_shndx != SHN_UNDEF) {
        uintptr_t address = image->base + symbol->st_value;
        if (ELF64_ST_TYPE(symbol->st_info) == STT_GNU_IFUNC) {
            *is_ifunc = 1;
        }
        return address;
    }

    const char *version = sh_symbol_version(image, symbol_index);
    int weak = ELF64_ST_BIND(symbol->st_info) == STB_WEAK;
    ShSymbolDefinition definition = sh_find_global_symbol(name, version);
    void *address = (void *)definition.address;
    if (address) {
        *definition_image = definition.image;
        *definition_symbol = definition.symbol;
        *is_ifunc = ELF64_ST_TYPE(definition.symbol->st_info) == STT_GNU_IFUNC;
    } else {
        address = sh_glibc_resolve(name, version, weak);
    }
    if (!address && !weak) {
        sh_set_error("%s: unresolved symbol %s%s%s", image->path, name,
                     version ? "@" : "", version ? version : "");
    }
    return (uintptr_t)address;
}

static int sh_apply_relr(ShElfImage *image) {
    uintptr_t *where = NULL;
    for (size_t index = 0; index < image->relr_count; ++index) {
        uintptr_t entry = image->relr[index];
        if ((entry & 1) == 0) {
            where = (uintptr_t *)(image->base + entry);
            *where += image->base;
            ++where;
            continue;
        }
        if (!where) {
            sh_set_error("%s: RELR bitmap appears before an address", image->path);
            return -1;
        }
        for (unsigned bit = 1; bit < 8 * sizeof(entry); ++bit) {
            if (entry & ((uintptr_t)1 << bit)) {
                where[bit - 1] += image->base;
            }
        }
        where += 8 * sizeof(entry) - 1;
    }
    return 0;
}

static int sh_apply_one_rela(ShElfImage *image, const Elf64_Rela *relocation,
                             int allow_ifunc, int *defer) {
    unsigned type = ELF64_R_TYPE(relocation->r_info);
    size_t symbol_index = ELF64_R_SYM(relocation->r_info);
    uintptr_t *where = (uintptr_t *)(image->base + relocation->r_offset);
    uintptr_t symbol_address = 0;
    int symbol_is_ifunc = 0;
    ShElfImage *definition_image = NULL;
    Elf64_Sym *definition_symbol = NULL;
    *defer = 0;

    switch (type) {
    case R_X86_64_RELATIVE:
        *where = image->base + relocation->r_addend;
        return 0;
    case R_X86_64_IRELATIVE:
        if (!allow_ifunc) {
            *defer = 1;
            return 0;
        }
        *where = ((uintptr_t (*)(void))(image->base + relocation->r_addend))();
        return 0;
    case R_X86_64_64:
    case R_X86_64_GLOB_DAT:
    case R_X86_64_JUMP_SLOT:
        symbol_address = sh_resolve_relocation_symbol(image, symbol_index, &symbol_is_ifunc,
                                                       &definition_image, &definition_symbol);
        if (!symbol_address && ELF64_ST_BIND(image->symtab[symbol_index].st_info) != STB_WEAK) {
            return -1;
        }
        if (symbol_is_ifunc && !allow_ifunc) {
            *defer = 1;
            return 0;
        }
        if (symbol_is_ifunc) {
            symbol_address = ((uintptr_t (*)(void))symbol_address)();
        }
        *where = symbol_address + (type == R_X86_64_64 ? relocation->r_addend : 0);
        return 0;
    case R_X86_64_DTPMOD64:
        if (symbol_index == 0) {
            definition_image = image;
        } else {
            symbol_address = sh_resolve_relocation_symbol(image, symbol_index,
                                                           &symbol_is_ifunc,
                                                           &definition_image,
                                                           &definition_symbol);
            if (!symbol_address) return -1;
        }
        if (!definition_image->tls_module) {
            sh_set_error("%s: TLS module relocation refers to a non-TLS image", image->path);
            return -1;
        }
        *where = definition_image->tls_module;
        return 0;
    case R_X86_64_DTPOFF64:
        if (symbol_index == 0) {
            *where = relocation->r_addend;
            return 0;
        }
        symbol_address = sh_resolve_relocation_symbol(image, symbol_index,
                                                       &symbol_is_ifunc,
                                                       &definition_image,
                                                       &definition_symbol);
        if (!symbol_address || !definition_image->tls_module) return -1;
        *where = definition_symbol->st_value + relocation->r_addend;
        return 0;
    case R_X86_64_TLSDESC: {
        uintptr_t offset = relocation->r_addend;
        if (symbol_index == 0) {
            definition_image = image;
        } else {
            symbol_address = sh_resolve_relocation_symbol(image, symbol_index,
                                                           &symbol_is_ifunc,
                                                           &definition_image,
                                                           &definition_symbol);
            if (!symbol_address) return -1;
            offset += definition_symbol->st_value;
        }
        if (!definition_image->tls_module) {
            sh_set_error("%s: TLSDESC refers to a non-TLS image", image->path);
            return -1;
        }
        ShTlsDescArgument *argument = malloc(sizeof(*argument));
        if (!argument) {
            sh_set_error("%s: cannot allocate TLSDESC argument", image->path);
            return -1;
        }
        argument->module = definition_image->tls_module;
        argument->offset = offset;
        where[0] = (uintptr_t)sh_elf_tlsdesc_entry;
        where[1] = (uintptr_t)argument;
        return 0;
    }
    default:
        sh_set_error("%s: unsupported x86-64 relocation type %u at %#lx", image->path,
                     type, (unsigned long)relocation->r_offset);
        return -1;
    }
}

static int sh_apply_relocations(ShElfImage *image, ShDeferredRelocation *deferred,
                                size_t *deferred_count) {
    if (sh_apply_relr(image) != 0) return -1;

    const struct {
        Elf64_Rela *items;
        size_t count;
    } tables[] = {
        {image->rela, image->rela_count},
        {image->jmprel, image->jmprel_count},
    };

    for (size_t table = 0; table < sizeof(tables) / sizeof(tables[0]); ++table) {
        for (size_t index = 0; index < tables[table].count; ++index) {
            int defer = 0;
            if (sh_apply_one_rela(image, &tables[table].items[index], 0, &defer) != 0) {
                return -1;
            }
            if (defer) {
                if (*deferred_count == SH_MAX_DEFERRED_RELOCS) {
                    sh_set_error("%s: too many IFUNC relocations", image->path);
                    return -1;
                }
                deferred[*deferred_count] = (ShDeferredRelocation){image, &tables[table].items[index]};
                ++*deferred_count;
            }
        }
    }
    return 0;
}

void *sh_elf_tls_get_addr(const uintptr_t index[2]) {
    size_t module = index[0];
    size_t offset = index[1];
    if (module == 0 || module >= SH_MAX_TLS_MODULES || !sh_tls_modules[module]) {
        sh_set_error("invalid TLS module %zu", module);
        return NULL;
    }
    ShElfImage *image = sh_tls_modules[module];
    if (offset >= image->tls_memory_size) {
        sh_set_error("TLS offset %zu exceeds module %zu size %zu", offset, module,
                     image->tls_memory_size);
        return NULL;
    }
    if (!sh_thread_tls[module]) {
        size_t alignment = image->tls_alignment;
        if (alignment < sizeof(void *)) alignment = sizeof(void *);
        void *block = NULL;
        if (posix_memalign(&block, alignment, image->tls_memory_size) != 0) {
            sh_set_error("cannot allocate TLS module %zu", module);
            return NULL;
        }
        memset(block, 0, image->tls_memory_size);
        memcpy(block, (void *)image->tls_template, image->tls_file_size);
        sh_thread_tls[module] = block;
    }
    return (unsigned char *)sh_thread_tls[module] + offset;
}

void *sh_elf_tlsdesc_address(const void *opaque_argument) {
    const ShTlsDescArgument *argument = opaque_argument;
    uintptr_t index[2] = {argument->module, argument->offset};
    return sh_elf_tls_get_addr(index);
}

static int sh_protect_image(ShElfImage *image) {
    long page_size_long = sysconf(_SC_PAGESIZE);
    if (page_size_long <= 0) return -1;
    uintptr_t page_size = (uintptr_t)page_size_long;

    if (mprotect((void *)image->map_start, image->map_size, PROT_NONE) != 0) {
        sh_set_error("%s: mprotect(PROT_NONE): %s", image->path, strerror(errno));
        return -1;
    }
    for (size_t index = 0; index < image->phnum; ++index) {
        Elf64_Phdr *header = &image->phdrs[index];
        if (header->p_type != PT_LOAD) continue;
        uintptr_t start = sh_align_down(image->base + header->p_vaddr, page_size);
        uintptr_t end = sh_align_up(image->base + header->p_vaddr + header->p_memsz, page_size);
        if (mprotect((void *)start, end - start, sh_segment_protection(header->p_flags)) != 0) {
            sh_set_error("%s: mprotect(PT_LOAD): %s", image->path, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int sh_apply_relro(ShElfImage *image) {
    if (!image->relro_size) return 0;
    long page_size_long = sysconf(_SC_PAGESIZE);
    uintptr_t page_size = (uintptr_t)page_size_long;
    uintptr_t start = sh_align_down(image->base + image->relro_start, page_size);
    uintptr_t end = sh_align_up(image->base + image->relro_start + image->relro_size, page_size);
    if (mprotect((void *)start, end - start, PROT_READ) != 0) {
        sh_set_error("%s: mprotect(RELRO): %s", image->path, strerror(errno));
        return -1;
    }
    return 0;
}

static void sh_run_initializers(ShElfImage *image) {
    if (image->init) {
        ((void (*)(void))image->init)();
    }
    uintptr_t *array = (uintptr_t *)image->init_array;
    for (size_t index = 0; index < image->init_array_count; ++index) {
        if (array[index] && array[index] != (uintptr_t)-1) {
            ((void (*)(void))array[index])();
        }
    }
}

static int sh_virtual_dependency(const char *name) {
    return strcmp(name, "libc.so.6") == 0 ||
           strcmp(name, "libpthread.so.0") == 0 ||
           strcmp(name, "libdl.so.2") == 0 ||
           strcmp(name, "libm.so.6") == 0 ||
           strcmp(name, "librt.so.1") == 0 ||
           strcmp(name, "ld-linux-x86-64.so.2") == 0;
}

static int sh_load_dependencies(ShElfImage *image) {
    for (Elf64_Dyn *entry = image->dynamic; entry->d_tag != DT_NULL; ++entry) {
        if (entry->d_tag != DT_NEEDED) continue;
        const char *needed = image->strtab + entry->d_un.d_val;
        if (sh_virtual_dependency(needed)) continue;
        if (image->dependency_count == SH_MAX_DEPENDENCIES) {
            sh_set_error("%s: too many DT_NEEDED entries", image->path);
            return -1;
        }
        ShElfImage *dependency = sh_elf_load(needed);
        if (!dependency) return -1;
        image->dependencies[image->dependency_count++] = dependency;
    }
    return 0;
}

ShElfImage *sh_elf_load(const char *path) {
    sh_error_buffer[0] = 0;
    char candidate[PATH_MAX];
    char resolved[PATH_MAX];
    if (strchr(path, '/')) {
        if (!realpath(path, resolved)) {
            sh_set_error("realpath(%s): %s", path, strerror(errno));
            return NULL;
        }
    } else {
        if (!sh_library_directory[0]) {
            sh_set_error("cannot resolve bare library name before the root image: %s", path);
            return NULL;
        }
        int length = snprintf(candidate, sizeof(candidate), "%s/%s", sh_library_directory, path);
        if (length < 0 || (size_t)length >= sizeof(candidate) || !realpath(candidate, resolved)) {
            sh_set_error("cannot find %s in %s", path, sh_library_directory);
            return NULL;
        }
    }
    for (size_t index = 0; index < sh_image_count; ++index) {
        if (strcmp(sh_images[index]->path, resolved) == 0 ||
            (sh_images[index]->soname && strcmp(sh_images[index]->soname, path) == 0)) {
            return sh_images[index];
        }
    }
    if (!sh_library_directory[0]) {
        size_t length = strlen(resolved);
        if (length >= sizeof(sh_library_directory)) {
            sh_set_error("library directory path is too long");
            return NULL;
        }
        memcpy(sh_library_directory, resolved, length + 1);
        char *slash = strrchr(sh_library_directory, '/');
        if (!slash) {
            sh_set_error("root library has no directory: %s", resolved);
            return NULL;
        }
        *slash = 0;
    }

    const char *load_path = resolved;
    int fd = open(load_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        sh_set_error("open(%s): %s", load_path, strerror(errno));
        return NULL;
    }

    Elf64_Ehdr elf_header;
    if (sh_pread_all(fd, &elf_header, sizeof(elf_header), 0) != 0 ||
        memcmp(elf_header.e_ident, ELFMAG, SELFMAG) != 0 ||
        elf_header.e_ident[EI_CLASS] != ELFCLASS64 ||
        elf_header.e_ident[EI_DATA] != ELFDATA2LSB ||
        elf_header.e_machine != EM_X86_64 || elf_header.e_type != ET_DYN ||
        elf_header.e_phentsize != sizeof(Elf64_Phdr)) {
        sh_set_error("%s: not a supported x86-64 ET_DYN ELF", load_path);
        close(fd);
        return NULL;
    }

    ShElfImage *image = calloc(1, sizeof(*image));
    if (!image) {
        sh_set_error("calloc image: %s", strerror(errno));
        close(fd);
        return NULL;
    }
    image->path = strdup(load_path);
    image->phnum = elf_header.e_phnum;
    image->phdrs = calloc(image->phnum, sizeof(*image->phdrs));
    if (!image->path || !image->phdrs ||
        sh_pread_all(fd, image->phdrs, image->phnum * sizeof(*image->phdrs),
                     (off_t)elf_header.e_phoff) != 0) {
        sh_set_error("%s: cannot read program headers", load_path);
        close(fd);
        return NULL;
    }

    long page_size_long = sysconf(_SC_PAGESIZE);
    uintptr_t page_size = (uintptr_t)page_size_long;
    uintptr_t min_address = UINTPTR_MAX;
    uintptr_t max_address = 0;
    for (size_t index = 0; index < image->phnum; ++index) {
        Elf64_Phdr *header = &image->phdrs[index];
        if (header->p_type != PT_LOAD) continue;
        uintptr_t start = sh_align_down(header->p_vaddr, page_size);
        uintptr_t end = sh_align_up(header->p_vaddr + header->p_memsz, page_size);
        if (start < min_address) min_address = start;
        if (end > max_address) max_address = end;
    }
    if (min_address == UINTPTR_MAX || max_address <= min_address) {
        sh_set_error("%s: no loadable segments", load_path);
        close(fd);
        return NULL;
    }

    image->map_size = max_address - min_address;
    void *mapping = mmap(NULL, image->map_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        sh_set_error("%s: mmap: %s", load_path, strerror(errno));
        close(fd);
        return NULL;
    }
    image->map_start = (uintptr_t)mapping;
    image->base = image->map_start - min_address;

    for (size_t index = 0; index < image->phnum; ++index) {
        Elf64_Phdr *header = &image->phdrs[index];
        if (header->p_type == PT_LOAD) {
            if (header->p_filesz > header->p_memsz ||
                sh_pread_all(fd, (void *)(image->base + header->p_vaddr),
                             header->p_filesz, (off_t)header->p_offset) != 0) {
                sh_set_error("%s: cannot load PT_LOAD segment", load_path);
                close(fd);
                return NULL;
            }
        } else if (header->p_type == PT_DYNAMIC) {
            image->dynamic = (Elf64_Dyn *)(image->base + header->p_vaddr);
        } else if (header->p_type == PT_GNU_RELRO) {
            image->relro_start = header->p_vaddr;
            image->relro_size = header->p_memsz;
        } else if (header->p_type == PT_TLS) {
            if (sh_tls_module_count + 1 >= SH_MAX_TLS_MODULES) {
                sh_set_error("%s: too many TLS modules", load_path);
                close(fd);
                return NULL;
            }
            image->tls_module = ++sh_tls_module_count;
            image->tls_template = image->base + header->p_vaddr;
            image->tls_file_size = header->p_filesz;
            image->tls_memory_size = header->p_memsz;
            image->tls_alignment = header->p_align;
            sh_tls_modules[image->tls_module] = image;
        }
    }
    close(fd);

    if (!image->dynamic || sh_parse_dynamic(image) != 0) {
        return NULL;
    }

    if (sh_image_count == SH_MAX_IMAGES) {
        sh_set_error("too many loaded images");
        return NULL;
    }
    sh_images[sh_image_count++] = image;
    if (sh_load_dependencies(image) != 0) return NULL;

    ShDeferredRelocation deferred[SH_MAX_DEFERRED_RELOCS];
    size_t deferred_count = 0;
    if (sh_apply_relocations(image, deferred, &deferred_count) != 0 ||
        sh_protect_image(image) != 0) {
        return NULL;
    }
    for (size_t index = 0; index < deferred_count; ++index) {
        int ignored = 0;
        if (sh_apply_one_rela(deferred[index].image, deferred[index].relocation, 1,
                              &ignored) != 0) {
            return NULL;
        }
    }
    if (sh_apply_relro(image) != 0) return NULL;

    image->relocated = 1;
    sh_run_initializers(image);
    image->initialized = 1;
    return image;
}

static uint32_t sh_gnu_hash_name(const char *name) {
    uint32_t hash = 5381;
    for (unsigned char byte; (byte = (unsigned char)*name++) != 0;) {
        hash = hash * 33 + byte;
    }
    return hash;
}

static ShSymbolDefinition sh_find_image_symbol(ShElfImage *image, const char *name,
                                                const char *version) {
    ShSymbolDefinition not_found = {0};
    if (!image || !image->gnu_hash) return not_found;
    uint32_t bucket_count = image->gnu_hash[0];
    uint32_t symbol_offset = image->gnu_hash[1];
    uint32_t bloom_size = image->gnu_hash[2];
    uint32_t bloom_shift = image->gnu_hash[3];
    Elf64_Xword *bloom = (Elf64_Xword *)(image->gnu_hash + 4);
    uint32_t *buckets = (uint32_t *)(bloom + bloom_size);
    uint32_t *chains = buckets + bucket_count;
    uint32_t hash = sh_gnu_hash_name(name);
    const unsigned word_bits = 8 * sizeof(Elf64_Xword);
    Elf64_Xword word = bloom[(hash / word_bits) % bloom_size];
    Elf64_Xword mask = ((Elf64_Xword)1 << (hash % word_bits)) |
                       ((Elf64_Xword)1 << ((hash >> bloom_shift) % word_bits));
    if ((word & mask) != mask) return not_found;

    uint32_t index = buckets[hash % bucket_count];
    if (index < symbol_offset) return not_found;
    for (;;) {
        uint32_t chain = chains[index - symbol_offset];
        if ((chain | 1) == (hash | 1)) {
            Elf64_Sym *symbol = &image->symtab[index];
            const char *symbol_version = sh_symbol_version(image, index);
            int version_hidden = image->versym && (image->versym[index] & 0x8000);
            int version_matches = version
                ? symbol_version && strcmp(symbol_version, version) == 0
                : !version_hidden;
            if (strcmp(image->strtab + symbol->st_name, name) == 0 &&
                symbol->st_shndx != SHN_UNDEF && version_matches &&
                ELF64_ST_VISIBILITY(symbol->st_other) != STV_HIDDEN &&
                ELF64_ST_VISIBILITY(symbol->st_other) != STV_INTERNAL) {
                return (ShSymbolDefinition){image->base + symbol->st_value, image, symbol};
            }
        }
        if (chain & 1) break;
        ++index;
    }
    return not_found;
}

static ShSymbolDefinition sh_find_global_symbol(const char *name, const char *version) {
    for (size_t index = 0; index < sh_image_count; ++index) {
        ShSymbolDefinition result = sh_find_image_symbol(sh_images[index], name, version);
        if (result.address) return result;
    }
    return (ShSymbolDefinition){0};
}

void *sh_elf_symbol(ShElfImage *image, const char *name) {
    ShSymbolDefinition definition = sh_find_image_symbol(image, name, NULL);
    if (!definition.address) return NULL;
    if (ELF64_ST_TYPE(definition.symbol->st_info) == STT_GNU_IFUNC) {
        definition.address = ((uintptr_t (*)(void))definition.address)();
    }
    return (void *)definition.address;
}

void *sh_elf_dlopen(const char *path, int flags) {
    (void)flags;
    if (!path) return (void *)(uintptr_t)-1;
    for (size_t index = 0; index < sh_image_count; ++index) {
        if (strcmp(sh_images[index]->path, path) == 0) return sh_images[index];
    }
    return sh_elf_load(path);
}

void *sh_elf_dlsym(void *handle, const char *name) {
    if (!name) {
        sh_set_error("dlsym: null name");
        return NULL;
    }
    if (handle && handle != (void *)(uintptr_t)-1) {
        void *result = sh_elf_symbol(handle, name);
        if (!result) sh_set_error("dlsym: symbol not found: %s", name);
        return result;
    }
    for (size_t index = sh_image_count; index != 0; --index) {
        void *result = sh_elf_symbol(sh_images[index - 1], name);
        if (result) return result;
    }
    sh_set_error("dlsym: symbol not found: %s", name);
    return NULL;
}

int sh_elf_dlclose(void *handle) {
    (void)handle;
    return 0;
}

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} ShDlInfo;

int sh_elf_dladdr(const void *address, void *opaque_info) {
    uintptr_t needle = (uintptr_t)address;
    ShDlInfo *info = opaque_info;
    for (size_t index = 0; index < sh_image_count; ++index) {
        ShElfImage *image = sh_images[index];
        if (needle >= image->map_start && needle < image->map_start + image->map_size) {
            info->dli_fname = image->path;
            info->dli_fbase = (void *)image->base;
            info->dli_sname = NULL;
            info->dli_saddr = NULL;
            return 1;
        }
    }
    return 0;
}
