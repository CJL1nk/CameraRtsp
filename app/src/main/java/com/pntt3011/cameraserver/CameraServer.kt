package com.pntt3011.cameraserver

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Rect
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
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
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Range
import android.util.Size
import android.view.Surface
import java.util.concurrent.Executor
import java.util.concurrent.Executors

class CameraServer(context: Context) {

    private val cameraThread = HandlerThread("CameraThread").apply { start() }
    private val cameraHandler = Handler(cameraThread.looper)
    private var encoder: MediaCodec? = null
    private var isEncoderRunning = false


    fun start() {
        startRecordCamera()
//        startHost()
    }

    private fun startRecordCamera() {
        val config = CameraConfig(cameraManager)
        prepareCamera(config, cameraHandler)
    }

    private val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager

    class CameraConfig(cameraManager: CameraManager) {
        var cameraId: String? = null
        init {
            val cameraIds = cameraManager.cameraIdList
            for (id in cameraIds) {
                val characteristics = cameraManager.getCameraCharacteristics(id)
                val lensFacing = characteristics.get(CameraCharacteristics.LENS_FACING)
                if (lensFacing == CameraCharacteristics.LENS_FACING_BACK) {
                    cameraId = id
                    break
                }
            }
        }
        val mimeType = "video/avc"
        val frameRate = 24
        var width = 1280
        var height = 720
        val bitrate = 2_000_000
        val iFrameInterval = 5
        val profile = 8
        val level = 65536
    }

    @SuppressLint("MissingPermission")
    private fun prepareCamera(config: CameraConfig, handler: Handler) {
        val stateCallback = object : CameraDevice.StateCallback() {
            override fun onOpened(camera: CameraDevice) {
                val characteristics = cameraManager.getCameraCharacteristics(camera.id)
                val sensorRect = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE)
                val sensorOrientation = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION)

                val recordSize = getRecordSize(sensorRect, sensorOrientation)
                config.width = recordSize.width
                config.height = recordSize.height
                startRecording(camera, config)
            }

            private fun getRecordSize(sensorRect: Rect?, sensorOrientation: Int?): Size {
                val aspectRatio = sensorRect?.let { it.height().toFloat() / it.width().toFloat() } ?: (3f / 4f)
                val orientation = sensorOrientation ?: 0

                val width = config.width

                // Compute a suitable height of Surface, ensuring that we always generate something that is
                // dividable by 4. This ensures a supported alignment.
                var height = (width * aspectRatio).toInt()
                height -= height % 4

                return if (orientation == 0 || orientation == 180) {
                    Size(width, height)
                } else {
                    Size(height, width)
                }
            }

            override fun onDisconnected(camera: CameraDevice) {
            }

            override fun onError(camera: CameraDevice, error: Int) {
            }
        }
        cameraManager.openCamera(config.cameraId!!, stateCallback, handler)
    }

    private var captureRequest: CaptureRequest? = null
    private var inputSurfaceTexture: SurfaceTexture? = null
    private var inputSurface: Surface? = null
    private var captureSession: CameraCaptureSession? = null

    private val cameraExecutor = Executor { command -> command?.let { cameraHandler.post(it) } }

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
            drawFrame()
        }
        val glSurface = Surface(inputSurfaceTexture)
        this.inputSurfaceTexture = inputSurfaceTexture
        this.inputSurface = glSurface
        surfaces.add(glSurface)

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
        checkEglError("eglCreateContext")
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
        checkEglError("eglCreateWindowSurface")
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

    private fun checkEglError(message: String) {
        var error: Int
        if ((EGL14.eglGetError().also { error = it }) != EGL14.EGL_SUCCESS) {
            throw RuntimeException(message + ": EGL error: 0x" + Integer.toHexString(error))
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

        val surfaceTexture = SurfaceTexture(oesTextureId)

        // Create output texture
        GLES31.glGenTextures(1, tex, 0)
        fboTextureId = tex[0]
        GLES31.glBindTexture(GLES31.GL_TEXTURE_2D, fboTextureId)
        GLES31.glTexImage2D(
            GLES31.GL_TEXTURE_2D, 0, GLES31.GL_RGBA,
            config.width, config.height, 0, GLES31.GL_RGBA,
            GLES31.GL_UNSIGNED_BYTE, null
        )

        return surfaceTexture
    }

    private fun drawFrame() {
        inputSurfaceTexture?.updateTexImage()

        // For now, this function is a placeholder
        // You can implement drawing to FBO using a full screen quad and fragment shader here
        EGLExt.eglPresentationTimeANDROID(eglDisplay, eglSurface, inputSurfaceTexture?.timestamp!!)
        EGL14.eglSwapBuffers(eglDisplay, eglSurface)
    }

    private val captureStateCallback = object : CameraCaptureSession.StateCallback() {
        override fun onConfigured(session: CameraCaptureSession) {
            captureSession = session
            captureRequest?.let {
                session.setRepeatingRequest(it, null, null)
            }
        }

        override fun onClosed(session: CameraCaptureSession) {
            cameraThread.apply {
                quitSafely()
                join()
            }
        }

        override fun onConfigureFailed(session: CameraCaptureSession) { }
    }

    private val encodeExecutor = Executors.newSingleThreadExecutor()

    private fun startProcessEncodedFrame() {
        encodeExecutor.submit {
            var completed: Boolean
            do {
                completed = processNextFrame()
                if (Thread.interrupted()) {
                    cleanUpResource()
                    break
                }
            } while (!completed)

            if (completed) {
                cleanUpResource()
            }
        }
    }

    private fun processNextFrame(): Boolean {
        return processNextVideoFrame()
    }

    private val encoderOutputBufferInfo = MediaCodec.BufferInfo()


    private fun processNextVideoFrame(): Boolean {
        val encoder = this.encoder
        if (!isEncoderRunning || encoder == null) {
            throw Exception("Encoder is not running")
        }

        val outputIndex = encoder.dequeueOutputBuffer(encoderOutputBufferInfo, 0)
        when {
            outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER -> {
            }
            outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                val newFormat = encoder.outputFormat
            }
            outputIndex >= 0 -> {
                val encodedData = encoder.getOutputBuffer(outputIndex)
                    ?: throw Exception("Encoder output buffer is null")
                val bufferInfo = encoderOutputBufferInfo
                if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    Log.d("ABC", "processNextVideoFrame: End stream")
                    return true
                }
                if (bufferInfo.size > 0) {
                    encodedData.position(bufferInfo.offset)
                    encodedData.limit(bufferInfo.offset + bufferInfo.size)

                    Log.d("ABC", "processNextVideoFrame: Encode ${bufferInfo.size}")

                    // Important: mark the buffer as processed
                    encoder.releaseOutputBuffer(outputIndex, false)
                }
            }
        }
        return false
    }

    fun stop() {
        stopCamera()
    }

    private fun stopCamera() {
        encoder?.signalEndOfInputStream()

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
    }
}