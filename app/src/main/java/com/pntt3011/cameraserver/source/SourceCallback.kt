package com.pntt3011.cameraserver.source

import android.media.MediaCodec.BufferInfo
import android.media.MediaFormat
import android.os.Handler
import java.nio.ByteBuffer

interface SourceCallback {
    val handler: Handler
    fun onPrepared(format: MediaFormat)
    fun onClosed()
}
