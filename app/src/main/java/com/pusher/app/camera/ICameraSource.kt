package com.pusher.app.camera

import android.content.Context
import android.graphics.SurfaceTexture

/**
 * Camera1 与 Camera2 的统一输入接口。
 */
interface ICameraSource {
    fun open(
        context: Context,
        surfaceTexture: SurfaceTexture,
        width: Int,
        height: Int,
        facing: Int,
        listener: Listener
    )

    fun close()

    interface Listener {
        fun onOpened(previewWidth: Int, previewHeight: Int)
        fun onError(message: String)
    }
}
