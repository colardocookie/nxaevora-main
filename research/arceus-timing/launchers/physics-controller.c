/* Local controller-capable test launcher for the elapsed-time physics candidate.
   Based on the earlier DLSS-controller.c helper. It never replaces an executable,
   never closes a game and adds no timeout: the player saves and closes the game. */
#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <stdio.h>
#include <wchar.h>
#define WORK L"C:\\Users\\brett\\Documents\\Codex\\work"
#define STAGE L"C:\\Users\\brett\\Documents\\Codex\\2026-11-07\\continue-from-renderer-handoff-txt\\work\\physics-stage"
#define RUNTIME STAGE L"\\ArceusTest.exe"
#define GAME WORK L"\\arceus-games\\ArceusTest"
#define TITLE L"Arceus elapsed physics test"
static int fail(const wchar_t* what) {
    wchar_t msg[512];
    swprintf(msg, 512, L"%ls\nWindows error: %lu\nNo running game will be closed.", what, GetLastError());
    MessageBoxW(NULL, msg, TITLE, MB_OK | MB_ICONERROR);
    return 1;
}
static HANDLE output(const wchar_t* dir, const wchar_t* name) {
    wchar_t path[1024];
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    swprintf(path, 1024, L"%ls\\%ls", dir, name);
    return CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}
static void env(const wchar_t* k, const wchar_t* v) { SetEnvironmentVariableW(k, v); }
static int runtime_ok(void) {
    static const unsigned char expected[32] = {
        0xb6,0x41,0xe8,0xe6,0x1c,0xf4,0x2f,0x46,0x61,0x32,0x2a,0x11,0xd1,0x2d,0x3f,0x0d,
        0x4a,0x68,0xe8,0x2c,0x55,0x81,0x9e,0x6a,0xf8,0xb2,0x0d,0x46,0xd5,0x3c,0xea,0x63};
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    unsigned char* object = NULL, digest[32];
    static unsigned char buffer[65536];
    DWORD size = 0, got = 0;
    int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&size, sizeof size, &got, 0) < 0) goto done;
    object = HeapAlloc(GetProcessHeap(), 0, size);
    if (!object || BCryptCreateHash(alg, &hash, object, size, NULL, 0, 0) < 0) goto done;
    file = CreateFileW(RUNTIME, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof buffer, &got, NULL)) goto done;
        if (!got) break;
        if (BCryptHashData(hash, buffer, got, 0) < 0) goto done;
    }
    if (BCryptFinishHash(hash, digest, sizeof digest, 0) < 0) goto done;
    ok = !memcmp(digest, expected, sizeof digest);
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) BCryptDestroyHash(hash);
    if (object) HeapFree(GetProcessHeap(), 0, object);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
int WINAPI wWinMain(HINSTANCE a, HINSTANCE b, LPWSTR command, int show) {
    (void)a; (void)b; (void)show;
    int verified = runtime_ok();
    if (command && wcsstr(command, L"--verify-only")) return verified ? 0 : 2;
    if (!verified) {
        MessageBoxW(NULL, L"Runtime hash mismatch. Nothing was launched.", TITLE, MB_OK | MB_ICONERROR);
        return 2;
    }
    HANDLE lock = CreateMutexW(NULL, FALSE, L"Local\\ArceusControllerResearchLaunch");
    if (!lock) return fail(L"Cannot create launcher lock");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"A controller test launcher is already active. Close the game yourself before another test.", TITLE, MB_OK);
        CloseHandle(lock);
        return 0;
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return fail(L"Cannot check for an existing game");
    PROCESSENTRY32W e = {.dwSize = sizeof e};
    BOOL more = Process32FirstW(snap, &e);
    while (more) {
        if (!_wcsicmp(e.szExeFile, L"ArceusTest.exe")) {
            CloseHandle(snap);
            MessageBoxW(NULL, L"ArceusTest is already running. Save and close it yourself before starting another test.", TITLE, MB_OK);
            CloseHandle(lock);
            return 0;
        }
        more = Process32NextW(snap, &e);
    }
    CloseHandle(snap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t run[1024], path[1024];
    swprintf(run, 1024, WORK L"\\arceus-runs\\timing-%04u%02u%02u-%02u%02u%02u-%03u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    if (!CreateDirectoryW(run, NULL)) return fail(L"Cannot create local capture directory");
    static wchar_t prior[32768], search[32768];
    DWORD n = GetEnvironmentVariableW(L"PATH", prior, 30000);
    if (!n || n >= 30000) prior[0] = 0;
    swprintf(search, 32768, WORK L"\\msys64\\clang64\\bin;" WORK L"\\llvm-mingw-20261104-ucrt-x86_64\\bin;%ls", prior);
    env(L"PATH", search);
    env(L"APPDATA", WORK L"\\arceus-appdata");
    /* Saved graphics settings apply, as in Start-WorldPT.ps1. */
    env(L"ARCEUS_UPSCALER", NULL); env(L"ARCEUS_NGX", NULL); env(L"ARCEUS_DLSS_RR", NULL); env(L"ARCEUS_RAY_TRACING", NULL);
    env(L"ARCEUS_PT_FILTER", L"1");
    env(L"SNX_ARCEUS_FPS60", L"0"); env(L"SNX_ARCEUS_TARGET_FPS", L"120"); env(L"SNX_ARCEUS_CAMERA_SPEED", L"2.2");
    env(L"SNX_ARCEUS_CLOCK_TRACE", L"1"); env(L"SNX_ARCEUS_TIMING_FIXES", L"1"); env(L"SNX_ARCEUS_EVENT_CAMERA_FIX", L"1");
    env(L"SNX_ARCEUS_BLINK_FIX", L"1"); env(L"SNX_ARCEUS_CAMERA_INPUT_FIX", L"1"); env(L"SNX_ARCEUS_MENU_REPEAT_FIX", L"1");
    env(L"SNX_ARCEUS_TIMING_RESEARCH", L"1"); env(L"SNX_ARCEUS_CAMERA_TRACE", L"0"); env(L"SNX_ARCEUS_DIALOGUE_TRACE", L"0");
    env(L"SNX_ARCEUS_FIXED_RESOLUTION", L"1"); env(L"SNX_ARCEUS_SCREEN_FETCH", L"1");
    env(L"SNX_ARCEUS_NEUTRAL_RIM", L"1"); env(L"SNX_ARCEUS_NATIVE_SHADOWS", L"1");
    env(L"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", L"1");
    /* The change under test. */
    env(L"SNX_ARCEUS_PHYSICS_TIMESTEP_FIX", L"1");
    env(L"SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX", L"1");
    env(L"SNX_ARCEUS_WORLD_TIMERS_FIX", L"1");
    env(L"SNX_ARCEUS_GRAVITY_FIX", L"1");
    env(L"SNX_ARCEUS_MOVEMENT_FIX", L"1");
    /* Bisect: 198E020 on, 1984A70 off. One of the two caused NPC spikes. */
    env(L"SNX_ARCEUS_MOVEMENT_SITES", L"1fd");
    env(L"SNX_ARCEUS_CAPTURE_FIX", L"1");
    /* Read-only: calls/s of the audited fixed-step owners. */
    env(L"SNX_ARCEUS_CADENCE_CENSUS", L"1");
    env(L"SNX_ARCEUS_GRAVITY_PROBE", L"1");
    env(L"SNX_ARCEUS_CAPTURE_WAIT_PROBE", L"1");
    env(L"SNX_ARCEUS_BATTLE_SEQUENCE_FIX", L"1");
    env(L"SNX_ARCEUS_MAP_CURSOR_FIX", L"1");
    swprintf(path, 1024, L"%ls\\submissions.csv", run);
    env(L"SNX_PRESENT_TRACE", path);
    swprintf(path, 1024, L"%ls\\test-settings.json", run);
    FILE* f = _wfopen(path, L"wt");
    if (f) {
        fprintf(f, "{\"target_fps\":120,\"camera_speed\":2.2,\"menu_repeat\":true,\"resolution_scale_override\":2.0,\"docked\":true,\"physics_timestep_fix\":true,\"ball_flight_correction\":true,\"navigation_timestep_fix\":true,\"world_timers_fix\":true,\"gravity_fix\":true,\"capture_fix\":true,\"cadence_census\":true,\"gravity_probe\":true,\"capture_wait_probe\":true,\"movement_fix\":true,\"movement_sites\":\"1fd\",\"map_cursor_fix\":true,\"battle_sequence_fix\":true,\"timing_research\":true,\"saved_graphics\":true,\"native_shadows\":true,\"screen_fetch_2x\":true,\"native_bc\":true,\"fixed_scene_resolution\":true,\"fullscreen\":true,\"controller_background\":true,\"desktop_launcher\":true,\"seconds\":0}\n");
        fclose(f);
    }
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    HANDLE out = output(run, L"game-out.txt"), err = output(run, L"game-err.txt");
    if (in == INVALID_HANDLE_VALUE || out == INVALID_HANDLE_VALUE || err == INVALID_HANDLE_VALUE) return fail(L"Cannot open capture streams");
    STARTUPINFOW si = {.cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES, .hStdInput = in, .hStdOutput = out, .hStdError = err};
    PROCESS_INFORMATION game = {0};
    wchar_t args[] = L"\"" RUNTIME L"\" --game=\"" GAME L"\" --nvn_native=0 --native_bc=1 --fps_limit=game --fullscreen=1 --log=1 --seconds=0 --gamepad_background=1 --docked=1 --resolution=2.0";
    if (!CreateProcessW(RUNTIME, args, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, GAME, &si, &game)) return fail(L"Cannot start the controller test");
    CloseHandle(game.hThread); CloseHandle(in); CloseHandle(out); CloseHandle(err);
    swprintf(path, 1024, L"%ls\\launch-status.txt", run);
    f = _wfopen(path, L"wt");
    if (f) { fprintf(f, "game_pid=%lu\nlauncher_pid=%lu\nmanual_close=1\n", game.dwProcessId, GetCurrentProcessId()); fclose(f); }
    WaitForSingleObject(game.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(game.hProcess, &code);
    CloseHandle(game.hProcess);
    f = _wfopen(path, L"at");
    if (f) { fprintf(f, "game_exit=%lu\n", code); fclose(f); }
    CloseHandle(lock);
    return 0;
}
