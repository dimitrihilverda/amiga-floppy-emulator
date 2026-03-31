#ifndef WEBUI_H
#define WEBUI_H

#include "config.h"

// Initialize WiFi AP and web server
void webui_begin();

// Handle web server requests (call from loop())
void webui_update();

#endif // WEBUI_H
