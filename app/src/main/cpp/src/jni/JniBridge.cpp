#include <jni.h>

#include <cstdint>

#include "pusher/core/StreamEngine.h"

namespace {
pusher::StreamEngine g_engine;
}

extern "C" {

JNIEXPORT jint JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeInitGl(JNIEnv*, jobject) {
    return g_engine.initGl() ? 0 : -1;
}

JNIEXPORT jint JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeCreateOesTexture(JNIEnv*, jobject) {
    return g_engine.createOesTexture();
}

JNIEXPORT void JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeRenderFrame(
    JNIEnv* env, jobject, jfloatArray transformMatrix, jlong timestampNs) {
    if (transformMatrix == nullptr) {
        return;
    }
    jfloat* matrix = env->GetFloatArrayElements(transformMatrix, nullptr);
    if (matrix == nullptr) {
        return;
    }
    g_engine.renderFrame(matrix, static_cast<int64_t>(timestampNs));
    env->ReleaseFloatArrayElements(transformMatrix, matrix, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeSetBeautyEnabled(JNIEnv*, jobject, jboolean enabled) {
    g_engine.setBeautyEnabled(enabled == JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeStartStream(
    JNIEnv* env,
    jobject,
    jstring url,
    jint videoWidth,
    jint videoHeight,
    jint videoFps,
    jint videoBitrate,
    jint audioSampleRate,
    jint audioChannels,
    jint audioBitrate) {
    if (url == nullptr) {
        return -1;
    }
    const char* chars = env->GetStringUTFChars(url, nullptr);
    if (chars == nullptr) {
        return -1;
    }

    pusher::VideoConfig video;
    video.width = videoWidth;
    video.height = videoHeight;
    video.fps = videoFps;
    video.bitrate = videoBitrate;

    pusher::AudioConfig audio;
    audio.sampleRate = audioSampleRate;
    audio.channels = audioChannels;
    audio.bitrate = audioBitrate;

    int result = g_engine.startStream(chars, video, audio);
    env->ReleaseStringUTFChars(url, chars);
    return result;
}

JNIEXPORT void JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeStopStream(JNIEnv*, jobject) {
    g_engine.stopStream();
}

JNIEXPORT void JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeOnAudioPcm(
    JNIEnv* env, jobject, jbyteArray data, jint size, jlong timestampNs) {
    if (data == nullptr || size <= 0) {
        return;
    }
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    if (bytes == nullptr) {
        return;
    }
    g_engine.onAudioPcm(
        reinterpret_cast<const uint8_t*>(bytes),
        static_cast<size_t>(size),
        static_cast<int64_t>(timestampNs));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_pusher_app_core_NativeStreamer_nativeRelease(JNIEnv*, jobject) {
    g_engine.release();
}

}  // extern "C"
