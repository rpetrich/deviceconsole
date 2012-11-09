#include <stdio.h>
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

static int exitAfterTimeout;
static int linesLogged;

static inline void write_fully(int fd, const void *buffer, size_t length)
{
    while (length) {
        ssize_t result = write(fd, buffer, length);
        if (result == -1)
            break;
        buffer += result;
        length -= result;
    }
}

static void Timeout()
{
    static int logged = -1;

    if(exitAfterTimeout) {
        if(debug)
            fprintf(stderr, "[!] Exit due to timeout.\n");
        exit(0);
    }

    if(logged != linesLogged) {
        logged = linesLogged;
    } else {
        if(debug)
            fprintf(stderr, "[!] Exit after timeout (because nothing else has been logged.)\n");

        exit(0);
    }
}

static void SocketCallback(CFSocketRef s, CFSocketCallBackType type, CFDataRef address, const void *data, void *info)
{
    // Skip null bytes
    ssize_t length = CFDataGetLength(data);
    const unsigned char *buffer = CFDataGetBytePtr(data);
    while (length) {
        while (*buffer == '\0') {
            buffer++;
            length--;
            if (length == 0)
                goto exit;
        }
        size_t extentLength = 0;
        while ((buffer[extentLength] != '\0') && extentLength != length) {
            extentLength++;
        }
        write_fully(1, buffer, extentLength);
        length -= extentLength;
        buffer += extentLength;
    }

exit:
    /*
     * It turns out that every time we get here, we've logged one complete log
     * statement. (which could be more than one line, but you get the idea.)
     */
    linesLogged++;

    return;
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
                            CFSocketRef socket = CFSocketCreateWithNative(kCFAllocatorDefault,
                                                                          connection,
                                                                          kCFSocketDataCallBack,
                                                                          SocketCallback,
                                                                          NULL);
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

int main (int argc, char * const argv[])
{
    double timeout = 0;

    if ((argc == 2) && (strcmp(argv[1], "--help") == 0)) {
        fprintf(stderr,
                "Usage: %s [options]\nOptions:\n"
                " -d           Include connect/disconnect messages in standard out,\n"
                "              and exit reason in standard err.\n"
                " -u <udid>    Show only logs from a specific device.\n"
                " -t <timeout> Exit after the specified timeout, in seconds (can be decimal),\n"
                "              if there have been no more logs in that timeframe.\n"
                " -x           (Must use with -t.) Exit unconditionally after the timeout,\n" 
                "              even if more logs are coming in.\n"
                "\nControl-C to disconnect\n"
                "Mail bug reports and suggestions to <ryan.petrich@medialets.com>\n",
                argv[0]);
        return 1;
    }
    int c;
    while ((c = getopt(argc, argv, "dxt:u:")) != -1)
        switch (c)
    {
        case 'd':
            debug = 1;
            break;
        case 'u':
            if (requiredDeviceId)
                CFRelease(requiredDeviceId);
            requiredDeviceId = CFStringCreateWithCString(kCFAllocatorDefault, optarg, kCFStringEncodingASCII);
            break;
        case 't':
            timeout = atof(optarg);
            break;
        case 'x':
            exitAfterTimeout = 1;
            break;
        case '?':
            if (optopt == 'u' || optopt == 't')
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
            return 1;
        default:
            abort();
    }
    liveConnections = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
    am_device_notification *notification;
    AMDeviceNotificationSubscribe(DeviceNotificationCallback, 0, 0, NULL, &notification);

    if(timeout != 0) {
        CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL,
                                                       /* give it a couple extra seconds to connect */
                                                       CFAbsoluteTimeGetCurrent() + 2.0 +  timeout,
                                                       timeout,
                                                       0, /* flags */
                                                       0, /* order */
                                                       (CFRunLoopTimerCallBack) Timeout,
                                                       NULL);

        CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopCommonModes);
    }


    CFRunLoopRun();
    return 0;
}
