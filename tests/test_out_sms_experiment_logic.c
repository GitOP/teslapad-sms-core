#include "../TeslaSMSOutBypass/include/out_sms_experiment.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (!out_sms_status_line_valid(OUT_SMS_STATUS_LOADED_INACTIVE))
        return 1;
    if (OUT_SMS_PATCH_VADDR != OUT_SMS_PIN_PLACEHOLDER_GATE_VA)
        return 1;
    if (OUT_SMS_PATCH_ORIG_U32 != OUT_SMS_PIN_PLACEHOLDER_GATE_INSN)
        return 1;
    if (strcmp(OUT_SMS_LOG_TOKEN, "#I Sms not allowed on device.") != 0)
        return 1;
    if (OUT_SMS_EXPECTED_COMM_SIZE != OUT_SMS_PIN_PLACEHOLDER_COMM_SIZE)
        return 1;
    puts("out_sms host tests passed");
    return 0;
}