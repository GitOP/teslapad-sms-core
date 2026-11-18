TARGET := iphone:clang:16.5:15.0
INSTALL_TARGET_PROCESSES = CommCenter
ARCHS = arm64

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = TeslaPadSMSBypass

TeslaPadSMSBypass_FILES = Tweak.x
TeslaPadSMSBypass_CFLAGS = -fobjc-arc
TeslaPadSMSBypass_FRAMEWORKS = Foundation
TeslaPadSMSBypass_LIBRARIES = substrate

include $(THEOS)/makefiles/tweak.mk
