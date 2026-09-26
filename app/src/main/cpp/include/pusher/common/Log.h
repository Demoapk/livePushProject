#pragma once

#include <android/log.h>

#define PUSHER_TAG "NativePusher"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, PUSHER_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, PUSHER_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, PUSHER_TAG, __VA_ARGS__)
