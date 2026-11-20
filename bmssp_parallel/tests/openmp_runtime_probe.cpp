// Isolate runtime support from the solver: no bmssp headers or algorithm code.
#include <omp.h>
#include <cstdio>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <cstring>
#endif

int main() {
#ifdef _WIN32
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);
    // Record the actually loaded module, rather than assuming the first DLL
    // found on PATH is the one used by Windows' loader.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Module32First(snapshot, &entry)) do {
            if (std::strstr(entry.szModule, ".dll") &&
                (std::strstr(entry.szModule, "omp") || std::strstr(entry.szModule, "gomp")))
                std::fprintf(stderr, "loaded_runtime_module=%s\n", entry.szExePath);
        } while (Module32Next(snapshot, &entry));
        CloseHandle(snapshot);
    }
#endif
    omp_set_num_threads(4);
    std::fprintf(stderr, "thread_limit=%d dynamic=%d\n",
                 omp_get_thread_limit(), omp_get_dynamic());
    std::fflush(stderr);
    for (int requested : {1, 2, 4, 8}) {
        std::fprintf(stderr, "requested=%d\n", requested);
        std::fflush(stderr);
        #pragma omp parallel num_threads(requested)
        {
            #pragma omp single
            {
                std::fprintf(stderr, "actual=%d\n", omp_get_num_threads());
                std::fflush(stderr);
            }
        }
    }
    std::puts("PASS: minimal OpenMP runtime probe");
}
