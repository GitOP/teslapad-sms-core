TARGET := iphone:clang:16.5:15.0
INSTALL_TARGET_PROCESSES = CommCenter
ARCHS = arm64

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = TeslaSMSOutBypass

TeslaSMSOutBypass_FILES = Tweak.x src/out_sms_gate_core.c
PIN_HDR := $(wildcard include/out_sms_pin_generated.h)
ifneq ($(PIN_HDR),)
TeslaSMSOutBypass_CFLAGS = -fobjc-arc -I. -include $(PIN_HDR)
else
TeslaSMSOutBypass_CFLAGS = -fobjc-arc -I.
endif
TeslaSMSOutBypass_FRAMEWORKS = Foundation
TeslaSMSOutBypass_LIBRARIES = substrate

include $(THEOS)/makefiles/tweak.mk

before-package::
	@echo "TESLA_SMS_OUT_BYPASS_MARKER=$$(echo -n 'INACTIVE_EXPERIMENT' | base64)"