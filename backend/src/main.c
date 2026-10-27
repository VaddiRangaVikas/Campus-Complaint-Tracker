#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "server.h"

static void signal_handler(int sig)
{
    (void)sig;
    printf("\n[Server] Interrupted by user. Shutting down gracefully...\n");
    server_stop();
}

int main(int argc, char *argv[])
{
    ServerConfig config;
    config.port = DEFAULT_PORT;
    strncpy(config.web_root, DEFAULT_WEB_ROOT, sizeof(config.web_root) - 1);
    strncpy(config.db_path, DEFAULT_DB_PATH, sizeof(config.db_path) - 1);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--web") == 0 && i + 1 < argc) {
            strncpy(config.web_root, argv[++i], sizeof(config.web_root) - 1);
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            strncpy(config.db_path, argv[++i], sizeof(config.db_path) - 1);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: campus_server.exe [options]\n");
            printf("Options:\n");
            printf("  --port <port>   Set HTTP server listening port (default: 8080)\n");
            printf("  --web <dir>     Set public directory path (default: public)\n");
            printf("  --db <path>     Set SQLite database file (default: campus_complaints.db)\n");
            printf("  --help          Display this help message\n");
            return 0;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!server_start(&config)) {
        return 1;
    }

    return 0;
}
