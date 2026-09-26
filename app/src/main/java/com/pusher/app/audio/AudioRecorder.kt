package com.pusher.app.audio

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Process
import com.pusher.app.core.NativeStreamer
import java.util.concurrent.atomic.AtomicBoolean

/**
 * 麦克风 PCM 采集线程。采集结果直接送入 C++ 的 AAC 编码队列。
 */
class AudioRecorder(
    private val sampleRate: Int,
    private val channelCount: Int
) {
    private val running = AtomicBoolean(false)
    private var recordThread: Thread? = null
    private var audioRecord: AudioRecord? = null

    @SuppressLint("MissingPermission")
    fun start() {
        if (running.getAndSet(true)) return

        val channelConfig = if (channelCount == 1) {
            AudioFormat.CHANNEL_IN_MONO
        } else {
            AudioFormat.CHANNEL_IN_STEREO
        }

        val minBuffer = AudioRecord.getMinBufferSize(
            sampleRate,
            channelConfig,
            AudioFormat.ENCODING_PCM_16BIT
        )
        val bufferSize = maxOf(minBuffer * 2, sampleRate * channelCount * 2 / 10)

        val recorder = try {
            AudioRecord(
                MediaRecorder.AudioSource.MIC,
                sampleRate,
                channelConfig,
                AudioFormat.ENCODING_PCM_16BIT,
                bufferSize
            )
        } catch (t: Throwable) {
            running.set(false)
            return
        }

        if (recorder.state != AudioRecord.STATE_INITIALIZED) {
            recorder.release()
            running.set(false)
            return
        }

        audioRecord = recorder
        try {
            recorder.startRecording()
        } catch (t: Throwable) {
            recorder.release()
            audioRecord = null
            running.set(false)
            return
        }

        recordThread = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
            // 一个 AAC 帧通常是 1024 个采样点。按这个粒度喂给编码器，
            // 才能保证每个编码帧拿到单调递增的时间戳，避免音画卡顿。
            val frameBytes = 1024 * channelCount * 2
            val buffer = ByteArray(frameBytes)
            var filled = 0
            var timestamp = 0L
            var firstFrame = true
            while (running.get()) {
                val read = recorder.read(buffer, filled, frameBytes - filled)
                if (read > 0) {
                    filled += read
                    if (filled == frameBytes) {
                        if (firstFrame) {
                            timestamp = System.nanoTime()
                            firstFrame = false
                        } else {
                            timestamp += 1024L * 1_000_000_000L / sampleRate
                        }
                        NativeStreamer.nativeOnAudioPcm(buffer, frameBytes, timestamp)
                        filled = 0
                    }
                }
            }
        }, "audio-capture")
        recordThread?.start()
    }

    fun stop() {
        if (!running.getAndSet(false)) return
        recordThread?.join(500)
        recordThread = null
        try {
            audioRecord?.stop()
        } catch (_: Throwable) {
        }
        audioRecord?.release()
        audioRecord = null
    }
}
