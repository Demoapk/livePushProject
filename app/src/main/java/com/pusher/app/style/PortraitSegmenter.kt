package com.pusher.app.style

import android.graphics.Bitmap
import android.graphics.Color
import android.util.Log
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.segmentation.Segmentation
import com.google.mlkit.vision.segmentation.selfie.SelfieSegmenterOptions
import java.nio.ByteBuffer

/**
 * 使用 Google ML Kit 的人像分割实现背景虚化。
 */
class PortraitSegmenter {

    private val segmenter = Segmentation.getClient(
        SelfieSegmenterOptions.Builder()
            .setDetectorMode(SelfieSegmenterOptions.STREAM_MODE)
            .build()
    )

    /** 对人像做背景虚化。 */
    fun process(bitmap: Bitmap): Bitmap {
        // 在低分辨率上做分割和模糊，避免全屏 CPU 计算卡死。
        val scale = 256f / maxOf(bitmap.width, bitmap.height)
        val smallWidth = maxOf(1, (bitmap.width * scale).toInt())
        val smallHeight = maxOf(1, (bitmap.height * scale).toInt())
        val small = Bitmap.createScaledBitmap(bitmap, smallWidth, smallHeight, true)
        val image = InputImage.fromBitmap(small, 0)
        val mask = Tasks.await(segmenter.process(image))
        val maskWidth = mask.width
        val maskHeight = mask.height
        val maskBuffer: ByteBuffer = mask.buffer
        var maskSum = 0L
        for (i in 0 until maskWidth * maskHeight) {
            maskSum += maskBuffer.get(i).toInt() and 0xFF
        }
        Log.i("PortraitSegmenter", "mask=${maskWidth}x${maskHeight} avg=${maskSum / (maskWidth * maskHeight)}")

        val scaled = Bitmap.createScaledBitmap(small, maskWidth, maskHeight, true)
        val pixels = IntArray(maskWidth * maskHeight)
        scaled.getPixels(pixels, 0, maskWidth, 0, 0, maskWidth, maskHeight)
        val blurred = fastBlur(scaled)
        val blurredPixels = IntArray(maskWidth * maskHeight)
        blurred.getPixels(blurredPixels, 0, maskWidth, 0, 0, maskWidth, maskHeight)

        val outPixels = IntArray(maskWidth * maskHeight)
        for (i in outPixels.indices) {
            val confidence = (maskBuffer.get(i).toInt() and 0xFF) / 255f
            // 尝试反向阈值：如果 mask 语义是背景为高值，则保留低值区域。
            outPixels[i] = if (confidence < 0.85f) pixels[i] else blurredPixels[i]
        }

        val result = Bitmap.createBitmap(maskWidth, maskHeight, Bitmap.Config.ARGB_8888).apply {
            setPixels(outPixels, 0, maskWidth, 0, 0, maskWidth, maskHeight)
        }
        return Bitmap.createScaledBitmap(result, bitmap.width, bitmap.height, true)
    }

    /** 快速模糊：大幅降采样后再放大，近似高斯模糊。 */
    private fun fastBlur(bitmap: Bitmap): Bitmap {
        val down = Bitmap.createScaledBitmap(
            bitmap,
            maxOf(1, bitmap.width / 12),
            maxOf(1, bitmap.height / 12),
            true
        )
        return Bitmap.createScaledBitmap(down, bitmap.width, bitmap.height, true)
    }

    /** 关闭分割器。 */
    fun close() {
        segmenter.close()
    }
}
