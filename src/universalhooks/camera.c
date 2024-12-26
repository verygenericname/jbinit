//
//  camera.c
//  plooshInit
//
//  Created by Nick Chan on 2024/12/26.
//

#include <substrate.h>
#include <CoreFoundation/CoreFoundation.h>

void NSLog(CFStringRef, ...);

CFTypeRef MGCopyAnswer(CFStringRef);
static CFTypeRef (*MGCopyAnswer_orig)(CFStringRef);

static CFTypeRef MGCopyAnswer_hook(CFStringRef k) {
    NSLog(CFSTR("MGCopyAnswer: %@"), k);
    /*if (CFStringCompare(k, CFSTR("ProductType"), 0) == 0) {
        return CFStringCreateWithCString(kCFAllocatorDefault, "iPad7,11", kCFStringEncodingUTF8);
    } else if (CFStringCompare(k, CFSTR("IsSimulator"), 0) == 0) {
        return kCFBooleanTrue;
    }*/
    return MGCopyAnswer_orig(k);
}

void cameraInit(void) {
    MSHookFunction(MGCopyAnswer, (void*)MGCopyAnswer_hook, (void**)&MGCopyAnswer_orig);
}
