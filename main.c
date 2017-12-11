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
static bool force_color = false;
static bool message_only = false;
static char *simulatorLogPath;
static CFStringRef requiredDeviceId;
static char *requiredProcessName;
static char includeOccurrences[256];
static char excludeOccurrences[256];
static void (*printMessage)(int fd, const char *, size_t);
static void (*printSeparator)(int fd);
static int case_insensitive;

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
    if(string != NULL){
        write_fully(fd, string, strlen(string));
    }
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

static bool find_in_string(const char *buffer, const char *pattern, bool case_ins) {
    bool found = false;
    if (case_ins) {
        found = strcasestr(buffer, pattern);
    } else {
        found = strstr(buffer, pattern);
    }

    return found;
}

static unsigned char should_print_message(const char *buffer, size_t length)
{
    if (length < 3)
        return 0; // don't want blank lines

    size_t space_offsets[3];
    find_space_offsets(buffer, length, space_offsets);

    // Check whether process name matches the one passed to -p option and filter if needed
    if (requiredProcessName != NULL) {
        int nameLength = space_offsets[1] - space_offsets[0]; //This size includes the NULL terminator.

        char *processName = malloc(nameLength);
        processName[nameLength - 1] = '\0';
        memcpy(processName, buffer + space_offsets[0] + 1, nameLength - 1);

        for (int i = strlen(processName); i != 0; i--)
            // the full process name looks like 'kernel(AppleBiometricSensor)[0]' in iOS 10
            // or 'kernel[0]' in iOS below 10
            // -> strip everything behind the first '(' or '['
            if (processName[i] == '[' || processName[i] == '(')
                processName[i] = '\0';

        if (!find_in_string(processName, requiredProcessName, case_insensitive)){
            free(processName);
            return 0;
        }
        free(processName);
    }

    // More filtering options can be added here and return 0 when they won't meed filter criteria
    if (strlen(includeOccurrences) && !find_in_string(buffer, includeOccurrences, case_insensitive))
    {
        return 0;
    }

    if (strlen(excludeOccurrences) && find_in_string(buffer, excludeOccurrences, case_insensitive))
    {
        return 0;
    }
    return 1;
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
    int o = find_space_offsets(buffer, length, space_offsets);

    if (o == 3) {

        if (!message_only) {
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
            write_const(fd, " ");
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
            } else if (levelLength == 8 && memcmp(buffer + space_offsets[1], " <Info>:", 8) == 0) {
                normalColor = COLOR_BLUE;
                darkColor = COLOR_DARK_BLUE;
            } else {
                goto level_unformatted;
            }
            write_string(fd, darkColor);
            write_fully(fd, buffer + space_offsets[1] + 1, 1);
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
        CFStringRef logMessage = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault, memcpy(buffer, buffer + space_offsets[2], length), kCFStringEncodingMacRoman, kCFAllocatorNull);
        CFMutableStringRef mutableLogMessage = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, logMessage);
        CFRelease(logMessage);
        CFStringFindAndReplace(mutableLogMessage, CFSTR("\\^["), CFSTR("\e"), CFRangeMake(0, CFStringGetLength(mutableLogMessage)), 0);
        const char *coloredMessage = CFStringGetCStringPtr( mutableLogMessage, kCFStringEncodingMacRoman );

        if (coloredMessage != NULL) {
            write_string(fd, coloredMessage);
        }else{
            write_string(fd, buffer);
        }
        CFRelease(mutableLogMessage);
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
    write_const(fd, COLOR_DARK_WHITE "--" COLOR_RESET "\n");
}

void simulator_write_callback(char *p, size_t size){
    char buffer[size];
    bzero(buffer, sizeof(buffer));
    memcpy(&buffer, p, size);


    if (should_print_message(buffer, size)) {
        printMessage(1, buffer, size);
        printSeparator(1);
    }
}

static void log_simulator(){
    FILE *fp = fopen(simulatorLogPath, "r");

    if(fp == NULL){
        fprintf(stderr, "Error: Could not open simulator log: %s", simulatorLogPath);
        return;
    }

    log_tail(fp);
    fclose(fp);
}

int main (int argc, char * const argv[])
{

  int c;

  static struct option long_options[] =
  {
      {"case_insensitive", no_argument, (int*)&case_insensitive, 'i'},
      {"filter", required_argument, NULL, 'f'},
      {"exclude", required_argument, NULL, 'x'},
      {"process", required_argument, NULL, 'p'},
      {"udid", required_argument, NULL, 'u'},
      {"simulator", required_argument, NULL, 's'},
      {"help", no_argument, NULL, 'h'},
      {"debug", no_argument, (int*)&debug, 1},
      {"use-separators", no_argument, (int*)&use_separators, 1},
      {"force-color", no_argument, (int*)&force_color, 1},
      {"message-only", no_argument, (int*)&message_only, 1},
      {NULL, 0, NULL, 0}
  };

  int option_index = 0;

  while((c = getopt_long(argc, argv, "iu:s:p:f:x:", long_options, &option_index)) != -1){
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
        case 'i':
            case_insensitive = 1;
            break;
        case 'p':
            requiredProcessName = malloc(strlen(optarg) + 1);
            requiredProcessName[strlen(optarg)] = '\0';

            strcpy(requiredProcessName, optarg);
            break;
        case 'f':
            strcpy(includeOccurrences, optarg);
            break;
        case 'x':
            strcpy(excludeOccurrences, optarg);
            break;
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

    if(simulatorLogPath && debug){
        printf("Warning: ignoring --debug flag due to --simulator.\n");
    }

    if (force_color || isatty(1)) {
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
    fprintf(stderr, "Usage: %s [options]\nOptions:\n-i | --case-insensitive     Make filters case-insensitive\n-f | --filter <string>      Filter include by single word occurrences (case-sensitive)\n-x | --exclude <string>     Filter exclude by single word occurrences (case-sensitive)\n-p | --process <string>     Filter by process name (case-sensitive)\n-u | --udid <udid>          Show only logs from a specific device\n-s | --simulator <version>  Show logs from iOS Simulator\n     --debug                Include connect/disconnect messages in standard out\n     --use-separators       Skip a line between each line\n     --force-color          Force colored text\n     --message-only          Display only level and message\nControl-C to disconnect\n", argv[0]);
    return 1;
}
