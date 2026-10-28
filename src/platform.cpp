#include "mod.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace gd {
Config config;
HMODULE self = nullptr, game = nullptr;
static std::mutex log_mutex;
void log(const char* format, ...) {
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    std::lock_guard<std::mutex> guard(log_mutex);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    if (auto slash = wcsrchr(path, L'\\'))
        slash[1] = 0;
    wcscat_s(path, L"gd-arena-fix.log");
    FILE* file = nullptr;
    _wfopen_s(&file, path, L"a");
    if (file) {
        fprintf(file, "[%.6f] %s\n", seconds(), text);
        fclose(file);
    }
    OutputDebugStringA(text);
}
double seconds() {
    static const double frequency = [] {
        LARGE_INTEGER v;
        QueryPerformanceFrequency(&v);
        return double(v.QuadPart);
    }();
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return double(v.QuadPart) / frequency;
}
bool write_memory(void* destination, const void* source, size_t size) {
    DWORD protection;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &protection))
        return false;
    memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    DWORD ignored;
    return VirtualProtect(destination, size, protection, &ignored) != FALSE;
}
void* at(uintptr_t va) {
    return reinterpret_cast<uint8_t*>(game) + (va - target::get().preferred_base);
}
static void** find_import(HMODULE module, const char* name, const char* dll, unsigned ordinal) {
    auto base = reinterpret_cast<uint8_t*>(module);
    if (!base || reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return nullptr;
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return nullptr;
    auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; desc->Name; ++desc) {
        if (dll && _stricmp(reinterpret_cast<const char*>(base + desc->Name), dll))
            continue;
        if (!desc->OriginalFirstThunk)
            continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                if (!name && IMAGE_ORDINAL64(names->u1.Ordinal) == ordinal)
                    return reinterpret_cast<void**>(&slots->u1.Function);
                continue;
            }
            if (!name)
                continue;
            auto item = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (!strcmp(reinterpret_cast<const char*>(item->Name), name))
                return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}
void** import_slot(HMODULE module, const char* name) {
    return find_import(module, name, nullptr, 0);
}
void** import_ordinal(HMODULE module, const char* dll, unsigned ordinal) {
    return find_import(module, nullptr, dll, ordinal);
}
std::string sha256_file(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        uint8_t buffer[65536], digest[32];
        DWORD read;
        bool good = true;
        while (true) {
            if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
                good = false;
                break;
            }
            if (!read)
                break;
            if (BCryptHashData(hash, buffer, read, 0) < 0) {
                good = false;
                break;
            }
        }
        if (good && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0) {
            char hex[65];
            for (size_t i = 0; i < sizeof(digest); ++i)
                sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
            result = hex;
        }
    }
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}
bool validate_game(HMODULE module) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(module, path, MAX_PATH))
        return false;
    const auto hash = sha256_file(path);
    log("gdxg.dll SHA256: %s", hash.c_str());
    return target::select(hash);
}
// Replace individual slots in the existing table. Native D3D implementations
// can use private slots beyond the public COM interface: cloning only the public
// table truncates those slots and is unsafe. Calls still chain to Spice wrappers.
struct Table {
    void** address;
    std::unique_ptr<void*[]> original;
    size_t count;
};
static std::vector<Table> tables;
static std::mutex tables_mutex;
// Original snapshots are immutable and remain allocated for process life. Cache
// the last registered vtable per thread so wrapped COM hot paths avoid locking
// and scanning `tables` after the first lookup. Only positive table hits enter
// this cache; a vtable seen before hook_method registers it must be looked up
// again after registration.
struct OriginalTableCache {
    void** address = nullptr;
    void** original = nullptr;
    size_t count = 0;
};
static thread_local OriginalTableCache original_table_cache;

void hook_method(void* object, size_t count, size_t index, void* replacement) {
    std::lock_guard<std::mutex> guard(tables_mutex);
    auto current = *reinterpret_cast<void***>(object);
    bool known = false;
    for (auto& table : tables)
        if (table.address == current)
            known = true;
    if (!known) {
        Table table{current, std::make_unique<void*[]>(count), count};
        memcpy(table.original.get(), current, count * sizeof(void*));
        tables.push_back(std::move(table));
    }
    if (!write_memory(current + index, &replacement, sizeof(replacement)))
        throw std::runtime_error("cannot write COM method slot");
}
void* original_method(void* object, size_t index) {
    auto current = *reinterpret_cast<void***>(object);
    const auto& cached = original_table_cache;
    if (cached.address == current && cached.original && index < cached.count)
        return cached.original[index];

    std::lock_guard<std::mutex> guard(tables_mutex);
    for (auto& table : tables)
        if (table.address == current) {
            if (index >= table.count)
                return current[index];
            original_table_cache = {table.address, table.original.get(), table.count};
            return table.original[index];
        }
    return current[index];
}
} // namespace gd
