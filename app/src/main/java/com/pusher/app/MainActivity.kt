package com.pusher.app

import android.Manifest
import android.annotation.SuppressLint
import android.content.pm.PackageManager
import android.graphics.SurfaceTexture
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.pusher.app.audio.AudioRecorder
import com.pusher.app.camera.CameraController
import com.pusher.app.camera.ICameraSource
import com.pusher.app.core.NativeStreamer
import java.util.concurrent.atomic.AtomicBoolean
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig

class MainActivity : AppCompatActivity() {

    private companion object {
        const val EGL_OPENGL_ES2_BIT = 4
        const val EGL_OPENGL_ES3_BIT = 0x40
        const val EGL_RECORDABLE_ANDROID = 0x3142
    }

    private lateinit var preview: GLSurfaceView
    private lateinit var status: TextView
    private lateinit var urlInput: EditText
    private lateinit var streamButton: Button
    private lateinit var beautyButton: Button
    private lateinit var switchCamera: Button
    private lateinit var resolutionSpinner: Spinner
    private lateinit var cameraSpinner: Spinner

    private val cameraController = CameraController()
    private var surfaceTexture: SurfaceTexture? = null
    private var glReady = false
    private var cameraPending = false
    private var openedWidth = 1280
    private var openedHeight = 720
    private var streaming = false
    private var beautyEnabled = false
    private var audioRecorder: AudioRecorder? = null

    private val frameAvailable = AtomicBoolean(false)
    private val transformMatrix = FloatArray(16)

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { result ->
            val cameraOk = result[Manifest.permission.CAMERA] == true
            if (!cameraOk) {
                status.text = "缺少相机权限"
            }
            if (cameraOk) {
                openCameraIfReady()
            }
        }

    private val renderer = object : GLSurfaceView.Renderer {
        /** GL surface 创建时初始化 native 渲染环境和相机 OES 纹理。 */
        override fun onSurfaceCreated(gl: javax.microedition.khronos.opengles.GL10?, config: javax.microedition.khronos.egl.EGLConfig?) {
            val result = NativeStreamer.nativeInitGl()
            if (result != 0) {
                runOnUiThread { status.text = "native 初始化失败" }
                return
            }

            surfaceTexture?.release()
            surfaceTexture = null
            val textureId = NativeStreamer.nativeCreateOesTexture()
            if (textureId < 0) {
                runOnUiThread { status.text = "创建相机纹理失败" }
                return
            }
            val tex = SurfaceTexture(textureId)
            tex.setOnFrameAvailableListener {
                frameAvailable.set(true)
                preview.requestRender()
            }
            surfaceTexture = tex
            glReady = true
            runOnUiThread { openCameraIfReady() }
        }

        /** GL surface 尺寸变化时设置视口。 */
        override fun onSurfaceChanged(gl: javax.microedition.khronos.opengles.GL10?, width: Int, height: Int) {
            GLES20.glViewport(0, 0, width, height)
            NativeStreamer.nativeRenderFrame(FloatArray(16) { 0f }, 0L)
        }

        /** 每帧更新 SurfaceTexture 并调用 native 渲染。 */
        override fun onDrawFrame(gl: javax.microedition.khronos.opengles.GL10?) {
            val tex = surfaceTexture ?: return
            if (frameAvailable.compareAndSet(true, false)) {
                tex.updateTexImage()
                tex.getTransformMatrix(transformMatrix)
            }
            NativeStreamer.nativeRenderFrame(transformMatrix, tex.timestamp)
        }

    }

    /** 创建页面、初始化 GLSurfaceView、下拉框、按钮并申请权限。 */
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        preview = findViewById(R.id.preview)
        status = findViewById(R.id.status)
        urlInput = findViewById(R.id.url_input)
        streamButton = findViewById(R.id.stream_button)
        beautyButton = findViewById(R.id.beauty_button)
        switchCamera = findViewById(R.id.switch_camera)
        resolutionSpinner = findViewById(R.id.resolution_spinner)
        cameraSpinner = findViewById(R.id.camera_spinner)

        preview.setEGLContextClientVersion(3)
        preview.setEGLConfigChooser(object : GLSurfaceView.EGLConfigChooser {
            /** 选择支持 OpenGL ES3 且可录制的 EGL 配置。 */
            override fun chooseConfig(
                egl: EGL10,
                display: javax.microedition.khronos.egl.EGLDisplay
            ): EGLConfig {
                val configs = arrayOfNulls<EGLConfig>(1)
                val numConfigs = IntArray(1)
                val attributes = intArrayOf(
                    EGL10.EGL_RED_SIZE, 8,
                    EGL10.EGL_GREEN_SIZE, 8,
                    EGL10.EGL_BLUE_SIZE, 8,
                    EGL10.EGL_ALPHA_SIZE, 8,
                    EGL10.EGL_DEPTH_SIZE, 0,
                    EGL10.EGL_STENCIL_SIZE, 0,
                    EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT or EGL_OPENGL_ES3_BIT,
                    EGL_RECORDABLE_ANDROID, 1,
                    EGL10.EGL_NONE
                )
                if (!egl.eglChooseConfig(display, attributes, configs, 1, numConfigs) || numConfigs[0] <= 0) {
                    throw RuntimeException("没有找到可用的 EGL 配置")
                }
                return configs[0]!!
            }
        })
        preview.preserveEGLContextOnPause = true
        preview.setRenderer(renderer)
        preview.renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY

        setupSpinners()
        setupButtons()
        checkPermissions()
    }

    /** 初始化分辨率和相机模式下拉框。 */
    private fun setupSpinners() {
        ArrayAdapter.createFromResource(
            this,
            R.array.resolution_labels,
            android.R.layout.simple_spinner_dropdown_item
        ).also { resolutionSpinner.adapter = it }

        ArrayAdapter.createFromResource(
            this,
            R.array.camera_labels,
            android.R.layout.simple_spinner_dropdown_item
        ).also { cameraSpinner.adapter = it }

        resolutionSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (!::resolutionSpinner.isInitialized) return
                openCameraIfReady()
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }

        cameraSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (!::cameraSpinner.isInitialized) return
                cameraController.mode = if (position == 0) {
                    CameraController.Mode.CAMERA2
                } else {
                    CameraController.Mode.CAMERA1
                }
                openCameraIfReady()
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }
    }

    /** 绑定切换摄像头、开始/停止推流和美颜开关按钮。 */
    private fun setupButtons() {
        switchCamera.setOnClickListener {
            cameraController.switchFacing()
            openCameraIfReady()
        }

        streamButton.setOnClickListener {
            if (streaming) stopStream() else startStream()
        }

        beautyButton.setOnClickListener {
            beautyEnabled = !beautyEnabled
            NativeStreamer.nativeSetBeautyEnabled(beautyEnabled)
            beautyButton.text = getString(if (beautyEnabled) R.string.beauty_on else R.string.beauty_off)
        }
    }

    /** 检查并申请相机和麦克风权限。 */
    private fun checkPermissions() {
        val missing = mutableListOf<String>()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            missing += Manifest.permission.CAMERA
        }
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            missing += Manifest.permission.RECORD_AUDIO
        }
        if (missing.isEmpty()) {
            openCameraIfReady()
        } else {
            permissionLauncher.launch(missing.toTypedArray())
        }
    }

    /** 在 GL 环境和相机纹理就绪后打开相机。 */
    private fun openCameraIfReady() {
        if (!glReady || surfaceTexture == null) {
            cameraPending = true
            return
        }
        cameraPending = false
        val target = selectedResolution()
        cameraController.open(
            this,
            surfaceTexture!!,
            target.first,
            target.second,
            object : ICameraSource.Listener {
                override fun onOpened(previewWidth: Int, previewHeight: Int) {
                    openedWidth = previewWidth
                    openedHeight = previewHeight
                    runOnUiThread {
                        status.text = "${previewWidth}x${previewHeight} · 待机"
                    }
                }

                override fun onError(message: String) {
                    runOnUiThread { status.text = message }
                }
            }
        )
    }

    /** 根据下拉框选择返回目标宽高。 */
    private fun selectedResolution(): Pair<Int, Int> {
        return when (resolutionSpinner.selectedItemPosition) {
            0 -> 1280 to 720
            1 -> 1920 to 1080
            2 -> 640 to 480
            else -> 1280 to 720
        }
    }

    @SuppressLint("SetTextI18n")
    /** 后台线程启动 native 推流，并在成功后启动麦克风采集。 */
    private fun startStream() {
        if (streaming) return
        val url = urlInput.text.toString().trim()
        if (url.isEmpty()) {
            status.text = "请输入 RTMP 地址"
            return
        }

        val bitrate = when (resolutionSpinner.selectedItemPosition) {
            1 -> 4_000_000
            2 -> 1_200_000
            else -> 2_500_000
        }

        val width = openedWidth
        val height = openedHeight
        streamButton.isEnabled = false
        status.text = "正在连接 RTMP..."

        Thread({
            val result = NativeStreamer.nativeStartStream(
                url, width, height, 30, bitrate, 44100, 1, 96000
            )
            runOnUiThread {
                streamButton.isEnabled = true
                if (result != 0) {
                    status.text = "启动推流失败：$result"
                } else {
                    audioRecorder = AudioRecorder(44100, 1).also { it.start() }
                    streaming = true
                    streamButton.text = getString(R.string.stop_stream)
                    status.text = "推流中 · ${width}x${height}"
                    switchCamera.isEnabled = false
                    resolutionSpinner.isEnabled = false
                    cameraSpinner.isEnabled = false
                    urlInput.isEnabled = false
                }
            }
        }, "stream-start").start()
    }

    /** 停止麦克风采集和 native 推流。 */
    private fun stopStream() {
        audioRecorder?.stop()
        audioRecorder = null
        NativeStreamer.nativeStopStream()
        streaming = false
        streamButton.text = getString(R.string.start_stream)
        status.text = "${openedWidth}x${openedHeight} · 待机"
        switchCamera.isEnabled = true
        resolutionSpinner.isEnabled = true
        cameraSpinner.isEnabled = true
        urlInput.isEnabled = true
    }

    /** 页面暂停时停止推流、关闭相机并暂停 GLSurfaceView。 */
    override fun onPause() {
        if (streaming) stopStream()
        cameraController.close()
        super.onPause()
        preview.onPause()
    }

    /** 页面恢复时恢复 GLSurfaceView，并在 GL 就绪后重开相机。 */
    override fun onResume() {
        super.onResume()
        preview.onResume()
        if (glReady) openCameraIfReady()
    }

    /** 销毁时关闭相机并释放相机纹理。 */
    override fun onDestroy() {
        cameraController.close()
        surfaceTexture?.release()
        surfaceTexture = null
        super.onDestroy()
    }
}
