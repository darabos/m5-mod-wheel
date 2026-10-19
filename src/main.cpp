#include <M5Unified.h>
#include <USB.h>
#include <esp32-hal-tinyusb.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32.h>

BLEMIDI_CREATE_INSTANCE("M5 Mod Wheel", MIDI)

#if !CFG_TUD_MIDI
#error "The selected ESP32-S3 framework must enable TinyUSB MIDI"
#endif

namespace
{
constexpr uint8_t MIDI_CHANNEL = 1;
constexpr uint8_t MOD_WHEEL_CC = 1;
constexpr float FULL_SCALE_TILT_DEGREES = 45.0f;
constexpr float TILT_FILTER_ALPHA = 0.2f;
constexpr uint32_t DISPLAY_REFRESH_MS = 33;
constexpr uint32_t MOD_WHEEL_COLOR = 0xC6A0F6;
constexpr uint32_t PITCH_BEND_COLOR = 0xA6E3A1;
constexpr int32_t PITCH_GUIDE_RANGE_SEMITONES = 2;
constexpr uint32_t PITCH_GUIDE_COLOR = 0x30402E;

enum class WheelMode : uint8_t
{
    ModWheel,
    PitchBend,
};

enum class MidiTransport : uint8_t
{
    None,
    Usb,
    Bluetooth,
};

volatile bool midiConnected = false;
bool bleStarted = false;
bool pairingRequested = false;
M5Canvas displayBuffer(&M5.Display);
bool displayBufferReady = false;
bool hasTiltReading = false;
bool hasZeroPose = false;
bool hasSentMidiValue = false;
bool previousMidiConnected = false;
MidiTransport activeTransport = MidiTransport::None;
WheelMode wheelMode = WheelMode::ModWheel;
float filteredTiltDegrees = 0.0f;
float zeroTiltDegrees = 0.0f;
int32_t lastSentMidiValue = 0;
uint32_t nextDisplayRefreshMs = 0;

uint16_t loadMidiDescriptor(uint8_t *destination, uint8_t *interfaceNumber)
{
    const uint8_t endpoint = tinyusb_get_free_duplex_endpoint();
    if (endpoint == 0)
    {
        return 0;
    }

    const uint8_t stringIndex = tinyusb_add_string_descriptor("M5 Mod Wheel MIDI");
    const uint8_t descriptor[TUD_MIDI_DESC_LEN] = {
        TUD_MIDI_DESCRIPTOR(
            *interfaceNumber,
            stringIndex,
            endpoint,
            static_cast<uint8_t>(0x80 | endpoint),
            64)};

    *interfaceNumber += 2;
    std::memcpy(destination, descriptor, sizeof(descriptor));
    return sizeof(descriptor);
}

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

bool startUsbMidi()
{
    if (!USB.productName("M5 Mod Wheel") ||
        !USB.manufacturerName("M5 Mod Wheel"))
    {
        Serial.println("ERROR: Could not configure USB MIDI identity");
        return false;
    }

    if (tinyusb_enable_interface(
            USB_INTERFACE_MIDI,
            TUD_MIDI_DESC_LEN,
            loadMidiDescriptor) != ESP_OK)
    {
        Serial.println("ERROR: Could not register USB MIDI interface");
        return false;
    }

    if (!USB.begin())
    {
        Serial.println("ERROR: Could not start USB MIDI");
        return false;
    }

    Serial.println("USB MIDI started");
    return true;
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
    const MidiTransport transport = tud_midi_mounted()
                                        ? MidiTransport::Usb
                                        : (bleStarted && midiConnected
                                               ? MidiTransport::Bluetooth
                                               : MidiTransport::None);
    if (transport != activeTransport)
    {
        activeTransport = transport;
        hasSentMidiValue = false;
        Serial.println(transport == MidiTransport::Usb
                           ? "MIDI output: USB"
                           : transport == MidiTransport::Bluetooth
                                 ? "MIDI output: Bluetooth"
                                 : "MIDI output: none");
    }

    if (transport == MidiTransport::None ||
        (hasSentMidiValue && midiValue == lastSentMidiValue))
    {
        return;
    }

    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint8_t cin = 0;

    if (wheelMode == WheelMode::ModWheel)
    {
        status = static_cast<uint8_t>(0xB0 | (MIDI_CHANNEL - 1));
        data1 = MOD_WHEEL_CC;
        data2 = static_cast<uint8_t>(midiValue);
        cin = 0x0B;
    }
    else
    {
        const uint16_t bend = static_cast<uint16_t>(midiValue + 8192);
        status = static_cast<uint8_t>(0xE0 | (MIDI_CHANNEL - 1));
        data1 = static_cast<uint8_t>(bend & 0x7F);
        data2 = static_cast<uint8_t>((bend >> 7) & 0x7F);
        cin = 0x0E;
    }

    if (transport == MidiTransport::Usb)
    {
        const uint8_t packet[4] = {cin, status, data1, data2};
        if (!tud_midi_packet_write(packet))
        {
            return;
        }
    }
    else if (wheelMode == WheelMode::ModWheel)
    {
        MIDI.sendControlChange(MOD_WHEEL_CC, data2, MIDI_CHANNEL);
    }
    else
    {
        MIDI.sendPitchBend(static_cast<int>(midiValue), MIDI_CHANNEL);
    }

    lastSentMidiValue = midiValue;
    hasSentMidiValue = true;
}

void drawStatus()
{
    if (!displayBufferReady)
    {
        return;
    }

    const uint32_t color = wheelMode == WheelMode::ModWheel
                               ? MOD_WHEEL_COLOR
                               : PITCH_BEND_COLOR;
    const int32_t leftWidth = 80;
    const int32_t margin = 8;
    const int32_t barHeight = 12;
    const int32_t centerY = displayBuffer.height() / 2;
    const int32_t travel = centerY - margin - barHeight / 2;
    const float position = hasZeroPose ? normalizedWheelPosition() : 0.0f;
    const int32_t barY = centerY - barHeight / 2 -
                         static_cast<int32_t>(std::lround(position * travel));

    displayBuffer.fillSprite(TFT_BLACK);
    displayBuffer.setTextColor(color, TFT_BLACK);
    displayBuffer.setTextDatum(middle_center);
    displayBuffer.setTextSize(10);
    displayBuffer.drawString(
        wheelMode == WheelMode::ModWheel ? "M" : "P",
        leftWidth / 2,
        centerY - 15);

    const char *connectionStatus = "";
    if (tud_midi_mounted())
    {
        connectionStatus = "USB";
    }
    else if (midiConnected)
    {
        connectionStatus = "CONN";
    }
    else if (pairingRequested)
    {
        connectionStatus = "PAIR";
    }

    displayBuffer.setTextSize(2);
    displayBuffer.drawString(connectionStatus, leftWidth / 2, displayBuffer.height() - 22);
    if (wheelMode == WheelMode::PitchBend)
    {
        for (int32_t semitone = -PITCH_GUIDE_RANGE_SEMITONES;
             semitone <= PITCH_GUIDE_RANGE_SEMITONES;
             ++semitone)
        {
            const float guidePosition = static_cast<float>(semitone) / PITCH_GUIDE_RANGE_SEMITONES;
            const int32_t guideY = centerY -
                                   static_cast<int32_t>(std::lround(guidePosition * travel));
            displayBuffer.drawFastHLine(
                leftWidth + margin,
                guideY,
                displayBuffer.width() - leftWidth - 2 * margin,
                PITCH_GUIDE_COLOR);
        }
    }
    displayBuffer.fillRect(
        leftWidth + margin,
        barY,
        displayBuffer.width() - leftWidth - 2 * margin,
        barHeight,
        color);
    displayBuffer.pushSprite(0, 0);
}
} // namespace

void setup()
{
    auto config = M5.config();
    M5.begin(config);

    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    displayBuffer.setColorDepth(16);
    displayBufferReady = displayBuffer.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
    if (!displayBufferReady)
    {
        Serial.println("ERROR: Could not allocate display buffer");
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.println("Display buffer allocation failed");
    }

    startUsbMidi();
    Serial.println("M5 Mod Wheel ready");
    drawStatus();
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
    }

    const uint32_t now = millis();
    if (static_cast<int32_t>(now - nextDisplayRefreshMs) >= 0)
    {
        drawStatus();
        nextDisplayRefreshMs = now + DISPLAY_REFRESH_MS;
    }

    delay(5);
}
