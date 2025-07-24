package com.pntt3011.cameraserver

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import android.os.Handler
import java.nio.ByteBuffer

interface SourceCallback {
    val handler: Handler
    fun onPrepared(format: MediaFormat)
    fun onFrameAvailable(buffer: ByteBuffer, bufferInfo: BufferInfo)
    fun onClosed()
}
