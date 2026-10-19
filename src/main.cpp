#include <M5Unified.h>

#include <cmath>
#include <cstdint>

#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32.h>

BLEMIDI_CREATE_INSTANCE("M5 Mod Wheel", MIDI)

namespace
{
constexpr uint8_t MIDI_CHANNEL = 1;
constexpr uint8_t MOD_WHEEL_CC = 1;
constexpr float FULL_SCALE_TILT_DEGREES = 45.0f;
constexpr float TILT_FILTER_ALPHA = 0.2f;
constexpr uint32_t DISPLAY_REFRESH_MS = 100;

enum class WheelMode : uint8_t
{
    ModWheel,
    PitchBend,
};

volatile bool midiConnected = false;
bool bleStarted = false;
bool pairingRequested = false;
bool hasTiltReading = false;
bool hasZeroPose = false;
bool hasSentMidiValue = false;
bool previousMidiConnected = false;
WheelMode wheelMode = WheelMode::ModWheel;
float filteredTiltDegrees = 0.0f;
float zeroTiltDegrees = 0.0f;
int32_t lastSentMidiValue = 0;
uint32_t nextDisplayRefreshMs = 0;

void onMidiConnected()
{
    midiConnected = true;
}

void onMidiDisconnected()
{
    midiConnected = false;
}

bool readTiltDegrees(float &tiltDegrees)
{
    if (!M5.Imu.update())
    {
        return false;
    }

    const auto &imuData = M5.Imu.getImuData();
    const float x = imuData.accel.x;
    const float y = imuData.accel.y;
    const float z = imuData.accel.z;
    const float transverseMagnitude = std::sqrt(y * y + z * z);
    const float magnitude = std::sqrt(x * x + transverseMagnitude * transverseMagnitude);

    if (magnitude < 0.1f)
    {
        return false;
    }

    tiltDegrees = std::atan2(x, transverseMagnitude) * (180.0f / PI);
    return true;
}

void startPairing()
{
    if (bleStarted)
    {
        return;
    }

    BLEMIDI.setHandleConnected(onMidiConnected);
    BLEMIDI.setHandleDisconnected(onMidiDisconnected);
    MIDI.begin();

    bleStarted = true;
    pairingRequested = true;
    Serial.println("BLE MIDI advertising started");
}

void setZeroPoseAndMode()
{
    float currentTiltDegrees = 0.0f;
    if (!readTiltDegrees(currentTiltDegrees))
    {
        Serial.println("Cannot change mode: IMU reading unavailable");
        return;
    }

    filteredTiltDegrees = currentTiltDegrees;
    hasTiltReading = true;
    zeroTiltDegrees = currentTiltDegrees;
    hasZeroPose = true;
    wheelMode = wheelMode == WheelMode::ModWheel
                    ? WheelMode::PitchBend
                    : WheelMode::ModWheel;
    hasSentMidiValue = false;

    Serial.print("Mode changed and zero pose captured: ");
    Serial.println(wheelMode == WheelMode::ModWheel ? "Mod Wheel" : "Pitch Bend");
}

float normalizedWheelPosition()
{
    float position = (filteredTiltDegrees - zeroTiltDegrees) / FULL_SCALE_TILT_DEGREES;
    if (position < -1.0f)
    {
        position = -1.0f;
    }
    else if (position > 1.0f)
    {
        position = 1.0f;
    }
    return position;
}

int32_t wheelMidiValue(float position)
{
    if (wheelMode == WheelMode::ModWheel)
    {
        return static_cast<int32_t>(std::lround((position + 1.0f) * 63.5f));
    }

    int32_t pitchBend = static_cast<int32_t>(std::lround(position * 8192.0f));
    if (pitchBend < -8192)
    {
        pitchBend = -8192;
    }
    else if (pitchBend > 8191)
    {
        pitchBend = 8191;
    }
    return pitchBend;
}

void sendWheelValue(int32_t midiValue)
{
    if (!bleStarted || !midiConnected || (hasSentMidiValue && midiValue == lastSentMidiValue))
    {
        return;
    }

    if (wheelMode == WheelMode::ModWheel)
    {
        MIDI.sendControlChange(MOD_WHEEL_CC, static_cast<uint8_t>(midiValue), MIDI_CHANNEL);
    }
    else
    {
        MIDI.sendPitchBend(static_cast<int>(midiValue), MIDI_CHANNEL);
    }

    lastSentMidiValue = midiValue;
    hasSentMidiValue = true;
}

void drawStatus(int32_t midiValue)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(5, 8);
    M5.Display.println(wheelMode == WheelMode::ModWheel ? "MOD WHEEL" : "PITCH BEND");

    M5.Display.setTextSize(1);
    M5.Display.setCursor(6, 43);
    M5.Display.print("BLE MIDI: ");
    if (!pairingRequested)
    {
        M5.Display.println("OFF");
    }
    else if (midiConnected)
    {
        M5.Display.println("CONNECTED");
    }
    else
    {
        M5.Display.println("ADVERTISING");
    }

    M5.Display.setCursor(6, 65);
    if (hasZeroPose)
    {
        if (wheelMode == WheelMode::ModWheel)
        {
            M5.Display.print("CC 1 value: ");
            M5.Display.println(midiValue);
        }
        else
        {
            M5.Display.print("Bend value: ");
            M5.Display.println(midiValue + 8192);
        }

        M5.Display.setCursor(6, 85);
        M5.Display.print("Tilt: ");
        M5.Display.print(filteredTiltDegrees - zeroTiltDegrees, 1);
        M5.Display.println(" deg");
    }
    else
    {
        M5.Display.println("Waiting for IMU...");
    }

    M5.Display.drawFastHLine(5, 112, M5.Display.width() - 10, TFT_DARKGREY);
    M5.Display.setCursor(6, 125);
    M5.Display.println("Btn1: mode + zero");
    M5.Display.setCursor(6, 145);
    M5.Display.println("Btn2: pair");
    M5.Display.setCursor(6, 177);
    M5.Display.println("Tilt X to move wheel");
}
} // namespace

void setup()
{
    auto config = M5.config();
    M5.begin(config);

    Serial.begin(115200);
    M5.Display.setRotation(0);
    M5.Display.fillScreen(TFT_BLACK);

    Serial.println("M5 Mod Wheel ready");
    drawStatus(64);
}

void loop()
{
    M5.update();

    if (M5.BtnA.wasClicked())
    {
        startPairing();
    }

    if (M5.BtnB.wasClicked())
    {
        setZeroPoseAndMode();
    }

    if (bleStarted)
    {
        MIDI.read();
    }

    const bool connected = midiConnected;
    if (connected != previousMidiConnected)
    {
        previousMidiConnected = connected;
        hasSentMidiValue = false;
        Serial.println(connected ? "BLE MIDI connected" : "BLE MIDI disconnected");
    }

    float currentTiltDegrees = 0.0f;
    if (readTiltDegrees(currentTiltDegrees))
    {
        if (!hasTiltReading)
        {
            filteredTiltDegrees = currentTiltDegrees;
            zeroTiltDegrees = currentTiltDegrees;
            hasTiltReading = true;
            hasZeroPose = true;
        }
        else
        {
            filteredTiltDegrees += TILT_FILTER_ALPHA * (currentTiltDegrees - filteredTiltDegrees);
        }

        const int32_t midiValue = wheelMidiValue(normalizedWheelPosition());
        sendWheelValue(midiValue);

        const uint32_t now = millis();
        if (static_cast<int32_t>(now - nextDisplayRefreshMs) >= 0)
        {
            drawStatus(midiValue);
            nextDisplayRefreshMs = now + DISPLAY_REFRESH_MS;
        }
    }

    delay(5);
}
