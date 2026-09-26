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
        close()
        source = when (mode) {
            Mode.CAMERA2 -> Camera2Source()
            Mode.CAMERA1 -> Camera1Source()
        }
        source?.open(context, surfaceTexture, width, height, facing, listener)
    }

    fun close() {
        source?.close()
        source = null
    }

    fun switchFacing() {
        facing = if (facing == CameraCharacteristics.LENS_FACING_FRONT) {
            CameraCharacteristics.LENS_FACING_BACK
        } else {
            CameraCharacteristics.LENS_FACING_FRONT
        }
    }
}
