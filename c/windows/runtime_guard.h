/* Restrict instrumentation dependencies to the active Windows JVM runtime. */
#include <tlhelp32.h>

#define RUNTIME_PATH_CAPACITY 32768
static WCHAR runtimeJavaPath[RUNTIME_PATH_CAPACITY];
static WCHAR runtimeInstrumentPath[RUNTIME_PATH_CAPACITY];
static WCHAR runtimeLoadedPath[RUNTIME_PATH_CAPACITY];
static HMODULE runtimeJava = NULL;

static void guard_write(const char *message) {
    DWORD written;
    HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    if (output != NULL && output != INVALID_HANDLE_VALUE)
        WriteFile(output, message, (DWORD)lstrlenA(message), &written, NULL);
}

static void guard_write_path(const WCHAR *path) {
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    if (size > 0) {
        char *encoded = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size);
        if (encoded) {
            WideCharToMultiByte(CP_UTF8, 0, path, -1, encoded, size, NULL, NULL);
            guard_write(encoded);
            HeapFree(GetProcessHeap(), 0, encoded);
        }
    }
}

/* Stable machine-readable codes complement the existing human-readable text. */
static int guard_fail(const char *code, const char *reason, const WCHAR *path) {
    guard_write("[ZB-NATIVE] JVM DLL guard: ");
    guard_write(reason);
    if (path) { guard_write(" "); guard_write_path(path); }
    guard_write(" [code="); guard_write(code); guard_write("]");
    guard_write("\n[ZB-NATIVE] Startup stopped. Close the game, remove leftover Java DLL copies outside the active runtime and verify game files with Steam. Keep runtime DLLs intact.\n");
    return 0;
}

/* Compare all 128 file-ID bits: ReFS does not guarantee unique 64-bit IDs.
 * Equivalent junction/hardlink paths are allowed; copied files are not. */
static int same_runtime_file(const WCHAR *left, const WCHAR *right) {
    FILE_ID_INFO a, b;
    unsigned int i;
    HANDLE first = CreateFileW(left, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE second = CreateFileW(right, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    int same = first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE
        && GetFileInformationByHandleEx(first, FileIdInfo, &a, sizeof(a))
        && GetFileInformationByHandleEx(second, FileIdInfo, &b, sizeof(b))
        && a.VolumeSerialNumber == b.VolumeSerialNumber;
    if (same) {
        for (i = 0; i < sizeof(a.FileId.Identifier); i++) {
            if (a.FileId.Identifier[i] != b.FileId.Identifier[i]) { same = 0; break; }
        }
    }
    if (first != INVALID_HANDLE_VALUE) CloseHandle(first);
    if (second != INVALID_HANDLE_VALUE) CloseHandle(second);
    return same;
}

static int check_runtime_modules(void) {
    MODULEENTRY32W entry;
    DWORD enumerationError;
    DWORD javaCount = 0, instrumentCount = 0, jvmCount = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return guard_fail("ZB_GUARD_SNAPSHOT", "Cannot inspect loaded JVM DLLs.", NULL);
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return guard_fail("ZB_GUARD_ENUMERATION", "Cannot enumerate loaded JVM DLLs.", NULL);
    }
    do {
        const WCHAR *expected = NULL;
        if (lstrcmpiW(entry.szModule, L"java.dll") == 0) { javaCount++; expected = runtimeJavaPath; }
        if (lstrcmpiW(entry.szModule, L"instrument.dll") == 0) { instrumentCount++; expected = runtimeInstrumentPath; }
        if (lstrcmpiW(entry.szModule, L"jvm.dll") == 0) jvmCount++;
        if (expected) {
            DWORD size = GetModuleFileNameW(entry.hModule, runtimeLoadedPath, RUNTIME_PATH_CAPACITY);
            if (!size || size >= RUNTIME_PATH_CAPACITY) {
                CloseHandle(snapshot);
                return guard_fail("ZB_GUARD_MODULE_PATH", "Cannot resolve a loaded JVM DLL path.", NULL);
            }
            if (!same_runtime_file(runtimeLoadedPath, expected)) {
                CloseHandle(snapshot);
                return guard_fail("ZB_GUARD_MODULE_IDENTITY", "Unexpected already-loaded JVM DLL:", runtimeLoadedPath);
            }
        }
    } while (Module32NextW(snapshot, &entry));
    enumerationError = GetLastError();
    CloseHandle(snapshot);
    if (enumerationError != ERROR_NO_MORE_FILES)
        return guard_fail("ZB_GUARD_ENUMERATION", "Loaded-module enumeration did not complete.", NULL);
    if (javaCount > 1 || instrumentCount > 1 || jvmCount != 1)
        return guard_fail("ZB_GUARD_MODULE_COUNT", "Multiple JVM DLL instances are already loaded.", NULL);
    return 1;
}

static HMODULE load_runtime_instrument(void) {
    WCHAR *last;
    DWORD length;
    HMODULE instrument;
    /* Anchor to the running JVM, not an installation/staging path. The module
     * enumeration below rejects ambiguous multiple JVMs before loading deps. */
    HMODULE jvm = GetModuleHandleW(L"jvm.dll");
    if (!jvm) { guard_fail("ZB_GUARD_JVM_MISSING", "Cannot locate the active jvm.dll.", NULL); return NULL; }
    length = GetModuleFileNameW(jvm, runtimeJavaPath, RUNTIME_PATH_CAPACITY);
    if (!length || length >= RUNTIME_PATH_CAPACITY) {
        guard_fail("ZB_GUARD_JVM_PATH", "Cannot resolve the active JVM directory.", NULL); return NULL;
    }
    /* Supported Windows runtime layout: bin/server/jvm.dll (or bin/client/jvm.dll). */
    last = runtimeJavaPath + length;
    while (last > runtimeJavaPath && *last != L'\\') last--;
    *last = 0;
    while (last > runtimeJavaPath && *last != L'\\') last--;
    if (lstrcmpiW(last + 1, L"server") && lstrcmpiW(last + 1, L"client")) {
        guard_fail("ZB_GUARD_JVM_LAYOUT", "Unsupported JVM directory:", runtimeJavaPath); return NULL;
    }
    *last = 0;
    if (lstrlenW(runtimeJavaPath) + 16 >= RUNTIME_PATH_CAPACITY) {
        guard_fail("ZB_GUARD_PATH_LENGTH", "Runtime DLL path is too long.", NULL); return NULL;
    }
    lstrcpyW(runtimeInstrumentPath, runtimeJavaPath);
    lstrcatW(runtimeJavaPath, L"\\java.dll");
    lstrcatW(runtimeInstrumentPath, L"\\instrument.dll");
    if (!check_runtime_modules()) return NULL;
    /* Load the canonical java.dll first. Its state will be initialized by this JVM. */
    runtimeJava = LoadLibraryExW(runtimeJavaPath, NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!runtimeJava) { guard_fail("ZB_GUARD_JAVA_LOAD", "Cannot load active runtime java.dll:", runtimeJavaPath); return NULL; }
    if (!check_runtime_modules()) return NULL;
    instrument = LoadLibraryExW(runtimeInstrumentPath, NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!instrument) { guard_fail("ZB_GUARD_INSTRUMENT_LOAD", "Cannot load active runtime instrument.dll:", runtimeInstrumentPath); return NULL; }
    if (!check_runtime_modules()) { FreeLibrary(instrument); return NULL; }
    return instrument;
}
