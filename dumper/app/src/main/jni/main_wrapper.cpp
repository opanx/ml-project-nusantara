/*
 * External wrapper - converts .so to ELF executable
 * All logic stays in existing code (Main.cpp, Il2cpp, ImGui, etc.)
 */
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>

extern "C" void* hack_thread(void*);

static volatile int running = 1;
void sig_handler(int sig) { running = 0; }

int main(int argc, char* argv[]) {
    printf("============================================\n");
    printf("  Android-LibTool-New External v4.0\n");
    printf("  By Panxcz & Freebuff\n");
    printf("============================================\n");
    printf("[+] PID: %d\n", getpid());
    printf("[+] Target: liblogic.so (MLBB)\n");
    printf("[+] Ctrl+C to exit\n\n");

    chmod(argv[0], 0777);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    pthread_t t;
    pthread_create(&t, nullptr, hack_thread, nullptr);

    while (running) sleep(1);
    printf("[+] Done\n");
    return 0;
}
