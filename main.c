#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <pwd.h>
#include <CoreFoundation/CoreFoundation.h>
#include "MobileDevice.h"
#include "extern.h"

typedef struct {
    service_conn_t connection;
    CFSocketRef socket;
    CFRunLoopSourceRef source;
} DeviceConsoleConnection;

static CFMutableDictionaryRef liveConnections;
static int debug;
static bool use_separators;
static char *simulatorLogPath;
static CFStringRef requiredDeviceId;
static void (*printMessage)(int fd, const char *, size_t);
static void (*printSeparator)(int fd);

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

#define write_const(fd, text) write_fully(fd, text, sizeof(text)-1)

#define COLOR_RESET         "\e[m"
#define COLOR_NORMAL        "\e[0m"
#define COLOR_DARK          "\e[2m"
#define COLOR_RED           "\e[0;31m"
#define COLOR_DARK_RED      "\e[2;31m"
#define COLOR_GREEN         "\e[0;32m"
#define COLOR_DARK_GREEN    "\e[2;32m"
#define COLOR_YELLOW        "\e[0;33m"
#define COLOR_DARK_YELLOW   "\e[2;33m"
#define COLOR_BLUE          "\e[0;34m"
#define COLOR_DARK_BLUE     "\e[2;34m"
#define COLOR_MAGENTA       "\e[0;35m"
#define COLOR_DARK_MAGENTA  "\e[2;35m"
#define COLOR_CYAN          "\e[0;36m"
#define COLOR_DARK_CYAN     "\e[2;36m"
#define COLOR_WHITE         "\e[0;37m"
#define COLOR_DARK_WHITE    "\e[0;37m"

static void write_colored(int fd, const char *buffer, size_t length)
{
    if (length < 16) {
        write_fully(fd, buffer, length);
        return;
    }
    size_t space_offsets[3];
    int o = 0;
    for (size_t i = 16; i < length; i++) {
        if (buffer[i] == ' ') {
            space_offsets[o++] = i;
            if (o == 3) {
                break;
            }
        }
    }
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
        printMessage(1, buffer, extentLength);
        printSeparator(1);
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
    write_const(fd, COLOR_DARK_WHITE "--" COLOR_RESET "\n");
}

void local_write_callback(char *p, size_t size){
    char buffer[size];
    bzero(buffer, sizeof(buffer));
    memcpy(&buffer, p, size);
    
    printMessage(1, buffer, size);
    
}

static void log_simulator(){
    FILE *fp = fopen(simulatorLogPath, "r");
    
    if(fp == NULL){
        fprintf(stderr, "Error: Could not open simulator log: %s", simulatorLogPath);
        return;
    }
    
    struct stat sb;
    
    rflag = 0;
    fflag = 1;
    
    enum STYLE style = RLINES;
    forward(fp, style, 0, &sb);
    
    fclose(fp);
}

int main (int argc, char * const argv[])
{
    int c;
    
    static struct option long_options[] =
    {
        {"udid", required_argument, NULL, 'u'},
        {"simulator", required_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {"debug", no_argument, (int*)&debug, 1},
        {"use-separators", no_argument, (int*)&use_separators, 1},
        {NULL, 0, NULL, 0}
    };
    
    int option_index = 0;
    
    while((c = getopt_long(argc, argv, "u:s:", long_options, &option_index)) != -1){
        switch (c){
            case 0:
                break;
            case 'u':
                if(requiredDeviceId)
                        CFRelease(requiredDeviceId);
                requiredDeviceId = CFStringCreateWithCString(kCFAllocatorDefault, optarg, kCFStringEncodingASCII);
                break;
            case 's':
            {
                int pathLength = strlen(optarg) + strlen(getpwuid(getuid())->pw_dir) + strlen("/Library/Logs/iOS Simulator//system.log");
                simulatorLogPath = malloc(pathLength + 1);/* Don't forget null terminator! */
                sprintf(simulatorLogPath, "%s/Library/Logs/iOS Simulator/%s/system.log", getpwuid(getuid())->pw_dir, optarg);
                
                if(access(simulatorLogPath, F_OK) == -1){
                    fprintf(stderr, "Error: Log for iOS Simulator version %s not found.\n", optarg);
                    return 1;
                }
                break;
            }
            case 'h':
            case '?':
                goto usage;
                break;
            default:
                abort();
        }
    }
    
    if(requiredDeviceId && simulatorLogPath){
        fprintf(stderr, "Error: --simulator and --udid cannot be used simultaneously.\n");
        return 1;
    }
    
    if (isatty(1)) {
        printMessage = &write_colored;
        printSeparator = use_separators ? &color_separator : &no_separator;
    } else {
        printMessage = &write_fully;
        printSeparator = use_separators ? &plain_separator : &no_separator;
    }
    
    if(simulatorLogPath){
        log_simulator();
        return 1;
    }else{
        liveConnections = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
        am_device_notification *notification;
        AMDeviceNotificationSubscribe(DeviceNotificationCallback, 0, 0, NULL, &notification);
    }
    CFRunLoopRun();
    return 0;
    
usage:
    fprintf(stderr, "Usage: %s [options]\nOptions:\n --udid <udid>          Show only logs from a specific device\n --simulator <version>  Show logs from iOS Simulator\n --debug                Include connect/disconnect messages in standard out\n --use-separators       Skip a line between each line.\n\nControl-C to disconnect\nMail bug reports and suggestions to <ryan.petrich@medialets.com>\n", argv[0]);
    return 1;
}
