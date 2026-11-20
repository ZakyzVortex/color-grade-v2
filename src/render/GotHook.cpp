#include "render/GotHook.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__aarch64__)
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace color_grade::got {

#if defined(__aarch64__)

namespace {

struct Ctx {
    const char *suffix;
    const char *symbol;
    void *expected;
    void *value;
    void **old;
    int patched = 0;
};

bool endsWith(const char *s, const char *suffix) {
    const size_t a = std::strlen(s);
    const size_t b = std::strlen(suffix);
    return a >= b && std::strcmp(s + a - b, suffix) == 0;
}

// Protection currently applied to the page that holds `addr` (from /proc/self/maps), or -1.
int protectionOf(uintptr_t addr) {
    FILE *f = std::fopen("/proc/self/maps", "r");
    if (!f)
        return -1;
    char line[512];
    int result = -1;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s", &start, &end, perms) != 3)
            continue;
        if (addr >= start && addr < end) {
            result = 0;
            if (perms[0] == 'r')
                result |= PROT_READ;
            if (perms[1] == 'w')
                result |= PROT_WRITE;
            if (perms[2] == 'x')
                result |= PROT_EXEC;
            break;
        }
    }
    std::fclose(f);
    return result;
}

bool writeSlot(void **slot, void *value) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    const uintptr_t page = reinterpret_cast<uintptr_t>(slot) & ~static_cast<uintptr_t>(pageSize - 1);
    int original = protectionOf(reinterpret_cast<uintptr_t>(slot));
    if (original < 0)
        original = PROT_READ;
    if (mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pageSize), PROT_READ | PROT_WRITE) != 0)
        return false;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    *slot = value;
    mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pageSize), original);
    return true;
}

void scanRelocations(const Elf64_Rela *relocs, size_t bytes, uintptr_t bias, const Elf64_Sym *symtab,
                     const char *strtab, Ctx &ctx) {
    const size_t count = bytes / sizeof(Elf64_Rela);
    for (size_t i = 0; i < count; ++i) {
        const Elf64_Rela &r = relocs[i];
        const uint32_t type = ELF64_R_TYPE(r.r_info);
        // R_AARCH64_GLOB_DAT = 1025, R_AARCH64_JUMP_SLOT = 1026
        if (type != 1025 && type != 1026)
            continue;
        const Elf64_Sym &sym = symtab[ELF64_R_SYM(r.r_info)];
        if (std::strcmp(strtab + sym.st_name, ctx.symbol) != 0)
            continue;

        void **slot = reinterpret_cast<void **>(bias + r.r_offset);
        if (ctx.expected && *slot != ctx.expected)
            continue;
        if (!ctx.expected && ctx.old && !*ctx.old)
            *ctx.old = *slot;
        if (writeSlot(slot, ctx.value))
            ++ctx.patched;
    }
}

int onLibrary(dl_phdr_info *info, size_t, void *data) {
    auto *ctx = static_cast<Ctx *>(data);
    if (!info->dlpi_name || !endsWith(info->dlpi_name, ctx->suffix))
        return 0;

    const ElfW(Dyn) *dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            dyn = reinterpret_cast<const ElfW(Dyn) *>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    }
    if (!dyn)
        return 1;

    const uintptr_t bias = info->dlpi_addr;
    auto fix = [bias](uintptr_t p) { return p >= bias ? p : p + bias; };

    const char *strtab = nullptr;
    const Elf64_Sym *symtab = nullptr;
    const Elf64_Rela *jmprel = nullptr, *rela = nullptr;
    size_t jmprelSize = 0, relaSize = 0;

    for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
        case DT_STRTAB:
            strtab = reinterpret_cast<const char *>(fix(d->d_un.d_ptr));
            break;
        case DT_SYMTAB:
            symtab = reinterpret_cast<const Elf64_Sym *>(fix(d->d_un.d_ptr));
            break;
        case DT_JMPREL:
            jmprel = reinterpret_cast<const Elf64_Rela *>(fix(d->d_un.d_ptr));
            break;
        case DT_PLTRELSZ:
            jmprelSize = d->d_un.d_val;
            break;
        case DT_RELA:
            rela = reinterpret_cast<const Elf64_Rela *>(fix(d->d_un.d_ptr));
            break;
        case DT_RELASZ:
            relaSize = d->d_un.d_val;
            break;
        default:
            break;
        }
    }

    if (!strtab || !symtab)
        return 1;
    if (jmprel && jmprelSize)
        scanRelocations(jmprel, jmprelSize, bias, symtab, strtab, *ctx);
    if (rela && relaSize)
        scanRelocations(rela, relaSize, bias, symtab, strtab, *ctx);
    return 1; // stop iterating
}

} // namespace

int replaceSymbol(const char *libSuffix, const char *symbol, void *expectedCurrent, void *newValue,
                  void **oldValue) {
    Ctx ctx{libSuffix, symbol, expectedCurrent, newValue, oldValue};
    dl_iterate_phdr(onLibrary, &ctx);
    return ctx.patched;
}

#else

int replaceSymbol(const char *, const char *, void *, void *, void **) { return 0; }

#endif

} // namespace color_grade::got
