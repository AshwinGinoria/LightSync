/* HTTP Provisioning Server — captive portal form handler.
 *
 * Serves a minimal HTML form on GET / that posts credentials to POST /connect.
 * Uses the config_storage API to persist WiFi credentials on successful submit.
 *
 * On x86 test builds (FLASH_MOCK), config_save() writes to the mock buffer.
 * On Pico builds, config_save() writes to real flash. */
#ifndef PROVISIONING_HTTP_H
#define PROVISIONING_HTTP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse an application/x-www-form-urlencoded POST body.
 * Extracts "ssid" and "password" parameters.
 * Returns 1 on success, 0 if ssid is empty or missing.
 * Truncates ssid to ssid_len-1 and password to pass_len-1. */
int parse_form(const char *body, char *ssid, char *pass,
               size_t ssid_len, size_t pass_len);

/* Build an HTTP 200 OK response with the given body.
 * Returns the number of bytes written (excluding null terminator).
 * Returns -1 if buffer is too small. */
int build_response_200(char *buf, size_t buf_size,
                       const char *body, size_t body_len);

/* Build an HTTP 302 Found redirect response.
 * Returns the number of bytes written.
 * Returns -1 if buffer is too small. */
int build_response_302(char *buf, size_t buf_size, const char *location);

/* Build the provisioning HTML form.
 * Returns the number of bytes written.
 * Returns -1 if buffer is too small. */
int build_provisioning_html(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* PROVISIONING_HTTP_H */
