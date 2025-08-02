package com.pntt3011.cameraserver.source

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaRecorder
import android.util.Log
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class AudioSource(private val callback: SourceCallback) {
    private val sampleRate = 44100
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO
    private val audioFormat = AudioFormat.ENCODING_PCM_16BIT
    private val bitrate = 64000
    private val channelCount = 1

    // The doc tells that we should choose a higher value
    private val bufferSize = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat) * 4
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

        audioRecord.startRecording()

        val format = MediaFormat.createAudioFormat("audio/mp4a-latm", sampleRate, channelCount)
        format.setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
        format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, bufferSize)

        val codec = MediaCodec.createEncoderByType("audio/mp4a-latm")
        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        codec.start()

        val bufferInfo = MediaCodec.BufferInfo()

        var finished = false
        while (!finished) {
            // Enqueue
            val inputIndex = codec.dequeueInputBuffer(0)
            if (inputIndex >= 0) {
                if (isRecording) {
                    // Queue record data
                    codec.getInputBuffer(inputIndex)?.let { inputBuffer ->
                        val targetSize = bufferSize.coerceAtMost(inputBuffer.capacity())
                        val readBytes = audioRecord.read(inputBuffer, targetSize)
                        if (readBytes > 0) {
                            codec.queueInputBuffer(inputIndex, 0, readBytes, System.nanoTime() / 1000, 0)
                        }
                    }
                } else {
                    // Queue EOS
                    codec.queueInputBuffer(inputIndex, 0, 0, -1, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                }
            }

            // Dequeue
            while (true) {
                val outputIndex = codec.dequeueOutputBuffer(bufferInfo, 0)
                when {
                    outputIndex >= 0 &&
                            (bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0 -> {
                        val outputBuffer = codec.getOutputBuffer(outputIndex) ?: break
                        outputBuffer.position(bufferInfo.offset)
                        outputBuffer.limit(bufferInfo.offset + bufferInfo.size)
                        onAudioFrameAvailableNative(outputBuffer, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, bufferInfo.flags)
                        codec.releaseOutputBuffer(outputIndex, false)

                        if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                            finished = true
                            break
                        }
                    }
                    else -> break
                }
            }
        }

        codec.stop()
        codec.release()

        audioRecord.stop()
        audioRecord.release()

        Log.d("CleanUp", "gracefully clean up audio source")
        callback.handler.post {
            callback.onClosed()
        }
    }


    fun stop() {
        if (!isRecording) return
        isRecording = false
    }

    private external fun onAudioFrameAvailableNative(buffer: ByteBuffer, offset: Int, size: Int, time: Long, flags: Int)
}