#ifndef OUT_SMS_FIXTURE_LAYOUT_H
#define OUT_SMS_FIXTURE_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

uint8_t *out_sms_fixture_build_mapped(int use_good_token, int corrupt_fst, size_t *out_len);

#endif