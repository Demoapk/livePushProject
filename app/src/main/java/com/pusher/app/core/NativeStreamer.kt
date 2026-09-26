package com.pusher.app.core

/**
 * JNI 桥接层：Java/Kotlin 只负责相机、音频输入和页面，
 * OpenGL 渲染、H.264/AAC 编码、FLV 封装与 RTMP 推流全部下沉到 C++。
 */
object NativeStreamer {

    init {
        System.loadLibrary("native_streamer")
    }

    /** 在 GL 线程创建 native 渲染与编码环境。 */
    external fun nativeInitGl(): Int

    /** 在 GL 线程创建 camera 使用的 OES 纹理，返回纹理 id。 */
    external fun nativeCreateOesTexture(): Int

    /**
     * 每帧调用：把相机 OES 纹理绘制到预览窗口，并在推流时同步绘制到编码器输入 Surface。
     * transformMatrix 为 SurfaceTexture.getTransformMatrix 的结果，timestampNs 为帧时间戳。
     */
    external fun nativeRenderFrame(transformMatrix: FloatArray, timestampNs: Long)

    /** 开启/关闭 OpenCL 美颜。 */
    external fun nativeSetBeautyEnabled(enabled: Boolean)

    /** 启动推流，返回 0 表示成功。 */
    external fun nativeStartStream(
        url: String,
        videoWidth: Int,
        videoHeight: Int,
        videoFps: Int,
        videoBitrate: Int,
        audioSampleRate: Int,
        audioChannels: Int,
        audioBitrate: Int
    ): Int

    /** 停止推流并释放编码器、RTMP 连接。 */
    external fun nativeStopStream()

    /** 音频 PCM 帧输入：16bit signed little-endian。 */
    external fun nativeOnAudioPcm(data: ByteArray, size: Int, timestampNs: Long)

    /** 释放 native 资源，通常在 GL surface 销毁时调用。 */
    external fun nativeRelease()
}
