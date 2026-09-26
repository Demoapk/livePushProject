package com.pusher.app.camera

import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCharacteristics

class CameraController {
    enum class Mode { CAMERA2, CAMERA1 }

    private var source: ICameraSource? = null
    var mode: Mode = Mode.CAMERA2
    var facing: Int = CameraCharacteristics.LENS_FACING_FRONT

    fun open(
        context: Context,
        surfaceTexture: SurfaceTexture,
        width: Int,
        height: Int,
        listener: ICameraSource.Listener
    ) {
        // 关闭旧相机，根据当前模式创建 Camera1 或 Camera2 输入源。
        close()
        source = when (mode) {
            Mode.CAMERA2 -> Camera2Source()
            Mode.CAMERA1 -> Camera1Source()
        }
        source?.open(context, surfaceTexture, width, height, facing, listener)
    }

    fun close() {
        // 关闭并释放当前相机输入源。
        source?.close()
        source = null
    }

    fun switchFacing() {
        // 在前/后摄像头之间切换。
        facing = if (facing == CameraCharacteristics.LENS_FACING_FRONT) {
            CameraCharacteristics.LENS_FACING_BACK
        } else {
            CameraCharacteristics.LENS_FACING_FRONT
        }
    }
}
