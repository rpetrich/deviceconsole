#include <stdio.h>
#include <regex.h>
#include <unistd.h>
#include "main.h"

static Config_p config = NULL;
static Colors_p colors = NULL;

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
        if (buffer[i] == '\0')
            break;
        
        if (buffer[i] == ' ') {
            space_offsets_out[o++] = i;
            if (o == 3) {
                break;
            }
        }
    }
    return o;
}


/* ! Modifies buffer ! */
static void parse_process_params(char *buffer, ProcessParams_p process_params_out) {
    memset(process_params_out, 0, sizeof(ProcessParams));
    
    process_params_out->name = buffer;
    
    for (int i = strlen(buffer); i != 0; i--) {
        if (buffer[i] == ']')
            buffer[i] = '\0';
        
        if (buffer[i] == '[') {
            process_params_out->pid = buffer + i + 1;
            buffer[i] = '\0';
        }
        
        if (buffer[i] == ')')
            buffer[i] = '\0';
        
        if (buffer[i] == '(') {
            process_params_out->extension = buffer + i + 1;
            buffer[i] = '\0';
        }
    }
}

static unsigned char should_print_message(const char *buffer, size_t length)
{
    if (length < 3) return 0; // don't want blank lines
    
    unsigned char should_print = 1;
    
    size_t space_offsets[3];
    find_space_offsets(buffer, length, space_offsets);
    
    
    // Both process name/pid and extension name filters require process params parsing
    if (config->requiredProcessNames != NULL || config->requiredExtensionNames != NULL) {
        int nameLength = space_offsets[1] - space_offsets[0]; //This size includes the NULL terminator.
        
        char *allProcessParams = malloc(nameLength);
        allProcessParams[nameLength - 1] = '\0';
        
        memcpy(allProcessParams, buffer + space_offsets[0] + 1, nameLength - 1);
        
        ProcessParams processParams;
        parse_process_params(allProcessParams, &processParams);
        
        // Check whether process name matches the list passed to -p option and filter if needed
        if (config->requiredProcessNames != NULL) {
            char *currentProcessName = strtok(strdup(config->requiredProcessNames), ", ");
            while (currentProcessName != NULL) {
                bool isPID = (processParams.pid != NULL);
                for (int i=0; i < strlen(currentProcessName); i++) {
                    if (isnumber(currentProcessName[i]) == 0) {
                        isPID = false;
                        break;
                    }
                }
                
                should_print = (strcmp((isPID) ? processParams.pid : processParams.name, currentProcessName) == 0);
                
                if (should_print)
                    break;
                
                currentProcessName = strtok(NULL, ", ");
            }
            
            if (!should_print)
                return should_print;
        }
        
        
        // Check whether extension name matches the list passed to -e option and filter if needed
        if (config->requiredExtensionNames != NULL) {
            if (processParams.extension == NULL)
                return false;
            
            char *currentExtensionName = strtok(strdup(config->requiredExtensionNames), ", ");
            while (currentExtensionName != NULL) {
                should_print = (strcmp(processParams.extension, currentExtensionName) == 0);
                
                if (should_print)
                    break;
                
                currentExtensionName = strtok(NULL, ", ");
            }
            
            if (!should_print)
                return should_print;
        }
        
        free(allProcessParams);
    }
    
    // Check whether buffer matches the regex passed to -r option and filter if needed
    if (config->use_regex) {
        char message[length + 1];
        
        memcpy(message, buffer, length);
        message[length + 1] = '\0';
        
        if (regexec(&config->requiredRegex, message, 0, NULL, 0) == REG_NOMATCH)
            should_print = false;
    }

    
    // More filtering options can be added here and return 0 when they won't meet filter criteria
    
    return should_print;
}

#define write_const(fd, text) write_fully(fd, text, strlen(text))
#define stringify(x) #x
#define xcode_color_with_rgb(type, r, g, b) "\e[" type stringify(r) "," stringify(g) "," stringify(b) ";"

static void set_colors(bool xcode_colors) {
    colors->reset         = (xcode_colors) ? "\e[;"                                       :   "\e[m";
    colors->normal        = (xcode_colors) ? xcode_color_with_rgb("fg", 0, 0, 0)          :   "\e[0m";
    colors->dark          = (xcode_colors) ? xcode_color_with_rgb("fg", 102, 102, 102)    :   "\e[2m";
    colors->red           = (xcode_colors) ? xcode_color_with_rgb("fg", 151, 4, 12)       :   "\e[0;31m";
    colors->dark_red      = (xcode_colors) ? xcode_color_with_rgb("fg", 227, 10, 23)       :   "\e[2;31m";
    colors->green         = (xcode_colors) ? xcode_color_with_rgb("fg", 23, 164, 26)       :   "\e[0;32m";
    colors->dark_green    = (xcode_colors) ? xcode_color_with_rgb("fg", 33, 215, 38)        :   "\e[2;32m";
    colors->yellow        = (xcode_colors) ? xcode_color_with_rgb("fg", 153, 152, 29)      :   "\e[0;33m";
    colors->dark_yellow   = (xcode_colors) ? xcode_color_with_rgb("fg", 229, 228, 49)      :   "\e[2;33m";
    colors->blue          = (xcode_colors) ? xcode_color_with_rgb("fg", 5, 22, 175)        :   "\e[0;34m";
    colors->dark_blue     = (xcode_colors) ? xcode_color_with_rgb("fg", 11, 36, 251)        :   "\e[2;34m";
    colors->magenta       = (xcode_colors) ? xcode_color_with_rgb("fg", 177, 25, 176)    :   "\e[0;35m";
    colors->dark_magenta  = (xcode_colors) ? xcode_color_with_rgb("fg", 227, 25, 227)    :   "\e[2;35m";
    colors->cyan          = (xcode_colors) ? xcode_color_with_rgb("fg", 26, 166, 177)     :   "\e[0;36m";
    colors->dark_cyan     = (xcode_colors) ? xcode_color_with_rgb("fg", 39, 229, 228)     :   "\e[2;36m";
    colors->white         = (xcode_colors) ? xcode_color_with_rgb("fg", 191, 191, 191)    :   "\e[0;37m";
    colors->dark_white    = (xcode_colors) ? xcode_color_with_rgb("fg", 230, 229, 230)    :   "\e[0;37m";
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
        write_const(fd, colors->dark_white);
        write_fully(fd, buffer, space_offsets[0]);
        // Log process name, extension name and pid
        int pos = 0;
        for (int i = space_offsets[0]; i < space_offsets[1]; i++) {
            if (buffer[i] == '(') {
                pos = i;
                break;
            }
            
            if (buffer[i] == '[') {
                pos = i;
                break;
            }
        }
        write_const(fd, colors->cyan);
        if (pos) {
            // Process name
            write_fully(fd, buffer + space_offsets[0], pos - space_offsets[0]);
            
            if (buffer[pos] == '(') {
                // Find '[' too
                
                int new_pos = 0;
                for (int i = space_offsets[0]; i < space_offsets[1]; i++) {
                    if (buffer[i] == '[') {
                        new_pos = i;
                        break;
                    }
                }
                
                if (new_pos) {
                    // Process extension name
                    write_const(fd, colors->dark_cyan);
                    write_fully(fd, buffer + pos, 1);
                    write_const(fd, colors->cyan);
                    write_fully(fd, buffer + pos + 1, new_pos - pos - 2);
                    write_const(fd, colors->dark_cyan);
                    write_fully(fd, buffer + new_pos - 1, 1);
                    
                    // PID
                    write_const(fd, colors->dark_cyan);
                    write_fully(fd, buffer + new_pos, 1);
                    write_const(fd, colors->cyan);
                    write_fully(fd, buffer + new_pos + 1, space_offsets[1] - new_pos - 2);
                    write_const(fd, colors->dark_cyan);
                    write_fully(fd, buffer + space_offsets[1] - 1, 1);
                } else {
                    write_fully(fd, buffer + pos, space_offsets[1] - pos);
                }
                
            } else {
                // PID
                write_const(fd, colors->dark_cyan);
                write_fully(fd, buffer + pos, 1);
                write_const(fd, colors->cyan);
                write_fully(fd, buffer + pos + 1, space_offsets[1] - pos - 2);
                write_const(fd, colors->dark_cyan);
                write_fully(fd, buffer + space_offsets[1] - 1, 1);
            }
        } else {
            write_fully(fd, buffer + space_offsets[0], space_offsets[1] - space_offsets[0]);
        }
        // Log level
        size_t levelLength = space_offsets[2] - space_offsets[1];
        if (levelLength > 4) {
            const char *normalColor;
            const char *darkColor;
            if (levelLength == 9 && memcmp(buffer + space_offsets[1], " <Debug>:", 9) == 0){
                normalColor = colors->magenta;
                darkColor = colors->dark_magenta;
            } else if (levelLength == 11 && memcmp(buffer + space_offsets[1], " <Warning>:", 11) == 0){
                normalColor = colors->yellow;
                darkColor = colors->dark_yellow;
            } else if (levelLength == 9 && memcmp(buffer + space_offsets[1], " <Error>:", 9) == 0){
                normalColor = colors->red;
                darkColor = colors->dark_red;
            } else if (levelLength == 10 && memcmp(buffer + space_offsets[1], " <Notice>:", 10) == 0) {
                normalColor = colors->green;
                darkColor = colors->dark_green;
            } else {
                goto level_unformatted;
            }
            write_string(fd, darkColor);
            write_fully(fd, buffer + space_offsets[1], 2);
            write_string(fd, normalColor);
            write_fully(fd, buffer + space_offsets[1] + 2, levelLength - 4);
            write_string(fd, darkColor);
            write_fully(fd, buffer + space_offsets[1] + levelLength - 2, 1);
            write_const(fd, colors->dark_white);
            write_fully(fd, buffer + space_offsets[1] + levelLength - 1, 1);
        } else {
        level_unformatted:
            write_const(fd, colors->reset);
            write_fully(fd, buffer + space_offsets[1], levelLength);
        }
        write_const(fd, colors->reset);
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

        static unsigned char should_print_result = false;
        bool should_filter = false;
        
        if (    strnstr(buffer, "<Notice>:", 150)   || strnstr(buffer, "<Info>:", 150)
            ||  strnstr(buffer, "<Error>:", 150)    || strnstr(buffer, "<Warning>:", 150)
            ||  strnstr(buffer, "<Warning>:", 150)  || strnstr(buffer, "<Debug>:", 150))
        {
            should_filter = true;
        }
        
        if (should_filter)
            should_print_result = should_print_message(buffer, extentLength);
        
        if (should_print_result) {
            if (should_filter) {
                static bool is_first_message = true;
                if (!is_first_message)
                    config->printSeparator(1);
                else
                    is_first_message = false;
                
                config->printMessage(1, buffer, extentLength);
            }
            else
                write_fully(1, buffer, extentLength);
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
            if (config->debug) {
                CFStringRef deviceId = AMDeviceCopyDeviceIdentifier(device);
                CFStringRef str = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("deviceconsole connected: %@"), deviceId);
                CFRelease(deviceId);
                CFShow(str);
                CFRelease(str);
            }
            if (config->requiredDeviceId) {
                CFStringRef deviceId = AMDeviceCopyDeviceIdentifier(device);
                Boolean isRequiredDevice = CFEqual(deviceId, config->requiredDeviceId);
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
                                    CFDictionarySetValue(config->liveConnections, device, data);
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
            if (config->debug) {
                CFStringRef deviceId = AMDeviceCopyDeviceIdentifier(device);
                CFStringRef str = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("deviceconsole disconnected: %@"), deviceId);
                CFRelease(deviceId);
                CFShow(str);
                CFRelease(str);
            }
            DeviceConsoleConnection *data = (DeviceConsoleConnection *)CFDictionaryGetValue(config->liveConnections, device);
            if (data) {
                CFDictionaryRemoveValue(config->liveConnections, device);
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
    size_t buffer_lentgh = sizeof(char) * (strlen(colors->dark_white) + strlen(colors->reset) + 3);
    char *buffer = malloc(buffer_lentgh);
    sprintf(buffer, "%s--%s\n", colors->dark_white, colors->reset);
    
    write_const(fd, buffer);
    free(buffer);
}

static void print_help(char *process_name) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "Options:\n"
            " -d\t\t\t\tInclude connect/disconnect messages in standard out"
            "\n"
            " -u <udid>\t\t\tShow only logs from a specific device"
            "\n"
            "-p <\"process name, pid\">\tShow only logs from a specific process name or pid"
            "\n"
            "-e <\"kext name, dylib name\">\tShow only logs from a specific process extension (kext/dylib) - *iOS 10 only*"
            "\n"
            "-r <regular expression>\tFilter messages by regular expression."
            "\n"
            "-x\t\t\t\tDisable tty coloring in Xcode (unless XcodeColors intalled)."
            "\n"
            "\n"
            "Control-C to disconnect"
            "\n"
            "Mail bug reports and suggestions to <ryan.petrich@medialets.com> (or <dany931@gmail.com>)"
            "\n"
            , process_name);
}

int main (int argc, char * const argv[])
{
    if ((argc == 2) && ((strcmp(argv[1], "--help") == 0) || (strcmp(argv[1], "-h") == 0))) {
        print_help(argv[0]);
        return 1;
    }
    
    config = malloc(sizeof(Config));
    memset(config, 0, sizeof(Config));
    
    int c;
    bool use_separators = false;
    bool force_color = isatty(1);
    bool xcode_colors = ((getenv("XcodeColors")) ? strstr(getenv("XcodeColors"), "YES") != NULL : false);
    bool in_xcode = xcode_colors;

    while ((c = getopt(argc, argv, "dcxsu:p:r:e:")) != -1)
        switch (c)
    {
        case 'd':
            config->debug = 1;
            break;
        case 'c':
            force_color = true;
            break;
        case 'x':
            in_xcode = true;
            break;
        case 's':
            use_separators = true;
            break;
        case 'u':
            if (config->requiredDeviceId)
                CFRelease(config->requiredDeviceId);
            config->requiredDeviceId = CFStringCreateWithCString(kCFAllocatorDefault, optarg, kCFStringEncodingASCII);
            break;
        case 'p':
            config->requiredProcessNames = malloc(strlen(optarg) + 1);
            config->requiredProcessNames[strlen(optarg)] = '\0';

            strcpy(config->requiredProcessNames, optarg);
            break;
        case 'e':
            config->requiredExtensionNames = malloc(strlen(optarg) + 1);
            config->requiredExtensionNames[strlen(optarg)] = '\0';
            
            strcpy(config->requiredExtensionNames, optarg);
            break;
        case 'r':
            config->use_regex = true;
            if (regcomp(&config->requiredRegex, optarg, REG_EXTENDED | REG_NEWLINE | REG_ICASE)) {
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
    if ((!in_xcode && force_color) || xcode_colors) {
        colors = malloc(sizeof(Colors));
        set_colors(xcode_colors);
        
        config->printMessage = &write_colored;
        config->printSeparator = use_separators ? &color_separator : &no_separator;
    } else {
        config->printMessage = &write_fully;
        config->printSeparator = use_separators ? &plain_separator : &no_separator;
    }
    config->liveConnections = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
    am_device_notification *notification;
    AMDeviceNotificationSubscribe(DeviceNotificationCallback, 0, 0, NULL, &notification);
    CFRunLoopRun();
    
    if (config->requiredProcessNames != NULL)
        free(config->requiredProcessNames);
    
    if (config->requiredExtensionNames != NULL)
        free(config->requiredExtensionNames);
    
    if (config->use_regex)
        regfree(&config->requiredRegex);
    
    if (config != NULL)
        free(config);
    
    if (colors != NULL)
        free(colors);
    
    return 0;
}
