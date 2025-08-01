package com.pntt3011.cameraserver.source

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLExt.EGL_RECORDABLE_ANDROID
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES31
import android.util.Log
import android.util.Range
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class CameraSource(context: Context, callback: SourceCallback) {

    private var encoder: MediaCodec? = null
    private var isEncoderRunning = false

    private val cameraCallback = callback
    private val cameraHandler get() = cameraCallback.handler
    private val cameraExecutor = Executor { command -> command?.let { cameraHandler.post(it) } }

    fun start() {
        if (isStopped) return
        cameraHandler.post {
            startRecordCamera()
        }
    }

    private fun startRecordCamera() {
        val config = CameraConfig()
        prepareCamera(config)
    }

    private val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager

    class CameraConfig {
        var cameraId: String = "0"
        val mimeType = "video/hevc"
        val frameRate = 24
        var width = 1088
        var height = 1088
        val bitrate = 2_000_000
        val iFrameInterval = 1
        val profile = 1
        val level = 2097152
    }

    @SuppressLint("MissingPermission")
    private fun prepareCamera(config: CameraConfig) {
        val stateCallback = object : CameraDevice.StateCallback() {
            override fun onOpened(camera: CameraDevice) {
                encodeExecutor.submit {
                    try {
                        startRecording(camera, config)
                    } catch (e: Exception) {
                        Log.e("CameraServer", "Exception", e)
                    }
                }
            }

            override fun onDisconnected(camera: CameraDevice) {
            }

            override fun onError(camera: CameraDevice, error: Int) {
            }
        }
        cameraManager.openCamera(config.cameraId, stateCallback, cameraHandler)
    }

    private var captureRequest: CaptureRequest? = null
    private var inputSurfaceTexture: SurfaceTexture? = null
    private var inputSurface: Surface? = null
    private var captureSession: CameraCaptureSession? = null

    private val frameLock = Any()
    private var isFrameAvailable = false

    private var program = 0
    private var aPosition = 0
    private var aTexCoord = 0
    private var uTexture = 0
    private var uTransformMatrixLocation = 0

    fun startRecording(device: CameraDevice, config: CameraConfig) {

        val surfaces = mutableListOf<Surface>()
        val encoder = prepareEncoder(config)
        val outputSurface = encoder.createInputSurface()
        this.encoder = encoder
        encoder.start()
        isEncoderRunning = true
        eglSetup(outputSurface)
        makeCurrent()

        val inputSurfaceTexture = setupGL(config)
        inputSurfaceTexture.setOnFrameAvailableListener {
            synchronized(frameLock) {
                frameTimeNano = System.nanoTime()
                if (!isFrameAvailable) {
                    isFrameAvailable = true
                    (frameLock as Object).notifyAll()
                }
            }
        }
        val glSurface = Surface(inputSurfaceTexture)
        this.inputSurfaceTexture = inputSurfaceTexture
        this.inputSurface = glSurface
        surfaces.add(glSurface)

        vertexBuffer = ByteBuffer.allocateDirect(vertexData.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
        vertexBuffer.put(vertexData).position(0)

        program = createShaderProgram()
        if (program == 0) {
            releaseGlProgram()
            throw RuntimeException("Could not create shader program")
        }
        aPosition = GLES31.glGetAttribLocation(program, "aPosition")
        aTexCoord = GLES31.glGetAttribLocation(program, "aTexCoord")
        uTexture = GLES31.glGetUniformLocation(program, "uTexture")
        uTransformMatrixLocation = GLES31.glGetUniformLocation(program, "uTransformMatrix")

        captureRequest = device.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
            addTarget(glSurface)
            set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(config.frameRate, config.frameRate))
        }.build()

        val sessionConfig = SessionConfiguration(
            SessionConfiguration.SESSION_REGULAR,
            surfaces.map { OutputConfiguration(it) },
            cameraExecutor,
            captureStateCallback
        )

        device.createCaptureSession(sessionConfig)
        startProcessEncodedFrame()
    }

    private fun prepareEncoder(config: CameraConfig): MediaCodec {
        val mediaFormat = MediaFormat.createVideoFormat(config.mimeType, config.width, config.height).apply {
            setInteger(MediaFormat.KEY_BIT_RATE, config.bitrate)
            setInteger(MediaFormat.KEY_FRAME_RATE, config.frameRate)
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, config.iFrameInterval)
            setInteger(MediaFormat.KEY_PROFILE, config.profile)
            setInteger(MediaFormat.KEY_LEVEL, config.level)
        }

        val encoder = MediaCodec.createEncoderByType(config.mimeType)
        encoder.configure(mediaFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        return encoder
    }

    private var eglDisplay: EGLDisplay? = null
    private var eglContext: EGLContext? = null
    private var outputSurface: Surface? = null
    private var eglSurface: EGLSurface? = null

    private fun eglSetup(surface: Surface) {
        outputSurface = surface
        eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (eglDisplay === EGL14.EGL_NO_DISPLAY) {
            throw RuntimeException("unable to get EGL14 display")
        }
        val version = IntArray(2)
        if (!EGL14.eglInitialize(eglDisplay, version, 0, version, 1)) {
            eglDisplay = null
            throw RuntimeException("unable to initialize EGL14")
        }

        // Configure EGL for recordable and OpenGL ES 2.0.  We want enough RGB bits
        // to minimize artifacts from possible YUV conversion.
        val egl14ConfigAttributes = intArrayOf(
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL_RECORDABLE_ANDROID, 1,
            EGL14.EGL_NONE
        )
        val configs = arrayOfNulls<EGLConfig>(1)
        val numConfigs = IntArray(1)
        if (!EGL14.eglChooseConfig(
                eglDisplay,
                egl14ConfigAttributes, 0,
                configs, 0,
                configs.size, numConfigs, 0
            )
        ) {
            throw RuntimeException("unable to find RGB888+recordable ES2 EGL config")
        }

        // Configure context for OpenGL ES 2.0.
        val egl14ContextAttributes = intArrayOf(
            EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL14.EGL_NONE
        )
        eglContext = EGL14.eglCreateContext(
            eglDisplay,
            configs[0],
            EGL14.EGL_NO_CONTEXT,
            egl14ContextAttributes,
            0
        )
        if (eglContext == null) {
            throw RuntimeException("null context")
        }

        // Create a window surface, and attach it to the Surface we received.
        val surfaceAttribs = intArrayOf(
            EGL14.EGL_NONE
        )
        eglSurface = EGL14.eglCreateWindowSurface(
            eglDisplay,
            configs[0],
            surface,
            surfaceAttribs,
            0
        )
        if (eglSurface == null) {
            throw RuntimeException("surface was null")
        }
    }

    /**
     * Makes our EGL context and surface current.
     */
    private fun makeCurrent() {
        if (!EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            throw RuntimeException("eglMakeCurrent failed")
        }
    }

    private var oesTextureId = 0
    private var fboTextureId = 0

    private fun setupGL(config: CameraConfig): SurfaceTexture {
        // Create OES texture
        val tex = IntArray(1)
        GLES31.glGenTextures(1, tex, 0)
        oesTextureId = tex[0]
        GLES31.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTextureId)
        GLES31.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES31.GL_TEXTURE_MIN_FILTER, GLES31.GL_LINEAR)
        GLES31.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES31.GL_TEXTURE_MAG_FILTER, GLES31.GL_LINEAR)
        GLES31.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES31.GL_TEXTURE_WRAP_S, GLES31.GL_CLAMP_TO_EDGE)
        GLES31.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES31.GL_TEXTURE_WRAP_T, GLES31.GL_CLAMP_TO_EDGE)

        val surfaceTexture = SurfaceTexture(oesTextureId)
        surfaceTexture.setDefaultBufferSize(config.width, config.height)

        // Create output texture
        GLES31.glGenTextures(1, tex, 0)
        fboTextureId = tex[0]
        GLES31.glBindTexture(GLES31.GL_TEXTURE_2D, fboTextureId)

        return surfaceTexture
    }

    private val captureStateCallback = object : CameraCaptureSession.StateCallback() {
        override fun onConfigured(session: CameraCaptureSession) {
            captureSession = session
            captureRequest?.let {
                session.setRepeatingRequest(it, null, null)
            }
        }

        override fun onClosed(session: CameraCaptureSession) {
            isStopped = true
            encodeExecutor.awaitTermination(5, TimeUnit.SECONDS)
            cameraCallback.onClosed()
        }

        override fun onConfigureFailed(session: CameraCaptureSession) { }
    }

    private val encodeExecutor = Executors.newSingleThreadExecutor { runnable ->
        Thread(
            runnable,
            "VideoEncoderThread"
        )
    }

    private fun startProcessEncodedFrame() {
        var completed: Boolean
        do {
            completed = processNextFrame()
            if (Thread.interrupted()) {
                break
            }
        } while (!completed)
        cleanUpResource()
    }

    private fun processNextFrame(): Boolean {
        if (!isDrawFinish) {
            isDrawFinish = drawFrame()
        }
        if (!isDequeFinish) {
            isDequeFinish = processNextVideoFrame()
        }
        return isDrawFinish && isDequeFinish
    }

    private var isDrawFinish = false
    private var isDequeFinish = false
    private var frameTimeNano = 0L

    private fun drawFrame(): Boolean {
        if (isStopped) {
            encoder?.signalEndOfInputStream()
            return true
        }

        var frameTimeNano: Long
        synchronized(frameLock) {
            // Use "while" instead of "if"
            // to avoid spurious wakeup
            while (!isFrameAvailable) {
                (frameLock as Object).wait()
            }
            isFrameAvailable = false
            frameTimeNano = this.frameTimeNano
        }

        inputSurfaceTexture?.updateTexImage()

        val transformMatrix = FloatArray(16)
        inputSurfaceTexture?.getTransformMatrix(transformMatrix)

        GLES31.glClearColor(0f, 0f, 0f, 1f)
        GLES31.glClear(GLES31.GL_COLOR_BUFFER_BIT)

        GLES31.glUseProgram(program)

        vertexBuffer.position(0)
        GLES31.glVertexAttribPointer(aPosition, 2, GLES31.GL_FLOAT, false, 4 * 4, vertexBuffer)
        GLES31.glEnableVertexAttribArray(aPosition)

        vertexBuffer.position(2)
        GLES31.glVertexAttribPointer(aTexCoord, 2, GLES31.GL_FLOAT, false, 4 * 4, vertexBuffer)
        GLES31.glEnableVertexAttribArray(aTexCoord)

        GLES31.glActiveTexture(GLES31.GL_TEXTURE0)
        GLES31.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTextureId)
        GLES31.glUniform1i(uTexture, 0)

        GLES31.glUniformMatrix4fv(uTransformMatrixLocation, 1, false, transformMatrix, 0)
        GLES31.glDrawArrays(GLES31.GL_TRIANGLE_STRIP, 0, 4)

        // For now, this function is a placeholder
        // You can implement drawing to FBO using a full screen quad and fragment shader here
        EGLExt.eglPresentationTimeANDROID(eglDisplay, eglSurface, frameTimeNano)
        EGL14.eglSwapBuffers(eglDisplay, eglSurface)

        return false
    }

    private val encoderOutputBufferInfo = MediaCodec.BufferInfo()

    private fun processNextVideoFrame(): Boolean {
        val encoder = this.encoder
        if (!isEncoderRunning || encoder == null) {
            throw Exception("Encoder is not running")
        }

        val outputIndex = encoder.dequeueOutputBuffer(encoderOutputBufferInfo, 0)
        when {
            outputIndex >= 0 -> {
                val encodedData = encoder.getOutputBuffer(outputIndex)
                    ?: throw Exception("Encoder output buffer is null")
                val bufferInfo = encoderOutputBufferInfo
                if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    return true
                }
                if (bufferInfo.size > 0) {
                    encodedData.position(bufferInfo.offset)
                    encodedData.limit(bufferInfo.offset + bufferInfo.size)
                    onVideoFrameAvailableNative(encodedData, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, bufferInfo.flags)
                    // Important: mark the buffer as processed
                    encoder.releaseOutputBuffer(outputIndex, false)
                }
            }
        }
        return false
    }

    fun stop() {
        if (isStopped) return
        cameraHandler.post {
            stopCamera()
        }
    }

    @Volatile
    private var isStopped = false

    private fun stopCamera() {
        if (isStopped) return
        captureSession?.apply {
            stopRepeating()
            close() // Call onClosed
        }
    }

    private fun cleanUpResource() {
        isEncoderRunning = false
        encoder?.stop()
        encoder?.release()
        encoder = null

        releaseGlProgram()

        if (eglDisplay !== EGL14.EGL_NO_DISPLAY) {
            EGL14.eglDestroySurface(eglDisplay, eglSurface)
            EGL14.eglDestroyContext(eglDisplay, eglContext)
            EGL14.eglReleaseThread()
            EGL14.eglTerminate(eglDisplay)

            eglDisplay = EGL14.EGL_NO_DISPLAY
            eglContext = EGL14.EGL_NO_CONTEXT
            eglSurface = EGL14.EGL_NO_SURFACE
        }
        outputSurface?.release()
        outputSurface = null

        inputSurface?.release()
        inputSurface = null
        inputSurfaceTexture?.release()
        inputSurfaceTexture = null

        Log.d("CleanUp", "gracefully clean up video source")
    }

    private val vertexData = floatArrayOf(
        -1f, -1f,  0f, 0f,
        1f, -1f,  1f, 0f,
        -1f,  1f,  0f, 1f,
        1f,  1f,  1f, 1f,
    )
    private lateinit var vertexBuffer: FloatBuffer

    private fun loadShader(type: Int, source: String): Int {
        val shader = GLES31.glCreateShader(type)
        GLES31.glShaderSource(shader, source)
        GLES31.glCompileShader(shader)
        val compiled = IntArray(1)
        GLES31.glGetShaderiv(shader, GLES31.GL_COMPILE_STATUS, compiled, 0)
        if (compiled[0] == 0) {
            val error = GLES31.glGetShaderInfoLog(shader)
            GLES31.glDeleteShader(shader)
            throw RuntimeException("Shader compilation failed: $error")
        }
        return shader
    }

    private var vertexShader = 0
    private var fragmentShader = 0

    private fun createShaderProgram(): Int {
        vertexShader = loadShader(GLES31.GL_VERTEX_SHADER, DEFAULT_VERTEX_SHADER)
        if (vertexShader == 0) {
            throw RuntimeException("failed loading vertex shader")
        }
        fragmentShader = loadShader(GLES31.GL_FRAGMENT_SHADER, DEFAULT_FRAGMENT_SHADER)
        if (fragmentShader == 0) {
            throw RuntimeException("failed loading fragment shader")
        }
        val program = GLES31.glCreateProgram()
        GLES31.glAttachShader(program, vertexShader)
        GLES31.glAttachShader(program, fragmentShader)
        GLES31.glLinkProgram(program)

        val linked = IntArray(1)
        GLES31.glGetProgramiv(program, GLES31.GL_LINK_STATUS, linked, 0)
        if (linked[0] == 0) {
            val error = GLES31.glGetProgramInfoLog(program)
            GLES31.glDeleteProgram(program)
            throw RuntimeException("Program link failed: $error")
        }
        return program
    }

    private fun releaseGlProgram() {
        GLES31.glDeleteProgram(program)
        GLES31.glDeleteShader(vertexShader)
        GLES31.glDeleteShader(fragmentShader)
        program = 0
        vertexShader = 0
        fragmentShader = 0
    }

    companion object {
        const val DEFAULT_VERTEX_SHADER = "" +
                "#version 300 es\n" +
                "in vec4 aPosition;\n" +
                "in vec2 aTexCoord;\n" +
                "uniform mat4 uTransformMatrix;\n" +
                "out vec2 vTexCoord;\n" +
                "\n" +
                "void main() {\n" +
                "    gl_Position = aPosition;\n" +
                "    vTexCoord = (uTransformMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;\n" +
                "}\n"

        const val DEFAULT_FRAGMENT_SHADER = "" +
                "#version 300 es\n" +
                "#extension GL_OES_EGL_image_external_essl3 : require\n" +
                "precision mediump float;\n" +
                "\n" +
                "in vec2 vTexCoord;\n" +
                "uniform samplerExternalOES uTexture;\n" +
                "out vec4 fragColor;\n" +
                "\n" +
                "void main() {\n" +
                "    fragColor = texture(uTexture, vTexCoord);\n" +
                "}\n"
    }

    private external fun onVideoFrameAvailableNative(buffer: ByteBuffer, offset: Int, size: Int, time: Long, flags: Int)
}