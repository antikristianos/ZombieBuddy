/* Isolated Windows JVM host: no Project Zomboid main or game installation edits. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <jni.h>

static WCHAR libraryPath[32768];
static char agentOption[65536], classpathOption[65536];
static JavaVMOption options[6];

static void log_message(const char *message) {
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)lstrlenA(message), &written, NULL);
}

static int utf8(const WCHAR *source, char *target, int capacity) {
    return WideCharToMultiByte(CP_UTF8, 0, source, -1, target, capacity, NULL, NULL);
}

static int native_option(const WCHAR *source, char *target, int capacity) {
    return WideCharToMultiByte(CP_ACP, 0, source, -1, target, capacity, NULL, NULL);
}

void mainCRTStartup(void) {
    typedef jint (JNICALL *CreateVm)(JavaVM **, void **, void *);
    union { FARPROC raw; jint (JNICALL *agent)(void*, char*, void*); } agentProc;
    int count;
    WCHAR **args = CommandLineToArgvW(GetCommandLineW(), &count);
    JavaVM *vm = NULL;
    JNIEnv *env = NULL;
    JavaVMInitArgs vmArgs;
    HMODULE library;
    CreateVm create;
    jclass probe, strings;
    jmethodID mainMethod;
    jobjectArray mainArgs;
    if (count < 5) { log_message("Host requires runtime bin, agent DLL, classpath, mode.\n"); ExitProcess(90); }
    if (lstrlenW(args[1]) + 32 >= 32768) ExitProcess(91);
    lstrcpyW(libraryPath, args[1]); lstrcatW(libraryPath, L"\\server\\jvm.dll");
    library = LoadLibraryExW(libraryPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) { log_message("Host cannot load JVM.\n"); ExitProcess(92); }
    create = (CreateVm)GetProcAddress(library, "JNI_CreateJavaVM");
    if (!create) ExitProcess(93);
    if (count > 5 && lstrcmpW(args[5], L"guard-only") == 0) {
        HMODULE agent = LoadLibraryExW(args[2], NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!agent) ExitProcess(103);
        agentProc.raw = GetProcAddress(agent, "Agent_OnLoad");
        if (!agentProc.raw) ExitProcess(104);
        /* Missing runtime dependency must fail before dereferencing this null VM. */
        ExitProcess(agentProc.agent(NULL, NULL, NULL) == -1 ? 1 : 0);
    }
    if (count > 5 && lstrcmpW(args[5], L"preload") == 0) {
        DWORD length = GetModuleFileNameW(NULL, libraryPath, 32768);
        while (length && libraryPath[length] != L'\\') length--;
        libraryPath[length] = 0; lstrcatW(libraryPath, L"\\java.dll");
        if (!LoadLibraryExW(libraryPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) ExitProcess(94);
    }
    lstrcpyA(agentOption, "-agentpath:");
    if (!native_option(args[2], agentOption + lstrlenA(agentOption), 65000)) ExitProcess(95);
    lstrcatA(agentOption, "=experimental,policy=deny-new,allow_unsigned_mods=false,frontend=console,config_dir=./config");
    lstrcpyA(classpathOption, "-Djava.class.path=");
    if (!native_option(args[3], classpathOption + lstrlenA(classpathOption), 65000)) ExitProcess(96);
    options[0].optionString = "-Xms32m";
    options[1].optionString = "-Xmx128m";
    options[2].optionString = "-XX:-CreateCoredumpOnCrash";
    options[3].optionString = agentOption;
    options[4].optionString = classpathOption;
    options[5].optionString = "--enable-native-access=ALL-UNNAMED";
    vmArgs.version = JNI_VERSION_1_8; vmArgs.nOptions = 6; vmArgs.options = options; vmArgs.ignoreUnrecognized = JNI_FALSE;
    if (create(&vm, (void**)&env, &vmArgs) != JNI_OK) ExitProcess(97);
    probe = (*env)->FindClass(env, "NativeGuardProbe");
    strings = (*env)->FindClass(env, "java/lang/String");
    if (!probe || !strings) { (*env)->ExceptionDescribe(env); ExitProcess(98); }
    mainMethod = (*env)->GetStaticMethodID(env, probe, "main", "([Ljava/lang/String;)V");
    mainArgs = (*env)->NewObjectArray(env, 2, strings, NULL);
    if (!mainMethod || !mainArgs) { (*env)->ExceptionDescribe(env); ExitProcess(99); }
    if (!utf8(args[1], classpathOption, 65536)) ExitProcess(100);
    (*env)->SetObjectArrayElement(env, mainArgs, 0, (*env)->NewStringUTF(env, classpathOption));
    if (!utf8(args[4], classpathOption, 65536)) ExitProcess(101);
    (*env)->SetObjectArrayElement(env, mainArgs, 1, (*env)->NewStringUTF(env, classpathOption));
    (*env)->CallStaticVoidMethod(env, probe, mainMethod, mainArgs);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); ExitProcess(102); }
    (*vm)->DestroyJavaVM(vm);
    ExitProcess(0);
}
