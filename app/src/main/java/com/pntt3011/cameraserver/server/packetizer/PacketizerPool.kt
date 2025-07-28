package com.pntt3011.cameraserver.server.packetizer

class PacketizerPool {
    private val audio by lazy { AACLATMPacketizer() }
    private val video by lazy { H265Packetizer() }
    fun get(isVideo: Boolean) = if (isVideo) video else audio
}