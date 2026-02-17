#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

// Initialize and start the async web server on port 443 (HTTPS)
void webserver_init();

// Call periodically to handle rate-limit expiry etc.
void webserver_loop();

#endif // WEB_SERVER_H
