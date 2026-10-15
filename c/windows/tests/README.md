# Windows runtime-library regression tests

## Protection contract

The anchor is the already running JVM, resolved from its module handle, not a hardcoded game directory or the test staging path. Before loading Java/instrumentation dependencies, module enumeration must prove there is exactly one `jvm.dll`. A second mapped JVM is rejected even if it is byte-identical. Java/instrumentation modules must identify the physical files in that JVM's runtime directory; independent copies are conflicts. Junction/hardlink aliases of the same file are allowed when Windows reuses one mapped module. Multiple mapped instances remain conflicts, regardless of aliasing. This is an early dependency-selection guard, not authentication of the JVM installation or protection against concurrent hostile DLL injection.

File comparisons use `GetFileInformationByHandleEx(FileIdInfo)` with the full 128-bit file ID and 64-bit volume serial number, following [Microsoft's identity contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info). The previous 64-bit file index is insufficient to establish uniqueness on ReFS. If opening a file or querying its identity fails, comparison fails closed; no path-string or 64-bit fallback is used. This adds an API requirement for FileIdInfo support on the runtime filesystem. Windows / Project Zomboid 42.21 is the tested target; a real ReFS installation was not tested.

Two explicit host regressions exercise the distinction: `guard-duplicate-jvm` proves two separate mapped JVM handles and zero Java/instrumentation modules, invokes the guard without creating a VM, and requires refusal with those dependency modules still absent. `guard-alias` preloads the real runtime `java.dll` via its original path, proves a different junction path reuses that module, and then starts the actual JVM/agent successfully. Together with the existing eight cases, there are ten native-host cases.

`NativeIdentityProbe.c` exercises the real comparator with controlled FileIdInfo responses and real open/closed handles. It checks differences in each of the 16 ID bytes, the upper volume bits, failed first/second queries, failed first/second opens and handle cleanup. These controlled responses test comparison width and failure handling, not actual ReFS behavior.

## Running the tests

These isolated tests start the real bundled JVM/agent, never a game or server main. They contrast the original loader with the guarded native candidate in executable directories containing no leftovers, copied Java/instrumentation DLLs, invalid DLLs and Unicode/spaces paths. They also reject a preloaded foreign Java DLL and a missing canonical runtime dependency without fallback. The actual native encoding exception is reproduced naturally; it is not injected.

Requirements: Windows x64, MSVC x64 tools and Windows SDK, JDK 25, the installed Project Zomboid 42.21 runtime/game JAR, an unmodified official ZombieBuddy 2.3.4 release JAR and original native DLL (SHA-256 `C2AE9335E717EE24B2F4A40D1A3BF77F1519762A72A0459E766A2BBAFC077F6C`). The tests use only upstream agent APIs and bundled game JNA to verify loaded module identities.

```powershell
./c/windows/tests/Test-RuntimeGuard.ps1 -GameRoot 'E:\Steam\steamapps\common\ProjectZomboid' -AgentJar '<official ZombieBuddy.jar>' -OriginalDll '<original zbNative.dll>'
```

The test compiles the native candidate with MSVC (without adding a CRT dependency); the ordinary MinGW Makefile remains supported but was not exercised here. Outputs are under ignored `build/native-runtime-guard/`. Initial staging creation is safe only for a new/generated directory; unknown existing staging is rejected. Runtime directory junctions are read-only. Do not recursively traverse those junctions during cleanup; remove the junction entries themselves.

The guarded process must load exactly one `java.dll` and one `instrument.dll` from its active runtime. Localhost resolution, socket/HTTP-client creation and native agent option forwarding must succeed. Already loaded conflicting JVM modules cause an explicit guard message and startup refusal; they are never forcibly unloaded. No file is removed from or installed in the game directory.

The native option buffer is 4096 bytes including `ZombieBuddy.jar`, the optional separator/tail and the trailing NUL. It is allocated on the process heap and released after callback return, avoiding a larger stack frame in the CRT-free loader. A C regression checks exact capacity, overflow refusal, guard bytes, argument/return-value forwarding and that overflow does not invoke the callback. Additional real bundled-JVM cases verify intact delivery at exactly 4096 bytes and refusal at 4097 bytes. Imports remain KERNEL32/USER32 only; the three original agent exports remain unchanged. Conservative Java-side forwarding limits remain compatible with both old and expanded native loaders.

Every ordinary case passes its staged `jre64/bin` junction to the native host. The Unicode/spaces case uses an explicit `unicode` mode: before JVM creation, the host checks the real `jvm.dll` module path with `GetModuleFileNameW`, requires a non-ASCII character and verifies that the requested staged path was retained. After initialization the Java probe checks exactly one JVM, Java and instrumentation module, their file identities and their actual staged Unicode paths. File identity alone is not Unicode-path evidence because the junction and original runtime identify the same files. The separate missing-dependency case retains its deliberately incomplete runtime.

These results do not establish full game/client/Coop/dedicated acceptance or native-attach behavior on an existing production JVM.
