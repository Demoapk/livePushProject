package com.pusher.app.camera

import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.Camera
import java.io.IOException

class Camera1Source : ICameraSource {
    private var camera: Camera? = null

    override fun open(
        context: Context,
        surfaceTexture: SurfaceTexture,
        width: Int,
        height: Int,
        facing: Int,
        listener: ICameraSource.Listener
    ) {
        close()
        val cameraId = findCameraId(facing)
        if (cameraId < 0) {
            listener.onError("没有找到对应方向的摄像头")
            return
        }

        try {
            val cam = Camera.open(cameraId)
            camera = cam
            val params = cam.parameters

            val previewSize = choosePreviewSize(params.supportedPreviewSizes, width, height)
            if (previewSize != null) {
                surfaceTexture.setDefaultBufferSize(previewSize.width, previewSize.height)
                params.setPreviewSize(previewSize.width, previewSize.height)
            } else {
                surfaceTexture.setDefaultBufferSize(1280, 720)
            }
            params.focusMode = chooseFocusMode(params.supportedFocusModes)
            cam.parameters = params
            cam.setPreviewTexture(surfaceTexture)
            cam.startPreview()
            listener.onOpened(previewSize?.width ?: 1280, previewSize?.height ?: 720)
        } catch (t: Throwable) {
            close()
            listener.onError(t.message ?: "Camera1 打开失败")
        }
    }

    override fun close() {
        try {
            camera?.stopPreview()
        } catch (_: Throwable) {
        }
        try {
            camera?.setPreviewCallback(null)
        } catch (_: Throwable) {
        }
        camera?.release()
        camera = null
    }

    private fun findCameraId(facing: Int): Int {
        val count = Camera.getNumberOfCameras()
        val info = Camera.CameraInfo()
        for (i in 0 until count) {
            Camera.getCameraInfo(i, info)
            if (info.facing == facing) return i
        }
        return if (count > 0) 0 else -1
    }

    private fun choosePreviewSize(
        sizes: List<Camera.Size>?,
        targetWidth: Int,
        targetHeight: Int
    ): Camera.Size? {
        if (sizes.isNullOrEmpty()) {
            return null
        }
        var best: Camera.Size? = null
        var bestScore = Long.MAX_VALUE
        for (size in sizes) {
            val score = Math.abs(size.width.toLong() * size.height - targetWidth.toLong() * targetHeight) +
                Math.abs(size.width - targetWidth) * 100L
            if (score < bestScore) {
                bestScore = score
                best = size
            }
        }
        return best ?: sizes[0]
    }

    private fun chooseFocusMode(supported: List<String>?): String {
        if (supported.isNullOrEmpty()) return ""
        return when {
            supported.contains(Camera.Parameters.FOCUS_MODE_CONTINUOUS_VIDEO) ->
                Camera.Parameters.FOCUS_MODE_CONTINUOUS_VIDEO
            supported.contains(Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE) ->
                Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE
            supported.contains(Camera.Parameters.FOCUS_MODE_AUTO) ->
                Camera.Parameters.FOCUS_MODE_AUTO
            else -> supported.firstOrNull() ?: ""
        }
    }
}
