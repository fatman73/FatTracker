// DISK OP. DELETE on macOS: the file goes to the Trash (the user's choice, 06.11.2026), not deleted for good.
#import <Foundation/Foundation.h>

bool macMoveToTrash(const char* utf8Path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:utf8Path] ?: @""];
        return [NSFileManager.defaultManager trashItemAtURL:url resultingItemURL:nil error:nil];
    }
}
