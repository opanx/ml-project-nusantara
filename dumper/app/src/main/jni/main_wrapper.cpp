/*
 * External wrapper for Android-LibTool-New
 * Converts from .so (internal) to executable (external)
 * Just adds main() entry point - all logic stays in existing code
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>

// Forward declarations from existing code
extern "C" {
    void* hack_thread(void*);
}

static volatile int running = 1;

void signal_handler(int sig) {
    printf("\n[+] Signal %d received, exiting...\n", sig);
    running = 0;
}

int main(int argc, char* argv[]) {
    printf("============================================\n");
    printf("  Android-LibTool-New External v4.0\n");
    printf("  By Panxcz & Freebuff\n");
    printf("============================================\n\n");
    
    // Set permission to 777 for easy access
    chmod(argv[0], 0777);
    
    // Handle signals
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("[+] Running as external binary\n");
    printf("[+] PID: %d\n", getpid());
    printf("[+] Use Ctrl+C to exit\n\n");
    
    // Start the hack thread (existing code)
    pthread_t thread;
    pthread_create(&thread, nullptr, hack_thread, nullptr);
    
    // Wait for exit
    while (running) {
        sleep(1);
    }
    
    printf("[+] Cleanup done\n");
    return 0;
}
