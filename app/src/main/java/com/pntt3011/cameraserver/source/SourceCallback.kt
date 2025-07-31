package com.pntt3011.cameraserver.source

import android.os.Handler

interface SourceCallback {
    val handler: Handler
    fun onClosed()
}
