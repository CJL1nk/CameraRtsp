package com.pntt3011.cameraserver.monitor

import android.util.Log

class TrackPerfMonitor(
    private val isVideo: Boolean,
    private val handler: android.os.Handler,
) {
    private val frameQueue = ArrayDeque<Pair<Long, Long>>()
    private var frameCount = 0
    private var frameSent = 0
    private var frameSkipped = 0
    private val sentOverheadBin = mutableMapOf<Int, Int>()
    private val skippedOverheadBin = mutableMapOf<Int, Int>()

    fun onFrameAvailable(presentationTimeUs: Long) {
        val now = System.nanoTime()
        handler.post {
            val item = Pair(presentationTimeUs, now)
            frameQueue.add(item)
        }
    }

    fun onFrameSend(presentationTimeUs: Long) {
        val now = System.nanoTime()
        handler.post {
            frameCount += 1
            var current: Pair<Long, Long> = Pair(0, 0)
            while (current.first < presentationTimeUs) {
                current = frameQueue.removeFirstOrNull() ?: break
                val overhead = now - current.second
                frameSkipped += 1
                skippedOverheadBin[getBin(overhead)] = (skippedOverheadBin[getBin(overhead)] ?: 0) + 1
            }
            if (current.first == presentationTimeUs) {
                frameSkipped -= 1
                frameSent += 1
                val overhead = now - current.second
                skippedOverheadBin[getBin(overhead)] = (skippedOverheadBin[getBin(overhead)] ?: 0) - 1
                sentOverheadBin[getBin(overhead)] = (sentOverheadBin[getBin(overhead)] ?: 0) + 1
            }
            val name = if (isVideo) "video" else "audio"
            if (frameCount == 1000) {
                Log.d(
                    "TrackPerfMonitor",
                    "Track $name: \n" +
                            "Send stats: ${getFiltedMap(sentOverheadBin)}\n" +
                            "Skip stats: ${getFiltedMap(skippedOverheadBin)}\n"
                )
                frameCount = 0
            }
        }
    }

    private fun getBin(x: Long) = (x / 1_000_000).toInt()

    private fun getFiltedMap(map: Map<Int, Int>): Map<Int, Int> {
        return map
            .filter { it.value != 0 }
            .toSortedMap()
    }
}