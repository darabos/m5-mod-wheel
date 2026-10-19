#include <LittleFS.h>
#include <M5Unified.h>

// ------------------------------------------------------------
// Settings
// ------------------------------------------------------------

// 16 kHz mono, 16-bit PCM is plenty for speech.
static constexpr uint32_t SAMPLE_RATE = 16000;

static float playbackSpeedMultiplier = 1.0f;
static bool playbackActive = false;
static uint32_t nextAnimationFrameMs = 0;
static uint8_t playbackFrameIndex = 0;
static float lastTiltMultiplier = 1.0f;
M5Canvas canvas = M5Canvas(&M5.Display);

static void updatePlaybackSpeedFromTilt()
{
    auto imuUpdate = M5.Imu.update();
    if (!imuUpdate)
    {
        return;
    }

    auto imuData = M5.Imu.getImuData();
    float tiltMultiplier = 1.0f + (imuData.gyro.x / 600.0f);
    tiltMultiplier = constrain(tiltMultiplier, 0.5f, 3.0f);

    if (fabs(tiltMultiplier - lastTiltMultiplier) > 0.05f)
    {
        lastTiltMultiplier = tiltMultiplier;
        playbackSpeedMultiplier = tiltMultiplier;

        if (playbackActive)
        {
            playRecording();
        }
    }
}

static void showCactusImage(const char *path)
{
    if (!LittleFS.exists(path))
    {
        Serial.print("Missing image: ");
        Serial.println(path);
        return;
    }

    canvas.fillSprite(TFT_BLACK);
    canvas.drawPngFile(LittleFS, path);
    canvas.pushSprite(0, 0);
}

// Maximum recording time.
// 10 seconds × 16000 samples × 2 bytes = 320 KB.
static constexpr uint32_t MAX_RECORD_SECONDS = 10;

static constexpr size_t MAX_SAMPLES =
    SAMPLE_RATE * MAX_RECORD_SECONDS;

// Audio buffer.
// Allocated in PSRAM because the StickS3 has 8 MB PSRAM.
int16_t *recording = nullptr;

size_t recordedSamples = 0;
bool wasRecording = false;

// ------------------------------------------------------------
// Start recording
// ------------------------------------------------------------

void startRecording()
{
    if (recording == nullptr)
    {
        Serial.println("ERROR: recording buffer not allocated");
        return;
    }

    recordedSamples = 0;
    wasRecording = true;

    // The microphone and speaker cannot be used simultaneously.
    M5.Speaker.end();

    if (!M5.Mic.isEnabled())
    {
        M5.Mic.begin();
    }

    showCactusImage("/cactus6.png");

    Serial.println("Recording started");
}

// ------------------------------------------------------------
// Record audio while Button 1 is held
// ------------------------------------------------------------

void recordAudio()
{
    if (!M5.Mic.isEnabled())
    {
        return;
    }

    // Record in relatively small chunks so we can continuously
    // check the button and stop as soon as it is released.
    static constexpr size_t CHUNK_SAMPLES = 256;

    if (recordedSamples >= MAX_SAMPLES)
    {
        return;
    }

    size_t remaining = MAX_SAMPLES - recordedSamples;
    size_t samplesToRecord =
        min(CHUNK_SAMPLES, remaining);

    if (M5.Mic.record(
            &recording[recordedSamples],
            samplesToRecord,
            SAMPLE_RATE))
    {

        recordedSamples += samplesToRecord;
    }
}

// ------------------------------------------------------------
// Stop recording
// ------------------------------------------------------------

void stopRecording()
{
    if (!wasRecording)
    {
        return;
    }

    wasRecording = false;

    // Wait for the current microphone operation to finish.
    while (M5.Mic.isRecording())
    {
        delay(1);
    }

    M5.Mic.end();

    showCactusImage("/cactus5.png");

    Serial.print("Recording stopped. Samples: ");
    Serial.println(recordedSamples);
}

// ------------------------------------------------------------
// Play recording
// ------------------------------------------------------------

void playRecording()
{
    if (recordedSamples == 0)
    {
        Serial.println("Nothing recorded");

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(10, 20);
        M5.Display.println("NO RECORDING");

        delay(500);
        return;
    }

    static constexpr uint32_t TRIM_MS = 100;
    static constexpr size_t TRIM_SAMPLES = (SAMPLE_RATE * TRIM_MS) / 1000;

    size_t playbackSamples = recordedSamples;
    int16_t *playbackBuffer = recording;

    if (playbackSamples > TRIM_SAMPLES)
    {
        playbackBuffer = recording + TRIM_SAMPLES;
        playbackSamples -= TRIM_SAMPLES * 2;
    }
    else
    {
        playbackBuffer = recording;
        playbackSamples = 0;
    }

    if (playbackSamples == 0)
    {
        Serial.println("Recording too short after trim");
        return;
    }

    uint32_t playbackRate = (uint32_t)(SAMPLE_RATE * playbackSpeedMultiplier + 0.5f);
    if (playbackRate == 0)
    {
        playbackRate = SAMPLE_RATE;
    }

    Serial.print("Playback started at ");
    Serial.print(playbackSpeedMultiplier, 2);
    Serial.print("x (rate: ");
    Serial.print(playbackRate);
    Serial.println(" Hz)");

    nextAnimationFrameMs = millis() + 180;
    playbackFrameIndex = 0;
    char framePath[32];
    snprintf(framePath, sizeof(framePath), "/cactus%d.png", playbackFrameIndex + 1);
    showCactusImage(framePath);

    // If we are restarting playback mid-stream, make sure the
    // previous output is fully stopped before starting the next one.
    if (M5.Speaker.isPlaying())
    {
        M5.Speaker.end();
    }

    // Start the speaker.
    M5.Speaker.begin();

    // Keep volume reasonable, especially when running from
    // the battery.
    M5.Speaker.setVolume(180);

    // Play the raw 16-bit mono PCM buffer.
    M5.Speaker.playRaw(
        playbackBuffer,
        playbackSamples,
        playbackRate,
        false, // stereo = false
        1,     // repeat count
        0      // channel
    );

    playbackActive = true;
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------

void setup()
{
    auto cfg = M5.config();

    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);

    canvas.createSprite(135, 240);
    M5.Display.setRotation(0);
    M5.Display.fillScreen(TFT_BLACK);

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(10, 10);
    M5.Display.println("Voice Recorder");

    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 45);
    M5.Display.println("Hold Btn1: Record");
    M5.Display.setCursor(10, 65);
    M5.Display.println("Press Btn2: Playback");

    // Allocate the audio buffer from PSRAM.
    recording = (int16_t *)heap_caps_malloc(
        MAX_SAMPLES * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (recording == nullptr)
    {
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setCursor(10, 100);
        M5.Display.println("PSRAM allocation FAILED!");

        Serial.println("ERROR: Could not allocate recording buffer");
        return;
    }

    Serial.print("Audio buffer allocated: ");
    Serial.print(MAX_SAMPLES * sizeof(int16_t));
    Serial.println(" bytes");

    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS mount failed");
    }
    else
    {
        Serial.println("LittleFS ready");
    }

    showCactusImage("/cactus5.png");

    Serial.println("Ready.");
}

// ------------------------------------------------------------
// Main loop
// ------------------------------------------------------------

void loop()
{
    M5.update();

    if (playbackActive)
    {
        updatePlaybackSpeedFromTilt();

        if (millis() >= nextAnimationFrameMs)
        {
            nextAnimationFrameMs = millis() + 180 / playbackSpeedMultiplier;
            playbackFrameIndex = (playbackFrameIndex + 1) % 4;
            char framePath[32];
            snprintf(framePath, sizeof(framePath), "/cactus%d.png", playbackFrameIndex + 1);
            showCactusImage(framePath);
        }
    }
    else
    {

        // --------------------------------------------------------
        // Button 1 held = record
        // --------------------------------------------------------

        if (M5.BtnB.isPressed())
        {

            // Button has just been pressed.
            if (!wasRecording)
            {
                startRecording();
            }

            // Continue recording.
            recordAudio();

            // Maximum recording time reached.
            if (recordedSamples >= MAX_SAMPLES)
            {
                stopRecording();
            }
        }
        else
        {
            // Button 1 released.
            if (wasRecording)
            {
                stopRecording();
            }
        }

        // --------------------------------------------------------
        // Button 2 = playback
        // --------------------------------------------------------

        if (M5.BtnA.wasClicked())
        {

            // Don't try to play while recording.
            if (!wasRecording)
            {
                playRecording();
            }
        }
    }

    if (playbackActive && !M5.Speaker.isPlaying())
    {
        playbackActive = false;
        playbackSpeedMultiplier = 1.0f;
        lastTiltMultiplier = 1.0f;

        Serial.println("Playback finished");
        showCactusImage("/cactus7.png");
    }

    delay(1);
}
