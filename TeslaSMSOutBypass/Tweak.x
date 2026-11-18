#import <Foundation/Foundation.h>
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#import <sys/sysctl.h>
#import <sys/stat.h>
#import <errno.h>
#import <CommonCrypto/CommonCrypto.h>
#import <signal.h>
#import <unistd.h>
#import <fcntl.h>
#import <string.h>
#import <limits.h>
#import <mach/mach.h>
#import <libkern/OSCacheControl.h>
#import <dispatch/dispatch.h>
#import <os/log.h>
#include "include/out_sms_experiment.h"
#include "include/out_sms_gate_resolve.h"

extern void MSHookMemory(void *target, const void *data, size_t size);

static volatile sig_atomic_t g_handling_fatal = 0;
static uintptr_t g_policy_branch_site = 0;
static uint32_t g_policy_branch_orig = 0;
static BOOL g_branch_resolved = NO;
static BOOL g_patch_applied = NO;
static dispatch_source_t g_sync_timer = NULL;
static BOOL g_last_want_override = NO;
static BOOL g_last_ks = NO;
static BOOL g_last_armed = NO;
static BOOL g_validate_only = NO;
static os_log_t g_out_oslog;

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

    int fd = open(OUT_SMS_CRASH_STATE_PATH, O_RDONLY);
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

    int fd = open(OUT_SMS_CRASH_STATE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
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

static void install_crash_counter_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fatal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

static void diag_ensure_log_file(void) {
    struct stat st;
    if (lstat(OUT_SMS_DIAG_PATH, &st) == 0) {
        if (access(OUT_SMS_DIAG_PATH, W_OK) != 0) {
            if (unlink(OUT_SMS_DIAG_PATH) != 0 && g_out_oslog)
                os_log(g_out_oslog, "diag unlink failed errno=%d path=%{public}s", errno, OUT_SMS_DIAG_PATH);
        }
    }
    int fd = open(OUT_SMS_DIAG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        fchmod(fd, 0666);
        close(fd);
    }
}

static void diag_emit(NSString *event, NSDictionary *fields) {
    diag_ensure_log_file();

    NSISO8601DateFormatter *fmt = [[NSISO8601DateFormatter alloc] init];
    fmt.formatOptions = NSISO8601DateFormatWithInternetDateTime;
    NSString *ts = [fmt stringFromDate:[NSDate date]];
    NSMutableDictionary *row = [NSMutableDictionary dictionaryWithDictionary:fields ?: @{}];
    row[@"event"] = event;
    row[@"ts"] = ts;
    row[@"euid"] = @(geteuid());
    row[@"pid"] = @(getpid());
    row[@"process"] = [[NSProcessInfo processInfo] processName] ?: @"";

    NSString *jsonLine = [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:row options:0 error:NULL]
                                               encoding:NSUTF8StringEncoding];
    if (jsonLine) {
        if (g_out_oslog)
            os_log(g_out_oslog, "%{public}@", jsonLine);
        int fd = open(OUT_SMS_DIAG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            NSString *line = [jsonLine stringByAppendingString:@"\n"];
            NSData *data = [line dataUsingEncoding:NSUTF8StringEncoding];
            if (data)
                write(fd, data.bytes, data.length);
            close(fd);
        }
    }
}

static int path_stat_errno(const char *path, BOOL *exists) {
    struct stat st;
    int rc = lstat(path, &st);
    if (rc == 0) {
        *exists = YES;
        return 0;
    }
    *exists = NO;
    return errno;
}

static BOOL kill_switch_active(BOOL *out_ks) {
    BOOL ks = NO;
    BOOL dummy = NO;
    if (path_stat_errno(OUT_SMS_DISABLE_PATH_PRIMARY, &dummy) == 0 && dummy)
        ks = YES;
    if (path_stat_errno(OUT_SMS_INBOUND_DISABLE_PATH, &dummy) == 0 && dummy)
        ks = YES;
    if (crashes_since_boot() > OUT_SMS_MAX_CRASHES)
        ks = YES;
    if (out_ks)
        *out_ks = ks;
    return ks;
}

static BOOL experiment_armed(int *arm_errno, BOOL *arm_present) {
    BOOL present = NO;
    int en = path_stat_errno(OUT_SMS_ARM_PATH_PRIMARY, &present);
    if (arm_errno)
        *arm_errno = en;
    if (arm_present)
        *arm_present = present;
    return present && en == 0;
}

static BOOL validate_only_active(void) {
    BOOL present = NO;
    return path_stat_errno(OUT_SMS_VALIDATE_PATH_PRIMARY, &present) == 0 && present;
}

static BOOL file_size_matches(NSString *path) {
    struct stat st;
    if (!path || stat(path.fileSystemRepresentation, &st) != 0)
        return NO;
    return (uint64_t)st.st_size == (uint64_t)OUT_SMS_EXPECTED_COMM_SIZE;
}

static BOOL uuid_matches_main_executable(const struct mach_header_64 *mh) {
    if (!mh || mh->magic != MH_MAGIC_64)
        return NO;

    const uint8_t *cursor = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_UUID) {
            const struct uuid_command *uc = (const struct uuid_command *)lc;
            return memcmp(uc->uuid, PATCH_PIN_EXPECTED_UUID, 16) == 0;
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
        const struct mach_header_64 *hdr = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (hdr && hdr->magic == MH_MAGIC_64 && uuid_matches_main_executable(hdr))
            return hdr;
    }
    return NULL;
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

static NSString *main_executable_path(void) {
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        return nil;
    return [NSString stringWithUTF8String:path];
}

static void write_status_reason(const char *outcome, const char *reason) {
    if (!outcome || !out_sms_status_line_valid(outcome))
        return;

    NSISO8601DateFormatter *fmt = [[NSISO8601DateFormatter alloc] init];
    fmt.formatOptions = NSISO8601DateFormatWithInternetDateTime;
    NSString *at = [fmt stringFromDate:[NSDate date]];
    NSString *line;
    if (reason && reason[0])
        line = [NSString stringWithFormat:@"outcome=%s reason=%s at=%@\n", outcome, reason, at];
    else
        line = [NSString stringWithFormat:@"outcome=%s at=%@\n", outcome, at];
    NSData *data = [line dataUsingEncoding:NSUTF8StringEncoding];
    [data writeToFile:@OUT_SMS_STATUS_PATH atomically:YES];
}

static NSDictionary *gate_result_dict(const out_sms_gate_result_t *gr, NSString *decision) {
    if (!gr)
        return @{};
    return @{
        @"mode" : @"re_gate_resolve_v1",
        @"decision" : decision ?: @"",
        @"fail_stage" : @(out_sms_gate_stage_name(gr->stage)),
        @"str_va" : @((unsigned long long)gr->str_va),
        @"call_site_adrp" : @((unsigned long long)gr->call_site_adrp),
        @"block_head" : @((unsigned long long)gr->block_head),
        @"gate_va" : @((unsigned long long)gr->gate_va),
        @"gate_insn" : @(gr->gate_insn),
        @"gate_insn_expected" : @(OUT_SMS_PATCH_ORIG_U32),
        @"fn_start" : @((unsigned long long)gr->fn_start),
        @"fn_end" : @((unsigned long long)gr->fn_end),
        @"xref_ref_count" : @(gr->ref_count),
        @"branch_to_lblk_count" : @(gr->branch_to_lblk_count),
        @"inbound_mt_site" : @((unsigned long long)(OUT_SMS_INBOUND_PATCH_VADDR)),
    };
}

static BOOL write_insn_runtime(uintptr_t site, uint32_t insn) {
    volatile uint32_t *word = (volatile uint32_t *)site;
    uint32_t before = *word;
    MSHookMemory((void *)word, &insn, sizeof(insn));
    sys_icache_invalidate((void *)word, sizeof(uint32_t));
    BOOL ok = (*word == insn);
    if (!ok) {
        diag_emit(@"hook_install_failed", @{
            @"site" : @((unsigned long long)site),
            @"wanted" : @(insn),
            @"got" : @(*word),
            @"before" : @(before)
        });
    }
    return ok;
}

static out_sms_gate_stage_t resolve_policy_branch(intptr_t slide, const struct mach_header_64 *mh, NSString *exe_path,
                                                uintptr_t *out_site, uint32_t *out_orig, NSDictionary **diag_out) {
    BOOL uuid_ok = uuid_matches_main_executable(mh);
    BOOL sha_ok = exe_path ? sha256_matches(exe_path) : NO;
    uint64_t fsize = OUT_SMS_EXPECTED_COMM_SIZE;
    if (exe_path) {
        struct stat st;
        if (stat(exe_path.fileSystemRepresentation, &st) == 0)
            fsize = (uint64_t)st.st_size;
    }

    out_sms_gate_result_t gr;
    out_sms_gate_stage_t st = out_sms_resolve_gate(mh, slide, uuid_ok, sha_ok, fsize, PATCH_PIN_EXPECTED_UUID, &gr);
    if (st != OUT_SMS_GATE_OK) {
        *diag_out = gate_result_dict(&gr, @(out_sms_gate_stage_name(st)));
        return st;
    }

    uintptr_t gate_rt = (uintptr_t)(slide + (intptr_t)(gr.gate_va - PATCH_PIN_IMAGE_BASE));
    uint32_t live = *(volatile uint32_t *)gate_rt;
    if (live == OUT_SMS_PATCH_NOP_U32) {
        gr.stage = OUT_SMS_GATE_PATCH_TARGET_CONFLICT;
        gr.gate_insn = live;
        *diag_out = gate_result_dict(&gr, @"PATCH_TARGET_CONFLICT");
        return OUT_SMS_GATE_PATCH_TARGET_CONFLICT;
    }
    if (live != OUT_SMS_PATCH_ORIG_U32) {
        gr.stage = OUT_SMS_GATE_PATCH_BYTES_MISMATCH;
        gr.gate_insn = live;
        *diag_out = gate_result_dict(&gr, @"PATCH_BYTES_MISMATCH");
        return OUT_SMS_GATE_PATCH_BYTES_MISMATCH;
    }

    *out_site = gate_rt;
    *out_orig = (live == OUT_SMS_PATCH_NOP_U32) ? OUT_SMS_PATCH_ORIG_U32 : live;
    *diag_out = gate_result_dict(&gr, @OUT_SMS_REASON_TARGET_VALIDATED_UP);
    return OUT_SMS_GATE_OK;
}

static void sync_policy_override(void) {
    if (!g_branch_resolved || g_policy_branch_site == 0)
        return;

    BOOL ks = NO;
    int arm_errno = 0;
    BOOL arm_present = NO;
    BOOL armed = experiment_armed(&arm_errno, &arm_present);
    (void)kill_switch_active(&ks);

    if (g_validate_only) {
        diag_emit(@"validate_only_poll", @{
            @"validate_path" : @OUT_SMS_VALIDATE_PATH_PRIMARY,
            @"arm_active" : @(armed),
            @"patch_writes" : @NO,
        });
        return;
    }

    BOOL want_override = armed && !ks;
    volatile uint32_t *word = (volatile uint32_t *)g_policy_branch_site;
    uint32_t cur = *word;
    BOOL is_nop = (cur == OUT_SMS_PATCH_NOP_U32);

    if (want_override != g_last_want_override || ks != g_last_ks || armed != g_last_armed) {
        g_last_want_override = want_override;
        g_last_ks = ks;
        g_last_armed = armed;
        diag_emit(@"arm_killswitch_poll", @{
            @"arm_path" : @OUT_SMS_ARM_PATH_PRIMARY,
            @"arm_stat_errno" : @(arm_errno),
            @"arm_active" : @(armed),
            @"kill_switch_active" : @(ks),
            @"policy_override_desired" : @(want_override),
            @"branch_insn" : @(cur),
            @"original_policy_result" : [NSNull null],
        });
    }

    if (want_override && !is_nop) {
        if (cur != OUT_SMS_PATCH_ORIG_U32) {
            write_status_reason(OUT_SMS_STATUS_PATCH_FAIL, "PATCH_BYTES_MISMATCH");
            diag_emit(@"policy_override_refused", @{ @"gate_insn" : @(cur) });
            return;
        }
        if (write_insn_runtime(g_policy_branch_site, OUT_SMS_PATCH_NOP_U32)) {
            g_patch_applied = YES;
            diag_emit(@"policy_override_applied", @{
                @"hook_installed" : @YES,
                @"branch_site" : @((unsigned long long)g_policy_branch_site)
            });
            write_status_reason(OUT_SMS_STATUS_OVERRIDE_ACTIVE, NULL);
        } else {
            write_status_reason(OUT_SMS_STATUS_PATCH_FAIL, "HOOK_BACKEND_UNSUPPORTED");
            diag_emit(@"policy_override_failed", @{ @"reason" : @"HOOK_BACKEND_UNSUPPORTED" });
        }
    } else if (!want_override && is_nop) {
        uint32_t restore = g_policy_branch_orig ? g_policy_branch_orig : OUT_SMS_PATCH_ORIG_U32;
        if (g_policy_branch_orig != 0 || OUT_SMS_PATCH_ORIG_U32 != 0) {
            if (write_insn_runtime(g_policy_branch_site, restore)) {
                g_patch_applied = NO;
                diag_emit(@"policy_override_cleared", @{ @"restored_insn" : @(restore) });
                write_status_reason(OUT_SMS_STATUS_LOADED_INACTIVE, NULL);
            }
        }
    } else if (!want_override && (cur == OUT_SMS_PATCH_ORIG_U32 || is_nop)) {
        write_status_reason(OUT_SMS_STATUS_LOADED_INACTIVE, OUT_SMS_REASON_TARGET_VALIDATED);
    }
}

static void start_sync_timer(void) {
    if (g_sync_timer)
        return;
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_UTILITY, 0);
    g_sync_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, q);
    if (!g_sync_timer)
        return;
    dispatch_source_set_timer(g_sync_timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                              (uint64_t)OUT_SMS_SYNC_POLL_SEC * NSEC_PER_SEC,
                              (uint64_t)(OUT_SMS_SYNC_POLL_SEC / 2) * NSEC_PER_SEC);
    dispatch_source_set_event_handler(g_sync_timer, ^{
        sync_policy_override();
    });
    dispatch_resume(g_sync_timer);
}

%ctor {
    @autoreleasepool {
        g_out_oslog = os_log_create("com.gitop.teslapadsmsoutbypass", "experiment");
        install_crash_counter_handlers();
        g_validate_only = validate_only_active();
        diag_emit(@"tweak_loaded", @{
            @"arm_path" : @OUT_SMS_ARM_PATH_PRIMARY,
            @"validate_path" : @OUT_SMS_VALIDATE_PATH_PRIMARY,
            @"validate_only" : @(g_validate_only),
        });
        diag_emit(@"ctor_entered", @{});

        const struct mach_header_64 *mh = pinned_commcenter_header();
        if (!mh) {
            write_status_reason(OUT_SMS_STATUS_PATCH_FAIL, "IMAGE_MISMATCH");
            diag_emit(@"tweak_abort", @{ @"reason" : @"IMAGE_MISMATCH", @"detail" : @"uuid_pin" });
            return;
        }
        diag_emit(@"target_image_found", @{});
        diag_emit(@"uuid_match", @{});

        NSString *exe = main_executable_path();
        if (!exe || !sha256_matches(exe) || !file_size_matches(exe)) {
            write_status_reason(OUT_SMS_STATUS_PATCH_FAIL, "IMAGE_MISMATCH");
            diag_emit(@"tweak_abort", @{ @"reason" : @"IMAGE_MISMATCH", @"detail" : @"sha_or_size" });
            return;
        }

        intptr_t slide = 0;
        for (uint32_t i = 0; i < _dyld_image_count(); i++) {
            if (_dyld_get_image_header(i) == (const struct mach_header *)mh) {
                slide = _dyld_get_image_vmaddr_slide(i);
                break;
            }
        }

        NSDictionary *resolve_diag = nil;
        out_sms_gate_stage_t rf =
            resolve_policy_branch(slide, mh, exe, &g_policy_branch_site, &g_policy_branch_orig, &resolve_diag);
        if (rf != OUT_SMS_GATE_OK) {
            const char *rname = out_sms_gate_stage_name(rf);
            write_status_reason(OUT_SMS_STATUS_PATCH_FAIL, rname);
            NSMutableDictionary *fail = [NSMutableDictionary dictionaryWithDictionary:resolve_diag ?: @{}];
            fail[@"ok"] = @NO;
            fail[@"fail"] = @(rname);
            diag_emit(@"hook_target_resolved", fail);
            return;
        }

        g_branch_resolved = YES;
        diag_emit(@"string_found", @{});
        diag_emit(@"xref_unique", @{});
        diag_emit(@"gate_found", @{});
        diag_emit(@"bytes_match", @{});

        NSMutableDictionary *ok = [NSMutableDictionary dictionaryWithDictionary:resolve_diag ?: @{}];
        ok[@"ok"] = @YES;
        ok[@"branch_orig"] = @(g_policy_branch_orig);
        ok[@"validate_only"] = @(g_validate_only);
        diag_emit(@"hook_target_resolved", ok);

        if (g_validate_only) {
            write_status_reason(OUT_SMS_STATUS_LOADED_INACTIVE, OUT_SMS_REASON_TARGET_VALIDATED);
            diag_emit(@"hook_installed", @{
                @"mode" : @"validate_only_no_patch",
                @"decision" : @OUT_SMS_REASON_TARGET_VALIDATED_UP,
            });
            return;
        }

        diag_emit(@"hook_installed", @{
            @"mode" : @"reversible_cbz_nop_mshookmemory",
            @"runtime_sync_sec" : @(OUT_SMS_SYNC_POLL_SEC),
            @"decision" : @OUT_SMS_REASON_TARGET_VALIDATED_UP,
        });
        write_status_reason(OUT_SMS_STATUS_LOADED_INACTIVE, OUT_SMS_REASON_TARGET_VALIDATED);

        start_sync_timer();
        sync_policy_override();
    }
}
