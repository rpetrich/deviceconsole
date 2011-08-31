#include <stdio.h>

#import <Foundation/Foundation.h>

@interface DTDKRemoteDeviceDataListener : NSObject
{
    void *deviceNotificationRef;
    NSThread *listenerThread;
    Class deviceDataClass;
}

+ (DTDKRemoteDeviceDataListener *)sharedInstance;
@property Class deviceDataClass; // @synthesize deviceDataClass;
@property(retain) NSThread *listenerThread; // @synthesize listenerThread;
- (id)deviceDataFromRecoveryModeDevice:(struct __AMRecoveryModeDevice *)arg1 withError:(id *)arg2;
- (id)deviceDataFromDFUDevice:(struct __AMDFUModeDevice *)arg1 withError:(id *)arg2;
- (id)deviceDataFromAMDevice:(struct _AMDevice *)arg1 withError:(id *)arg2;
- (void)recoveryModeDeviceDetached:(id)device;
- (void)DFUModeDeviceDetached:(id)device;
- (void)restoreDeviceDetached:(id)device;
- (void)deviceDetached:(id)device;
- (void)recoveryModeDeviceAttached:(id)device;
- (void)DFUModeDeviceAttached:(id)device;
- (void)restoreDeviceAttached:(id)device;
- (void)deviceAttached:(id)device;
- (void)_makeNoteOfDetachedDevice:(id)arg1;
- (void)_makeNoteOfAttachedDevice:(id)arg1;
- (id)deviceWithECID:(unsigned long long)arg1;
- (void)setDevice:(id)arg1 forECID:(unsigned long long)arg2;
- (id)deviceWithIdentifier:(id)arg1;
- (void)setDevice:(id)arg1 forIdentifier:(id)arg2;
- (id)deviceAtLocation:(unsigned int)arg1;
- (void)setDevice:(id)arg1 forLocation:(unsigned int)arg2;
- (void)presentError:(id)arg1;
- (void)listenerThreadImplementation;
- (void)stopListening;
- (void)startListening;
- (id)init;

@end

@implementation DTDKRemoteDeviceDataListener (Silencer)

// Suppress logging

- (void)recoveryModeDeviceDetached:(id)device
{
}

- (void)DFUModeDeviceDetached:(id)device
{
}

- (void)restoreDeviceDetached:(id)device
{
}

- (void)deviceDetached:(id)device
{
}

- (void)recoveryModeDeviceAttached:(id)device
{
}

- (void)DFUModeDeviceAttached:(id)device
{
}

- (void)restoreDeviceAttached:(id)device
{
}

- (void)deviceAttached:(id)device
{
}

@end


extern NSString *const DTDKConnectedDeviceDocumentNotification;
extern NSString *const DTDKConsoleDataReceivedConsoleStringKey;
extern NSString *const DTDKConsoleDataReceivedNotification;
extern NSString *const DTDKConsoleDataReceivedUDIDKey;

static void ConsoleDataCallback(CFNotificationCenterRef center, void *observer, CFStringRef name, const void *object, CFDictionaryRef userInfo)
{
    CFStringRef text = CFDictionaryGetValue(userInfo, DTDKConsoleDataReceivedConsoleStringKey);
    if (text) {
        fputs([(NSString *)text UTF8String], stdout);
    }
}

int main (int argc, const char * argv[])
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(), NULL, ConsoleDataCallback, (CFStringRef)DTDKConsoleDataReceivedNotification, NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
    [[DTDKRemoteDeviceDataListener sharedInstance] startListening];
    CFRunLoopRun();
    [pool drain];
    return 0;
}
