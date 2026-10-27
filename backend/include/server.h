#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

#define DEFAULT_PORT 8080
#define DEFAULT_WEB_ROOT "public"
#define DEFAULT_DB_PATH "campus_complaints.db"

typedef struct {
    int port;
    char web_root[256];
    char db_path[256];
} ServerConfig;

// Start HTTP server and listen on configured port
bool server_start(const ServerConfig *config);
void server_stop(void);

#endif // SERVER_H
