package com.pntt3011.cameraserver

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaRecorder
import android.os.Handler
import android.util.Log
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class AudioSource {
    private val sampleRate = 44100
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO
    private val audioFormat = AudioFormat.ENCODING_PCM_16BIT
    private val bitrate = 64000
    private val channelCount = 1

    private val bufferSize = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat) * 2
    @Volatile
    private var isRecording = false

    fun start() {
        if (isRecording) return
        isRecording = true
        encodeExecutor.submit {
            try {
                startRecording()
            } catch (e: Exception) {
                Log.e("AudioSource", "Exception", e)
            }
        }
    }

    private val encodeExecutor = Executors.newSingleThreadExecutor { runnable ->
        Thread(
            runnable,
            "AudioEncoderThread"
        )
    }

    @SuppressLint("MissingPermission")
    private fun startRecording() {
        val audioRecord = AudioRecord(
            MediaRecorder.AudioSource.MIC,
            sampleRate,
            channelConfig,
            audioFormat,
            bufferSize
        )

        val format = MediaFormat.createAudioFormat("audio/mp4a-latm", sampleRate, channelCount)
        format.setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
        format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, bufferSize)

        val codec = MediaCodec.createEncoderByType("audio/mp4a-latm")
        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        codec.start()

        val buffer = ByteArray(bufferSize)

        audioRecord.startRecording()
        val bufferInfo = MediaCodec.BufferInfo()

        var finished = false

        while (!finished) {
            val inputIndex = codec.dequeueInputBuffer(0)

            // Enqueue
            if (inputIndex >= 0) {
                if (isRecording) {
                    var readBytes = 0
                    val inputBuffer = codec.getInputBuffer(inputIndex)
                    if (inputBuffer != null) {
                        inputBuffer.clear()
                        val targetSize = bufferSize.coerceAtMost(inputBuffer.capacity())
                        readBytes = audioRecord.read(inputBuffer, targetSize)
                    }
                    if (inputBuffer != null && readBytes > 0) {
                        inputBuffer.put(buffer, 0, readBytes)
                        codec.queueInputBuffer(inputIndex, 0, readBytes, System.nanoTime() / 1000, 0)
                    }
                } else {
                    codec.queueInputBuffer(inputIndex, 0, 0, System.nanoTime() / 1000, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                }
            }

            // Dequeue
            var outputIndex = codec.dequeueOutputBuffer(bufferInfo, 0)
            while (outputIndex >= 0) {
                if (bufferInfo.flags != MediaCodec.BUFFER_FLAG_END_OF_STREAM) {
                    val outputBuffer = codec.getOutputBuffer(outputIndex) ?: break
                    outputBuffer.position(bufferInfo.offset)
                    outputBuffer.limit(bufferInfo.offset + bufferInfo.size)
                    codec.releaseOutputBuffer(outputIndex, false)
                    outputIndex = codec.dequeueOutputBuffer(bufferInfo, 0)
                } else {
                    finished = true
                    break
                }
            }
        }

        codec.stop()
        codec.release()

        audioRecord.stop()
        audioRecord.release()

        closedHandler?.post { onClosed?.invoke() }
    }


    private var onClosed: (() -> Unit)? = null
    private var closedHandler: Handler? = null

    fun stop(handler: Handler, callback: (() -> Unit)?) {
        if (!isRecording) return
        onClosed = callback
        closedHandler = handler
        isRecording = false
        encodeExecutor?.awaitTermination(1, TimeUnit.SECONDS)
    }
}