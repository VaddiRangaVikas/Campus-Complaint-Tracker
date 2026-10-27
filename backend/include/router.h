#ifndef ROUTER_H
#define ROUTER_H

#include <stdbool.h>
#include "../lib/mongoose.h"
#include "common.h"

// Handle incoming HTTP request and dispatch to corresponding API endpoint or static file
void handle_http_request(struct mg_connection *c, struct mg_http_message *hm);

// Helper to extract session token from Cookie header or Authorization Bearer header
bool extract_auth_token(struct mg_http_message *hm, char *token_buf, size_t buf_size);

// Send standardized JSON response
void send_json_response(struct mg_connection *c, int status_code, bool success, const char *message, const char *json_data_str);

// Send JSON error
void send_json_error(struct mg_connection *c, int status_code, const char *message);

#endif // ROUTER_H
