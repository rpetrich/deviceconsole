#include <stdio.h>
#include <regex.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include "MobileDevice.h"

typedef struct {
    service_conn_t connection;
    CFSocketRef socket;
    CFRunLoopSourceRef source;
} DeviceConsoleConnection;

static CFMutableDictionaryRef liveConnections;
static int debug;
static CFStringRef requiredDeviceId;
static char *requiredProcessName;
static regex_t *requiredRegex;
static void (*printMessage)(int fd, const char *, size_t);
static void (*printSeparator)(int fd);

static char *COLOR_RESET = NULL;
static char *COLOR_NORMAL = NULL;
static char *COLOR_DARK = NULL;
static char *COLOR_RED = NULL;
static char *COLOR_DARK_RED = NULL;
static char *COLOR_GREEN = NULL;
static char *COLOR_DARK_GREEN = NULL;
static char *COLOR_YELLOW = NULL;
static char *COLOR_DARK_YELLOW = NULL;
static char *COLOR_BLUE = NULL;
static char *COLOR_DARK_BLUE = NULL;
static char *COLOR_MAGENTA = NULL;
static char *COLOR_DARK_MAGENTA = NULL;
static char *COLOR_CYAN = NULL;
static char *COLOR_DARK_CYAN = NULL;
static char *COLOR_WHITE = NULL;
static char *COLOR_DARK_WHITE = NULL;

static inline void write_fully(int fd, const char *buffer, size_t length)
{
    while (length) {
        ssize_t result = write(fd, buffer, length);
        if (result == -1)
            break;
        buffer += result;
        length -= result;
    }
}

static inline void write_string(int fd, const char *string)
{
    write_fully(fd, string, strlen(string));
}

static int find_space_offsets(const char *buffer, size_t length, size_t *space_offsets_out)
{
    int o = 0;
    for (size_t i = 16; i < length; i++) {
        if (buffer[i] == ' ') {
            space_offsets_out[o++] = i;
            if (o == 3) {
                break;
            }
        }
    }
    return o;
}
static unsigned char should_print_message(const char *buffer, size_t length)
{
    if (length < 3) return 0; // don't want blank lines
    
    unsigned char should_print = 1;
    
    size_t space_offsets[3];
    find_space_offsets(buffer, length, space_offsets);
    
    // Check whether process name matches the one passed to -p option and filter if needed
    if (requiredProcessName != NULL) {
        int nameLength = space_offsets[1] - space_offsets[0]; //This size includes the NULL terminator.
        
        char *processName = malloc(nameLength);
        processName[nameLength - 1] = '\0';
        memcpy(processName, buffer + space_offsets[0] + 1, nameLength - 1);

        for (int i = strlen(processName); i != 0; i--)
            if (processName[i] == '[')
                processName[i] = '\0';
        
        if (strcmp(processName, requiredProcessName) != 0)
            should_print = 0;
            
        free(processName);
    }
    
    if (requiredRegex != NULL) {
        char *message = malloc(length + 1);
        memcpy(message, buffer, length + 1);
        message[length + 1] = '\0';
        
        if (regexec(requiredRegex, message, 0, NULL, 0) == REG_NOMATCH)
            should_print = 0;
    }
    
    // More filtering options can be added here and return 0 when they won't meed filter criteria
    
    return should_print;
}

#define write_const(fd, text) write_fully(fd, text, strlen(text))
#define stringify(x) #x
#define xcode_color_with_rgb(type, r, g, b) "\e[" type stringify(r) "," stringify(g) "," stringify(b) ";"

static void set_colors(xcode_colors) {
    COLOR_RESET         = (xcode_colors) ? "\e[;"                                       :   "\e[m";
    COLOR_NORMAL        = (xcode_colors) ? xcode_color_with_rgb("fg", 0, 0, 0)          :   "\e[0m";
    COLOR_DARK          = (xcode_colors) ? xcode_color_with_rgb("fg", 207, 201, 189)    :   "\e[2m";
    COLOR_RED           = (xcode_colors) ? xcode_color_with_rgb("fg", 255, 31, 2)       :   "\e[0;31m";
    COLOR_DARK_RED      = (xcode_colors) ? xcode_color_with_rgb("fg", 148, 18, 1)       :   "\e[2;31m";
    COLOR_GREEN         = (xcode_colors) ? xcode_color_with_rgb("fg", 2, 255, 14)       :   "\e[0;32m";
    COLOR_DARK_GREEN    = (xcode_colors) ? xcode_color_with_rgb("fg", 1, 139, 8)        :   "\e[2;32m";
    COLOR_YELLOW        = (xcode_colors) ? xcode_color_with_rgb("fg", 255, 228, 0)      :   "\e[0;33m";
    COLOR_DARK_YELLOW   = (xcode_colors) ? xcode_color_with_rgb("fg", 167, 149, 1)      :   "\e[2;33m";
    COLOR_BLUE          = (xcode_colors) ? xcode_color_with_rgb("fg", 0, 0, 255)        :   "\e[0;34m";
    COLOR_DARK_BLUE     = (xcode_colors) ? xcode_color_with_rgb("fg", 0, 0, 100)        :   "\e[2;34m";
    COLOR_MAGENTA       = (xcode_colors) ? xcode_color_with_rgb("fg", 213, 149, 139)    :   "\e[0;35m";
    COLOR_DARK_MAGENTA  = (xcode_colors) ? xcode_color_with_rgb("fg", 145, 100, 165)    :   "\e[2;35m";
    COLOR_CYAN          = (xcode_colors) ? xcode_color_with_rgb("fg", 45, 253, 255)     :   "\e[0;36m";
    COLOR_DARK_CYAN     = (xcode_colors) ? xcode_color_with_rgb("fg", 34, 129, 128)     :   "\e[2;36m";
    COLOR_WHITE         = (xcode_colors) ? xcode_color_with_rgb("fg", 255, 255, 255)    :   "\e[0;37m";
    COLOR_DARK_WHITE    = (xcode_colors) ? xcode_color_with_rgb("fg", 180, 180, 180)    :   "\e[0;37m";
}

static void write_colored(int fd, const char *buffer, size_t length)
{
    if (length < 16) {
        write_fully(fd, buffer, length);
        return;
    }
    size_t space_offsets[3];
    int o = find_space_offsets(buffer, length, space_offsets);
    
    if (o == 3) {
        
        // Log date and device name
        write_const(fd, COLOR_DARK_WHITE);
        write_fully(fd, buffer, space_offsets[0]);
        // Log process name
        int pos = 0;
        for (int i = space_offsets[0]; i < space_offsets[0]; i++) {
            if (buffer[i] == '[') {
                pos = i;
                break;
            }
        }
        write_const(fd, COLOR_CYAN);
        if (pos && buffer[space_offsets[1]-1] == ']') {
            write_fully(fd, buffer + space_offsets[0], pos - space_offsets[0]);
            write_const(fd, COLOR_DARK_CYAN);
            write_fully(fd, buffer + pos, space_offsets[1] - pos);
        } else {
            write_fully(fd, buffer + space_offsets[0], space_offsets[1] - space_offsets[0]);
        }
        // Log level
        size_t levelLength = space_offsets[2] - space_offsets[1];
        if (levelLength > 4) {
            const char *normalColor;
            const char *darkColor;
            if (levelLength == 9 && memcmp(buffer + space_offsets[1], " <Debug>:", 9) == 0){
                normalColor = COLOR_MAGENTA;
                darkColor = COLOR_DARK_MAGENTA;
            } else if (levelLength == 11 && memcmp(buffer + space_offsets[1], " <Warning>:", 11) == 0){
                normalColor = COLOR_YELLOW;
                darkColor = COLOR_DARK_YELLOW;
            } else if (levelLength == 9 && memcmp(buffer + space_offsets[1], " <Error>:", 9) == 0){
                normalColor = COLOR_RED;
                darkColor = COLOR_DARK_RED;
            } else if (levelLength == 10 && memcmp(buffer + space_offsets[1], " <Notice>:", 10) == 0) {
                normalColor = COLOR_GREEN;
                darkColor = COLOR_DARK_GREEN;
            } else {
                goto level_unformatted;
            }
            write_string(fd, darkColor);
            write_fully(fd, buffer + space_offsets[1], 2);
            write_string(fd, normalColor);
            write_fully(fd, buffer + space_offsets[1] + 2, levelLength - 4);
            write_string(fd, darkColor);
            write_fully(fd, buffer + space_offsets[1] + levelLength - 2, 1);
            write_const(fd, COLOR_DARK_WHITE);
            write_fully(fd, buffer + space_offsets[1] + levelLength - 1, 1);
        } else {
        level_unformatted:
            write_const(fd, COLOR_RESET);
            write_fully(fd, buffer + space_offsets[1], levelLength);
        }
        write_const(fd, COLOR_RESET);
        write_fully(fd, buffer + space_offsets[2], length - space_offsets[2]);
    } else {
        write_fully(fd, buffer, length);
    }
}
static void SocketCallback(CFSocketRef s, CFSocketCallBackType type, CFDataRef address, const void *data, void *info)
{
    // Skip null bytes
    ssize_t length = CFDataGetLength(data);
    const char *buffer = (const char *)CFDataGetBytePtr(data);
    while (length) {
        while (*buffer == '\0') {
            buffer++;
            length--;
            if (length == 0)
                return;
        }
        size_t extentLength = 0;
        while ((buffer[extentLength] != '\0') && extentLength != length) {
            extentLength++;
        }
        
        if (should_print_message(buffer, extentLength)) {
            printMessage(1, buffer, extentLength);
            printSeparator(1);
        }
        
        length -= extentLength;
        buffer += extentLength;
    }
}

static void DeviceNotificationCallback(am_device_notification_callback_info *info, void *unknown)
{
    struct am_device *device = info->dev;
    switch (info->msg) {
        case ADNCI_MSG_CONNECTED: {
            if (debug) {
                CFStringRef deviceId = AMDeviceCopyDeviceIdentifier(device);
                CFStringRef str = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("deviceconsole connected: %@"), deviceId);
                CFRelease(deviceId);
                CFShow(str);
                CFRelease(str);
            }
            if (requiredDeviceId) {
                CFStringRef deviceId = AMDeviceCopyDeviceIdentifier(device);
                Boolean isRequiredDevice = CFEqual(deviceId, requiredDeviceId);
                CFRelease(deviceId);
                if (!isRequiredDevice)
                    break;
            }
            if (AMDeviceConnect(device) == MDERR_OK) {
                if (AMDeviceIsPaired(device) && (AMDeviceValidatePairing(device) == MDERR_OK)) {
                    if (AMDeviceStartSession(device) == MDERR_OK) {
                        service_conn_t connection;
                        if (AMDeviceStartService(device, AMSVC_SYSLOG_RELAY, &connection, NULL) == MDERR_OK) {
                            CFSocketRef socket = CFSocketCreateWithNative(kCFAllocatorDefault, connection, kCFSocketDataCallBack, SocketCallback, NULL);
                            if (socket) {
                                CFRunLoopSourceRef source = CFSocketCreateRunLoopSource(kCFAllocatorDefault, socket, 0);
                                if (source) {
                                    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
                                    AMDeviceRetain(device);
                                    DeviceConsoleConnection *data = malloc(sizeof *data);
                                    data->connection = connection;
                                    data->socket = socket;
                                    data->source = source;
                                    CFDictionarySetValue(liveConnections, device, data);
                                    return;
                                }
                                CFRelease(source);
                            }
                        }
                        AMDeviceStopSession(device);
                    }
                }
            }
            AMDeviceDisconnect(device);
            break;
        }
        case ADNCI_MSG_DISCONNECTED: {
            if (debug) {
                CFStringRef deviceId = AMDeviceCopyDeviceIdentifier(device);
                CFStringRef str = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("deviceconsole disconnected: %@"), deviceId);
                CFRelease(deviceId);
                CFShow(str);
                CFRelease(str);
            }
            DeviceConsoleConnection *data = (DeviceConsoleConnection *)CFDictionaryGetValue(liveConnections, device);
            if (data) {
                CFDictionaryRemoveValue(liveConnections, device);
                AMDeviceRelease(device);
                CFRunLoopRemoveSource(CFRunLoopGetMain(), data->source, kCFRunLoopCommonModes);
                CFRelease(data->source);
                CFRelease(data->socket);
                free(data);
                AMDeviceStopSession(device);
                AMDeviceDisconnect(device);
            }
            break;
        }
        default:
            break;
    }
}

static void no_separator(int fd)
{
}

static void plain_separator(int fd)
{
    write_const(fd, "--\n");
}

static void color_separator(int fd)
{
    size_t buffer_lentgh = sizeof(char) * (strlen(COLOR_DARK_WHITE) + strlen(COLOR_RESET) + 3);
    char *buffer = malloc(buffer_lentgh);
    sprintf(buffer, "%s--%s\n", COLOR_DARK_WHITE, COLOR_RESET);
    
    write_const(fd, buffer);
    free(buffer);
}

int main (int argc, char * const argv[])
{
    if ((argc == 2) && (strcmp(argv[1], "--help") == 0)) {
        fprintf(stderr, "Usage: %s [options]\nOptions:\n -d\t\t\tInclude connect/disconnect messages in standard out\n -u <udid>\t\tShow only logs from a specific device\n -p <process name>\tShow only logs from a specific process\n -r <regular expression>\tFilter messages by regular expression.\n\nControl-C to disconnect\nMail bug reports and suggestions to <ryan.petrich@medialets.com>\n", argv[0]);
        return 1;
    }
    int c;
    bool use_separators = false;
    bool force_color = isatty(1);
    bool xcode_colors = (getenv("XcodeColors"));

    while ((c = getopt(argc, argv, "dcxsu:p:r:")) != -1)
        switch (c)
    {
        case 'd':
            debug = 1;
            break;
        case 'c':
            force_color = true;
            break;
        case 'x':
            xcode_colors = true;
            break;
        case 's':
            use_separators = true;
            break;
        case 'u':
            if (requiredDeviceId)
                CFRelease(requiredDeviceId);
            requiredDeviceId = CFStringCreateWithCString(kCFAllocatorDefault, optarg, kCFStringEncodingASCII);
            break;
        case 'p':
            requiredProcessName = malloc(strlen(optarg) + 1);
            requiredProcessName[strlen(optarg)] = '\0';

            strcpy(requiredProcessName, optarg);
            break;
        case 'r':
            requiredRegex = malloc(sizeof(regex_t));
            if (regcomp(requiredRegex, optarg, REG_EXTENDED | REG_NEWLINE | REG_ICASE)) {
                fprintf(stderr, "Error: Could not compile regex %s.\n", optarg);
                return 1;
            }
            break;
        case '?':
            if (optopt == 'u')
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
            return 1;
        default:
            abort();
    }
    if (force_color || xcode_colors) {
        set_colors(xcode_colors);
        printMessage = &write_colored;
        printSeparator = use_separators ? &color_separator : &no_separator;
    } else {
        printMessage = &write_fully;
        printSeparator = use_separators ? &plain_separator : &no_separator;
    }
    liveConnections = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
    am_device_notification *notification;
    AMDeviceNotificationSubscribe(DeviceNotificationCallback, 0, 0, NULL, &notification);
    CFRunLoopRun();
    return 0;
}
