#include <CoreFoundation/CoreFoundation.h>
#include "MobileDevice.h"

typedef struct {
    service_conn_t connection;
    CFSocketRef socket;
    CFRunLoopSourceRef source;
} DeviceConsoleConnection;

typedef struct {
    char *name;
    char *pid;
    char *extension;
} ProcessParams, *ProcessParams_p;

typedef struct {
    char *reset;
    char *normal;
    char *dark;
    char *red;
    char *dark_red;
    char *green;
    char *dark_green;
    char *yellow;
    char *dark_yellow;
    char *blue;
    char *dark_blue;
    char *magenta;
    char *dark_magenta;
    char *cyan;
    char *dark_cyan;
    char *white;
    char *dark_white;
} Colors, *Colors_p;

typedef struct {
    int debug;
    CFMutableDictionaryRef liveConnections;
    CFStringRef requiredDeviceId;
    char *requiredProcessNames;
    char *requiredExtensionNames;
    bool use_regex;
    regex_t requiredRegex;
    void (*printMessage)(int fd, const char *, size_t);
    void (*printSeparator)(int fd);
} Config, *Config_p;
