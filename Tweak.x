#import <Foundation/Foundation.h>
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#import <mach-o/fat.h>
#import <sys/sysctl.h>
#import <sys/stat.h>
#import <CommonCrypto/CommonCrypto.h>
#import <signal.h>
#import <unistd.h>
#import <fcntl.h>
#import <string.h>
#import <limits.h>
#import <mach/mach.h>
#import <libkern/OSCacheControl.h>
#include "include/patch_pin.h"

extern void MSHookMemory(void *target, const void *data, size_t size);

/* Placeholder pin — inject real CommCenter fingerprint privately before device use. */

/* Kill-switch: founder eng-run path or /tmp alternate (_wireless cannot write .eng-run). */
#define kDisablePathEngRun     "/var/mobile/.eng-run/sms-tweak-disable"
#define kDisablePathTmp        "/tmp/teslapad-sms-tweak-disable"
#define kStatusPath            "/tmp/teslapad-sms-tweak-status"
#define kCrashStatePath        "/tmp/teslapad-sms-tweak-crash-state"
#define kMaxCrashesSinceBoot   2

static volatile sig_atomic_t g_handling_fatal = 0;

static int read_boot_time_sec(time_t *out) {
    struct timeval boot;
    size_t len = sizeof(boot);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &boot, &len, NULL, 0) != 0)
        return -1;
    *out = boot.tv_sec;
    return 0;
}

static int crashes_since_boot(void) {
    time_t boot = 0;
    if (read_boot_time_sec(&boot) != 0)
        return 0;

    int fd = open(kCrashStatePath, O_RDONLY);
    if (fd < 0)
        return 0;

    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';

    long stored_boot = 0;
    int count = 0;
    if (sscanf(buf, "%ld %d", &stored_boot, &count) != 2)
        return 0;
    if ((time_t)stored_boot != boot)
        return 0;
    return count;
}

static void record_crash_since_boot(void) {
    time_t boot = 0;
    if (read_boot_time_sec(&boot) != 0)
        return;

    int count = crashes_since_boot() + 1;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%ld %d\n", (long)boot, count);
    if (n <= 0)
        return;

    int fd = open(kCrashStatePath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    write(fd, buf, (size_t)n);
    close(fd);
}

static void fatal_signal_handler(int sig) {
    if (g_handling_fatal)
        _exit(128 + sig);
    g_handling_fatal = 1;
    record_crash_since_boot();
    signal(sig, SIG_DFL);
    raise(sig);
}

static BOOL kill_switch_active(void) {
    struct stat st;
    if (lstat(kDisablePathEngRun, &st) == 0)
        return YES;
    if (lstat(kDisablePathTmp, &st) == 0)
        return YES;
    return crashes_since_boot() > kMaxCrashesSinceBoot;
}

static BOOL sha256_file(NSString *path, unsigned char digest[CC_SHA256_DIGEST_LENGTH]) {
    NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil];
    if (!data || data.length == 0)
        return NO;
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    return YES;
}

static BOOL sha256_matches(NSString *path) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    if (!sha256_file(path, digest))
        return NO;

    static const char hex[] = "0123456789abcdef";
    char text[CC_SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        text[i * 2]     = hex[(digest[i] >> 4) & 0xf];
        text[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    text[CC_SHA256_DIGEST_LENGTH * 2] = '\0';
    return (strcasecmp(text, PATCH_PIN_EXPECTED_SHA256) == 0);
}

static BOOL uuid_matches_main_executable(const struct mach_header_64 *mh) {
    if (!mh || mh->magic != MH_MAGIC_64)
        return NO;

    const uint8_t *cursor = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_UUID) {
            const struct uuid_command *uc = (const struct uuid_command *)lc;
            return patch_pin_uuid_matches(uc->uuid);
        }
        cursor += lc->cmdsize;
    }
    return NO;
}

static const struct mach_header_64 *pinned_commcenter_header(void) {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name || !strstr(name, "CommCenter"))
            continue;
        const struct mach_header_64 *hdr =
            (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (hdr && hdr->magic == MH_MAGIC_64 && uuid_matches_main_executable(hdr))
            return hdr;
    }
    return NULL;
}

static NSString *main_executable_path(void) {
    const struct mach_header_64 *mh = pinned_commcenter_header();
    if (!mh)
        return nil;

    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        return nil;
    return [NSString stringWithUTF8String:path];
}

static void write_status(const char *outcome) {
    if (!outcome || !sms_tweak_status_line_valid(outcome))
        return;

    NSISO8601DateFormatter *fmt = [[NSISO8601DateFormatter alloc] init];
    fmt.formatOptions = NSISO8601DateFormatWithInternetDateTime;
    NSString *at = [fmt stringFromDate:[NSDate date]];

    NSString *line = [NSString stringWithFormat:@"outcome=%s at=%@\n", outcome, at];
    NSData *data = [line dataUsingEncoding:NSUTF8StringEncoding];
    [data writeToFile:@kStatusPath atomically:YES];
}

static BOOL apply_patch(uintptr_t slide) {
    uintptr_t target = patch_pin_runtime_target(slide);
    volatile uint32_t *site = (volatile uint32_t *)target;

    uint32_t current = *site;
    if (current == PATCH_PIN_NOP_U32)
        return YES;
    if (current != PATCH_PIN_ORIG_U32)
        return NO;

    uint32_t nop = PATCH_PIN_NOP_U32;
    MSHookMemory((void *)site, &nop, sizeof(nop));
    sys_icache_invalidate((void *)site, sizeof(uint32_t));
    return (*site == PATCH_PIN_NOP_U32);
}

static void install_crash_counter_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fatal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

%ctor {
    @autoreleasepool {
        install_crash_counter_handlers();

        if (kill_switch_active()) {
            write_status(SMS_TWEAK_STATUS_SKIP_KILLSWITCH);
            return;
        }

        NSString *exe = main_executable_path();
        if (!exe) {
            write_status(SMS_TWEAK_STATUS_SKIP_PIN);
            return;
        }
        if (!sha256_matches(exe)) {
            write_status(SMS_TWEAK_STATUS_SKIP_SHA);
            return;
        }

        const struct mach_header_64 *mh = pinned_commcenter_header();
        if (!mh) {
            write_status(SMS_TWEAK_STATUS_SKIP_PIN);
            return;
        }

        uintptr_t slide = 0;
        for (uint32_t i = 0; i < _dyld_image_count(); i++) {
            if (_dyld_get_image_header(i) == (const struct mach_header *)mh) {
                slide = (uintptr_t)_dyld_get_image_vmaddr_slide(i);
                break;
            }
        }
        if (!apply_patch(slide)) {
            write_status(SMS_TWEAK_STATUS_PATCH_FAIL);
            return;
        }
        write_status(SMS_TWEAK_STATUS_PATCH_OK);
    }
}