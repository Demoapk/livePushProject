package com.pusher.app.camera

import android.content.Context
import android.graphics.SurfaceTexture

/**
 * Camera1 与 Camera2 的统一输入接口。
 */
interface ICameraSource {
    /** 打开相机并把预览输出绑定到指定 SurfaceTexture。 */
    fun open(
        context: Context,
        surfaceTexture: SurfaceTexture,
        width: Int,
        height: Int,
        facing: Int,
        listener: Listener
    )

    /** 关闭并释放相机资源。 */
    fun close()

    interface Listener {
        /** 相机成功打开并确定预览尺寸。 */
        fun onOpened(previewWidth: Int, previewHeight: Int)

        /** 相机打开或配置失败。 */
        fun onError(message: String)
    }
}
