package com.pusher.app.camera

import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface

class Camera2Source : ICameraSource {
    private var cameraManager: CameraManager? = null
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var handlerThread: HandlerThread? = null
    private var handler: Handler? = null
    private var surfaceTexture: SurfaceTexture? = null
    private var previewWidth = 1280
    private var previewHeight = 720

    override fun open(
        context: Context,
        surfaceTexture: SurfaceTexture,
        width: Int,
        height: Int,
        facing: Int,
        listener: ICameraSource.Listener
    ) {
        close()
        this.surfaceTexture = surfaceTexture

        handlerThread = HandlerThread("camera2-thread").also { it.start() }
        handler = Handler(handlerThread!!.looper)
        cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager

        val cameraId = findCameraId(cameraManager!!, facing)
        if (cameraId == null) {
            listener.onError("没有找到对应方向的摄像头")
            return
        }

        try {
            val characteristics = cameraManager!!.getCameraCharacteristics(cameraId)
            val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            val sizes = map?.getOutputSizes(SurfaceTexture::class.java) ?: emptyArray()
            val size = choosePreviewSize(sizes, width, height)
            previewWidth = size.width
            previewHeight = size.height
            surfaceTexture.setDefaultBufferSize(size.width, size.height)

            @Suppress("MissingPermission")
            cameraManager!!.openCamera(cameraId, object : CameraDevice.StateCallback() {
                override fun onOpened(device: CameraDevice) {
                    cameraDevice = device
                    createCaptureSession(device, Surface(surfaceTexture), listener)
                }

                override fun onDisconnected(device: CameraDevice) {
                    device.close()
                    cameraDevice = null
                }

                override fun onError(device: CameraDevice, error: Int) {
                    device.close()
                    cameraDevice = null
                    listener.onError("Camera2 错误：$error")
                }
            }, handler)
        } catch (t: Throwable) {
            listener.onError(t.message ?: "Camera2 打开失败")
        }
    }

    private fun createCaptureSession(
        device: CameraDevice,
        surface: Surface,
        listener: ICameraSource.Listener
    ) {
        try {
            device.createCaptureSession(
                listOf(surface),
                object : CameraCaptureSession.StateCallback() {
                    override fun onConfigured(session: CameraCaptureSession) {
                        captureSession = session
                        try {
                            val request = device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                            request.addTarget(surface)
                            request.set(
                                CaptureRequest.CONTROL_MODE,
                                CaptureRequest.CONTROL_MODE_AUTO
                            )
                            session.setRepeatingRequest(request.build(), null, handler)
                            listener.onOpened(previewWidth, previewHeight)
                        } catch (t: Throwable) {
                            listener.onError(t.message ?: "Camera2 预览配置失败")
                        }
                    }

                    override fun onConfigureFailed(session: CameraCaptureSession) {
                        listener.onError("Camera2 session 配置失败")
                    }
                },
                handler
            )
        } catch (t: Throwable) {
            listener.onError(t.message ?: "Camera2 session 创建失败")
        }
    }

    override fun close() {
        try {
            captureSession?.close()
        } catch (_: Throwable) {
        }
        captureSession = null
        try {
            cameraDevice?.close()
        } catch (_: Throwable) {
        }
        cameraDevice = null
        handlerThread?.quitSafely()
        try {
            handlerThread?.join(500)
        } catch (_: Throwable) {
        }
        handlerThread = null
        handler = null
    }

    private fun findCameraId(manager: CameraManager, facing: Int): String? {
        return manager.cameraIdList.firstOrNull { id ->
            manager.getCameraCharacteristics(id)
                .get(CameraCharacteristics.LENS_FACING) == facing
        }
    }

    private fun choosePreviewSize(
        sizes: Array<android.util.Size>,
        targetWidth: Int,
        targetHeight: Int
    ): android.util.Size {
        if (sizes.isEmpty()) return android.util.Size(1280, 720)
        var best = sizes[0]
        var bestScore = Long.MAX_VALUE
        for (size in sizes) {
            val score = Math.abs(size.width.toLong() * size.height - targetWidth.toLong() * targetHeight) +
                Math.abs(size.width - targetWidth) * 100L
            if (score < bestScore) {
                bestScore = score
                best = size
            }
        }
        return best
    }
}
