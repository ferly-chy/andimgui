# BNM Android Build Script (Modern)
BNM_LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := BNM

# C++23 Standard
LOCAL_CPPFLAGS += -std=c++23 -Wall -Wextra -O3 -fvisibility=hidden -ffunction-sections -fdata-sections

LOCAL_C_INCLUDES := $(BNM_LOCAL_PATH)/include \
                    $(BNM_LOCAL_PATH)/external/include \
                    $(BNM_LOCAL_PATH)/external/spdlog/include \
                    $(BNM_LOCAL_PATH)/external \
                    $(BNM_LOCAL_PATH)/external/utf8 \
                    $(BNM_LOCAL_PATH)/src/private

# Recursively find all source files
define all-cpp-files-under
$(patsubst ./%,%,$(shell find $(BNM_LOCAL_PATH)/$(1) -name "*.cpp"))
endef

LOCAL_SRC_FILES := $(call all-cpp-files-under,src)

LOCAL_LDLIBS := -llog

include $(BUILD_STATIC_LIBRARY)
