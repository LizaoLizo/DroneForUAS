#include <winsock2.h>     // Receive Network UDP Windowss
#include <windows.h>     // Check keyboard input run sleep()

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")    // Tell MSVC link the Windows network 
#pragma comment(lib, "User32.lib")     // Link make the program find Windows API 

static_assert(
    sizeof(float) == 4 &&
    sizeof(std::int32_t) == 4,
    "X-Plane payloads payloads require four-byte numbers."
);

using Clock = std::chrono::steady_clock;

const char* const roll_input_path =
    "sim/joystick/yoke_roll_ratio";

const char* const roll_override_path =
    "sim/operation/override/override_joystick_roll";

const char* const pitch_input_path =
    "sim/joystick/yoke_pitch_ratio";

const char* const pitch_override_path =
    "sim/operation/override/override_joystick_pitch";

const char* const yaw_input_path =
    "sim/joystick/yoke_heading_ratio";

const char* const yaw_override_path =
    "sim/operation/override/override_joystick_heading";

const char* const throttle_input_path =
    "sim/cockpit2/engine/actuators/throttle_ratio_all";

// 1. GROUP RELATED VALUES

struct KeyInput
{
    bool space = false;

    bool a = false;
    bool d = false;

    bool w = false;
    bool s = false;

    bool q = false;
    bool e = false;

    bool throttle_up = false;
    bool throttle_down = false;
};

struct AxisUpdate
{
    bool send = false;

    double target = 0.0;
    double output = 0.0;
};

struct RpyUpdate
{
    bool send = false;

    AxisUpdate roll;
    AxisUpdate pitch;
    AxisUpdate yaw;
};

struct AxisFrame     // What data in this packet
{
    bool has_input = false;
    float input_ratio = 0.0F;

    bool has_angle = false;
    float angle_deg = 0.0F;
};

struct RpyFrame
{
    AxisFrame roll;
    AxisFrame pitch;
    AxisFrame yaw;

    bool has_throttle = false;

    std::array<float, 4> throttle_ratios = {};     // This is used to store the four motors
};

struct AxisFeedback     // What is received and stored
{
    bool have_input = false;
    bool have_angle = false;

    float input_ratio = 0.0F;
    float angle_deg = 0.0F;

    Clock::time_point last_input = {};
    Clock::time_point last_angle = {};

    unsigned samples = 0;
};

struct ThrottleUpdate     // This is used to store the calculated throttle in this time
{
    bool send = false;
    double output = 0.0;
};

struct ThrottleFeedback     // Store the throttle value from X-Plane and update the time
{
    bool have_input = false;

    std::array<float, 4> input_ratios = {};

    Clock::time_point last_input = {};

    unsigned samples = 0;
};

struct ThrottleState     // Store the throttle settings in program self memory and check whether the ready
{
    double value = 0.0;

    bool ready = false;
    bool previous_space = false;

    Clock::time_point requested = {};
};

struct QuadUpdate     // Collect data all
{
    bool send = false;

    RpyUpdate axes;

    ThrottleUpdate throttle;
};

// CALCULATE COMMANDS WITHOUT NETWORKING

double axis_command(bool negative, bool positive, double magnitude)
{
    if (negative && !positive)
    {
        return -magnitude;
    }

    if (positive && !negative)
    {
        return magnitude;
    }

    return 0.0;
}

// Slew-Rate Limiter
double move_towards(double current, double target, double maximum_change)     // Smooth Control
{
    const double difference = target - current;

    if (difference > maximum_change)
    {
        return current + maximum_change;
    }

    if (difference < -maximum_change)
    {
        return current - maximum_change;
    }

    return target;
}

AxisUpdate calculate_axis(bool enabled, bool negative, bool positive, double current, double dt)
{
    AxisUpdate update;

    if (!enabled || !std::isfinite(current)||std::abs(current) > 0.25 || !std::isfinite(dt) || dt < 0.0)
    {
        return update;
    }

    if (dt > 0.1)
    {
        dt = 0.1;
    }

    update.send = true;

    update.target = axis_command(negative, positive, 0.25);

    update.output = move_towards(current, update.target, 0.50*dt);

    return update;
}

RpyUpdate calculate_rpy(const KeyInput & keys, bool roll_fresh,
                        bool pitch_fresh, bool yaw_fresh,
                        double current_roll, double current_pitch,
                        double current_yaw, double dt)
{
    const bool enabled = keys.space && roll_fresh && pitch_fresh && yaw_fresh;

    const AxisUpdate roll = calculate_axis(enabled, keys.a, keys.d, current_roll, dt);

    const AxisUpdate pitch = calculate_axis(enabled, keys.w, keys.s, current_pitch, dt);

    const AxisUpdate yaw = calculate_axis(enabled, keys.q, keys.e, current_yaw, dt);

    if (!roll.send || !pitch.send || !yaw.send)
    {
        return {};
    }

    RpyUpdate update;

    update.send = true;
    update.roll = roll;
    update.pitch = pitch;
    update.yaw = yaw;

    return update;
}

ThrottleUpdate calculate_throttle(bool enabled, bool increase, bool decrease, double current, double dt)
{
    ThrottleUpdate update;

    if (!std::isfinite(current) || current < 0.0 || current > 1.0)
    {
        return update;
    }

    update.output = current;

    if (!enabled || !std::isfinite(dt) || dt < 0.0)
    {
        return update;
    }

    if (dt > 0.1)
    {
        dt = 0.1;
    }

    update.send = true;

    const double rate = 0.10;

    if (increase && !decrease)
    {
        update.output += rate * dt;
    }
    else if (decrease && !increase)
    {
        update.output -= rate * dt;
    }

    if (update.output > 1.0)
    {
        update.output = 1.0;
    }

    if (update.output < 0.0)
    {
        update.output = 0.0;
    }

    return update;
}

QuadUpdate calculate_quad(
    const KeyInput & keys,
    bool roll_fresh,
    bool pitch_fresh,
    bool yaw_fresh,
    bool throttle_ready,
    double current_roll,
    double current_pitch,
    double current_yaw,
    double current_throttle,
    double dt
)
{
    KeyInput gated_keys = keys;     // Copy the last keyboard state

    gated_keys.space = keys.space && roll_fresh && pitch_fresh && yaw_fresh && throttle_ready;

    QuadUpdate update;

    update.axes = calculate_rpy(gated_keys, roll_fresh, pitch_fresh, yaw_fresh, current_roll, current_pitch, current_yaw, dt);

    update.throttle = calculate_throttle(gated_keys.space, keys.throttle_up, keys.throttle_down, current_throttle, dt);

    update.send = update.axes.send && update.throttle.send;

    if (!update.send)
    {
        update.axes = {};

        update.throttle = calculate_throttle(false, false, false,
            current_throttle, dt);
    }

    return update;
}

// CREATE DREF DATA PACKET

// BUILD AND SEND DREF PROTOCOL

bool make_dref_packet(const char * path, float value, std::array<char, 509> & packet)     // Create Data Packets
{
    if (path == nullptr || !std::isfinite(value))
    {
        return false;
    }

    const std::size_t length = std::strlen(path);

    if (length == 0 || length >= 500)
    {
        return false;
    }

    packet.fill(' ');

    std::memcpy(packet.data(), "DREF", 4);
    packet[4] = '\0';

    std::memcpy(packet.data() + 5, &value, sizeof(value));

    std::memcpy(packet.data() + 9, path, length);

    packet[9 + length] = '\0';

    return true;
}

bool send_dref(SOCKET channel, const sockaddr_in & destination, const char * path, float value)
{
    std::array<char, 509> packet;

    if (!make_dref_packet(path, value, packet))
    {
        return false;
    }

    const int sent = sendto(channel, packet.data(), static_cast<int>(packet.size()), 0, reinterpret_cast<const sockaddr *> (&destination), static_cast<int>(sizeof(destination)));

    return sent == static_cast<int>(packet.size());
}


// Send the control one axis
bool send_axis(SOCKET channel, const sockaddr_in & destination, const char * input_path, const char * override_path, double output, bool & attempted)
{
    attempted = true;

    if (!send_dref(channel, destination, override_path, 1.0F))
    {
        return false;
    }

    return send_dref(channel, destination, input_path, static_cast<float>(output));
}

bool submit_quad(SOCKET channel, const sockaddr_in & destination, const QuadUpdate & update, bool & roll_attempted, bool & pitch_attempted, bool & yaw_attempted)
{
    if (!update.send)
    {
        return true;
    }

    return send_axis(
        channel, destination,
        roll_input_path, roll_override_path,
        update.axes.roll.output, roll_attempted
    ) &&
    send_axis(
        channel, destination,
        pitch_input_path, pitch_override_path,
        update.axes.pitch.output, pitch_attempted
    ) &&
    send_axis(
        channel, destination,
        yaw_input_path, yaw_override_path,
        update.axes.yaw.output, yaw_attempted
    ) &&
    send_dref(
        channel, destination,
        throttle_input_path,
        static_cast<float>(update.throttle.output)
    );
}

bool release_axis(SOCKET channel, const sockaddr_in & destination, const char * input_path, const char * override_path)
{
    bool queued = true;

    for (int attemp = 0; attemp < 3; ++attemp)
    {
        if (!send_dref(channel, destination, input_path, 0.0F))
        {
            queued = false;
        }

        if (!send_dref(channel, destination, override_path, 0.0F))
        {
            queued = false;
        }

        Sleep(10);
    }

    return queued;
}

// This functuion used to control motor
bool release_attempted_axes(
    SOCKET channel,
    const sockaddr_in& destination,
    bool& roll_attempted,
    bool& pitch_attempted,
    bool& yaw_attempted
)
{
    bool queued = true;

    if (roll_attempted)
    {
        if (release_axis(
            channel, destination,
            roll_input_path, roll_override_path))
        {
            roll_attempted = false;
        }
        else
        {
            queued = false;
        }
    }

    if (pitch_attempted)
    {
        if (release_axis(
            channel, destination,
            pitch_input_path, pitch_override_path))
        {
            pitch_attempted = false;
        }
        else
        {
            queued = false;
        }
    }

    if (yaw_attempted)
    {
        if (release_axis(
            channel, destination,
            yaw_input_path, yaw_override_path))
        {
            yaw_attempted = false;
        }
        else
        {
            queued = false;
        }
    }

    return queued;
}

// UDP RECEIVE AND ANALYSIS

// ANALYSE X-Plane DATA PACKEGE

// READ FEEDBACK AND ANGLES

    bool extract_rpy_frame(
        const char* buffer,
        int byte_count,
        RpyFrame& result
     ) 
    {
        if (buffer == nullptr ||
            byte_count < 41 ||
            std::memcmp(buffer, "DATA", 4) != 0 ||
            (byte_count - 5) % 36 != 0)
        {
            return false;
        }

            RpyFrame frame;

            for (int offset = 5;
             offset < byte_count;
             offset += 36)
        {
             std::int32_t index = 0;

             std::memcpy(
                &index,
                buffer + offset,
                sizeof(index)
            );

            if (index != 8 && index != 17 && index != 25)
            {
                continue;
            }

            float values[8] = {};

            std::memcpy(
                values,
                buffer + offset + 4,
                sizeof(values)
            );

            if (index == 25)
            {
                bool valid = true;

                for (int engine = 0; engine < 4; ++engine)
                {
                    const float value = values[engine];

                    if (!std::isfinite(value) ||
                        value < 0.0F ||
                        value > 1.0F)
                {
                    valid = false;
                }
                }

            if (valid)
            {
                frame.has_throttle = true;

                for (std::size_t engine = 0;
                     engine < 4;
                     ++engine)
                {
                    frame.throttle_ratios[engine] =
                        values[engine];
                }
            }

            continue;
        }

            const float pitch = values[0];
            const float roll = values[1];
            const float yaw_or_heading = values[2];

            if (index == 8)
            {
                if (std::isfinite(pitch) &&
                    std::abs(pitch) <= 1.0F)
                {
                    frame.pitch.has_input = true;
                    frame.pitch.input_ratio = pitch;
                }

                if (std::isfinite(roll) &&
                    std::abs(roll) <= 1.0F)
                {
                    frame.roll.has_input = true;
                    frame.roll.input_ratio = roll;
                }

                if (std::isfinite(yaw_or_heading) &&
                    std::abs(yaw_or_heading) <= 1.0F)
                {
                    frame.yaw.has_input = true;
                    frame.yaw.input_ratio = yaw_or_heading;
                }
            }
             else
            {
                if (std::isfinite(pitch) &&
                    std::abs(pitch) <= 180.0F)
                {
                    frame.pitch.has_angle = true;
                    frame.pitch.angle_deg = pitch;
                }

                if (std::isfinite(roll) &&
                std::abs(roll) <= 180.0F)
                {
                    frame.roll.has_angle = true;
                    frame.roll.angle_deg = roll;
                }

                if (std::isfinite(yaw_or_heading) &&
                    yaw_or_heading >= 0.0F &&
                    yaw_or_heading <= 360.0F)
                {
                    frame.yaw.has_angle = true;
                    frame.yaw.angle_deg = yaw_or_heading;
                }
            }
        }

        if (!frame.roll.has_input &&
            !frame.pitch.has_input &&
            !frame.yaw.has_input &&
            !frame.roll.has_angle &&
            !frame.pitch.has_angle &&
            !frame.yaw.has_angle &&
            !frame.has_throttle)
        {
            return false;
        }

        result = frame;

        return true;
}

// Save one axis history feedback
void remember_feedback(const AxisFrame & frame, AxisFeedback & feedback, Clock::time_point received_at)
{
    if (frame.has_input)
    {
        feedback.have_input = true;
        feedback.input_ratio = frame.input_ratio;
        feedback.last_input = received_at;
        ++feedback.samples;
    }

    if (frame.has_angle)
    {
        feedback.have_angle = true;
        feedback.angle_deg = frame.angle_deg;
        feedback.last_angle = received_at;
    }
}

// Check whether the feedback is overdated
bool input_is_fresh(const AxisFeedback & feedback, Clock::time_point now)
{
    return feedback.have_input && (std::chrono::duration<double>(now - feedback.last_input).count() < 1.0);
}

// Save Throttle Feedback

void remember_throttle(const RpyFrame & frame, ThrottleFeedback & feedback, Clock::time_point received_at)
{
    if (!frame.has_throttle)
    {
        return;
    }

    feedback.have_input = true;
    feedback.input_ratios = frame.throttle_ratios;
    feedback.last_input = received_at;
    ++feedback.samples;
}

// Check whether the throttle feedback is overdated
bool throttle_is_fresh(const ThrottleFeedback & feedback, Clock::time_point now)
{
    return feedback.have_input && std::chrono::duration<double>(now - feedback.last_input).count() < 1.0;
}

// Check throttle
void synchronize_throttle(bool space, bool feedback_fresh, const ThrottleFeedback & feedback, Clock::time_point now, ThrottleState & state)
{
    if (!space)
    {
        state.ready = false;
        state.previous_space = false;
        return;
    }

    if (!state.previous_space || !feedback_fresh)
    {
        state.ready = false;
        state.requested = now;
    }

    state.previous_space = true;

    if (state.ready || !feedback_fresh || !feedback.have_input || feedback.last_input <= state.requested)
    {
        return;
    }

    double lowest = 1.0;
    double highest = 0.0;
    double total = 0.0;

    for (float value : feedback.input_ratios)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
        {
            return;
        }

        if (value < lowest)
        {
            lowest = value;
        }

        if (value > highest)
        {
            highest = value;
        }

        total += value;
    }

    if (highest - lowest > 0.02)     // Only sum if the highest - lowest <= 0.02
    {
        return;
    }

    state.value = total / 4.0;
    state.ready = true;
}

// Connect the keyboard control and the calculation
QuadUpdate update_quad(
    const KeyInput& keys,
    bool roll_fresh,
    bool pitch_fresh,
    bool yaw_fresh,
    const ThrottleFeedback& throttle_feedback,
    Clock::time_point now,
    ThrottleState& state,
    double current_roll,
    double current_pitch,
    double current_yaw,
    double dt
)
{
    const bool throttle_fresh =
        throttle_is_fresh(throttle_feedback, now);

    const bool all_feedback_fresh =
        roll_fresh &&
        pitch_fresh &&
        yaw_fresh &&
        throttle_fresh;

    synchronize_throttle(
        keys.space,
        all_feedback_fresh,
        throttle_feedback,
        now,
        state
    );

    QuadUpdate update = calculate_quad(
        keys,
        roll_fresh,
        pitch_fresh,
        yaw_fresh,
        throttle_fresh && state.ready,
        current_roll,
        current_pitch,
        current_yaw,
        state.value,
        dt
    );

    if (update.send)
    {
        state.value = update.throttle.output;
    }

    return update;
}

// Print feedback
void print_axis(
    const char* name,
    const char* angle_label,
    const AxisUpdate& update,
    const AxisFeedback& feedback,
    Clock::time_point now
)
{
    std::cout << " | " << name
              << " target=" << update.target
              << " tx=" << update.output
              << " rx=";

    if (input_is_fresh(feedback, now))
    {
        std::cout << feedback.input_ratio;
    }
    else
    {
        std::cout << "STALE";
    }

    std::cout << ' ' << angle_label << '=';

    const bool angle_fresh =
        feedback.have_angle &&
        std::chrono::duration<double>(
            now - feedback.last_angle
        ).count() < 1.0;

    if (angle_fresh)
    {
        std::cout << feedback.angle_deg;
    }
    else
    {
        std::cout << "STALE";
    }
}

int main()
{
    // SETTING UDP SOCKET
    const unsigned short listener_port = 49005;     // Receive X-Plane DATA
    const unsigned short simulator_port = 49000;     // Send to X-Plane

    WSADATA winsock = {};

    const int startup = WSAStartup(MAKEWORD(2, 2), &winsock);

    if (startup != 0)
    {
        std::cerr << "WSAStartup failed: " << startup << '\n';

        return 1;
    }

    // Create UDP socket
    SOCKET channel = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);     // AF_INET: Using IPv4 SOCK_DGRAM: Data report socket IPPROTO_UDP: UDP protocol

    if (channel == INVALID_SOCKET)
    {
        std::cerr << "socket failed: " << WSAGetLastError() << '\n';

        WSACleanup();

        return 1;
    }

    u_long nonblocking = 1;

    if (ioctlsocket(channel, FIONBIO, &nonblocking) == SOCKET_ERROR)
    {
        std::cerr << "Nonblocking setup failed: " << WSAGetLastError() << '\n';

        closesocket(channel);
        WSACleanup();

        return 1;
    }

     sockaddr_in local = {};     // Used to save local address and port

    local.sin_family = AF_INET;

    local.sin_port = htons(listener_port);

    local.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);

    if (bind(
        channel,
        reinterpret_cast<const sockaddr*>(&local),
        static_cast<int>(sizeof(local))
    ) == SOCKET_ERROR)
    {
        std::cerr << "bind failed: "
                  << WSAGetLastError()
                  << ". Stop the previous receiver before retrying.\n";

        closesocket(channel);
        WSACleanup();

        return 1;
    }

    // Set up the destination address for sending data
     sockaddr_in simulator = {};

    simulator.sin_family = AF_INET;

    simulator.sin_port = htons(simulator_port);

    simulator.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);

        std::cout << std::fixed
              << std::setprecision(3);

    std::cout << "QUADCOPTER KEYBOARD: Roll + Pitch + Yaw + common throttle.\n";
    std::cout << "Hold SPACE. A/D=Roll; W/S=Pitch; Q/E=Yaw. ESC exits.\n";
    std::cout << "W requests negative Pitch; S requests positive Pitch.\n";
    std::cout << "Q requests left Yaw; E requests right Yaw.\n";
    std::cout << "Release SPACE to send neutral and hand all axes back.\n";
    std::cout << "SPACE+T increases throttle; SPACE+G decreases it.\n";
    std::cout << "Releasing T/G holds throttle. Space release DOES NOT cut throttle.\n";
    std::cout << "Start on the ground with ALL four throttle handles at zero.\n";
    std::cout << "Hold Space to sync and enable.\n";
    std::cout << "Use G to reduce throttle before exiting with Escape.\n\n";

    // Create Reiceive buffer, feedback state and control memory
    std::array<char, 65536> buffer = {};     // Store Original UDP data space

    double current_roll = 0.0;
    double current_pitch = 0.0;
    double current_yaw = 0.0;

    AxisFeedback roll_feedback;
    AxisFeedback pitch_feedback;
    AxisFeedback yaw_feedback;

    ThrottleFeedback throttle_feedback;
    ThrottleState throttle_state;

    bool roll_attempted = false;
    bool pitch_attempted = false;
    bool yaw_attempted = false;

    int exit_code = 0;

    const auto started = Clock::now();
    auto previous_update = started;

    auto last_display = started - std::chrono::seconds(1);

     while (true)
    {
        if (GetAsyncKeyState(VK_ESCAPE) < 0)
        {
            break;
        }

        // Receive available datagrams.
        for (int packet_number = 0;
             packet_number < 64;
             ++packet_number)
        {
            const int size = recvfrom(
                channel,
                buffer.data(),
                static_cast<int>(buffer.size()),
                0,
                nullptr,
                nullptr
            );

            if (size == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();

                if (error != WSAEWOULDBLOCK)
                {
                    std::cerr << "recvfrom failed: "
                              << error << '\n';

                    exit_code = 1;
                }

                break;
            }

            RpyFrame frame;

            if (!extract_rpy_frame(
                buffer.data(), size, frame))
            {
                continue;
            }

            const auto received_at = Clock::now();

            remember_feedback(
                frame.roll,
                roll_feedback,
                received_at
            );

            remember_feedback(
                frame.pitch,
                pitch_feedback,
                received_at
            );

            remember_feedback(
                frame.yaw,
                yaw_feedback,
                received_at
            );

            remember_throttle(
                frame,
                throttle_feedback,
                received_at
            );
        }

        if (exit_code != 0)
        {
            break;
        }

    const auto now = Clock::now();

    const double dt = std::chrono::duration<double>(now - previous_update).count();

    previous_update = now;

    // Read Keyboard
    KeyInput keys;

        keys.space = GetAsyncKeyState(VK_SPACE) < 0;

        keys.a = GetAsyncKeyState('A') < 0;
        keys.d = GetAsyncKeyState('D') < 0;

        keys.w = GetAsyncKeyState('W') < 0;
        keys.s = GetAsyncKeyState('S') < 0;

        keys.q = GetAsyncKeyState('Q') < 0;
        keys.e = GetAsyncKeyState('E') < 0;

        keys.throttle_up =
            GetAsyncKeyState('T') < 0;

        keys.throttle_down =
            GetAsyncKeyState('G') < 0;

    // Check Feedback and calculate commands

    const bool roll_fresh =
            input_is_fresh(roll_feedback, now);

        const bool pitch_fresh =
            input_is_fresh(pitch_feedback, now);

        const bool yaw_fresh =
            input_is_fresh(yaw_feedback, now);

        const bool throttle_fresh =
            throttle_is_fresh(throttle_feedback, now);

        const QuadUpdate controls = update_quad(
            keys,
            roll_fresh,
            pitch_fresh,
            yaw_fresh,
            throttle_feedback,
            now,
            throttle_state,
            current_roll,
            current_pitch,
            current_yaw,
            dt
        );

        const RpyUpdate& update = controls.axes;

    // Submit or release the commands

      if (update.send)
        {
            if (!submit_quad(
                channel,
                simulator,
                controls,
                roll_attempted,
                pitch_attempted,
                yaw_attempted
            ))
            {
                std::cerr << "Axis send failed: "
                          << WSAGetLastError() << '\n';

                exit_code = 1;
                break;
            }
        }
        else
        {
            if (!release_attempted_axes(
                channel,
                simulator,
                roll_attempted,
                pitch_attempted,
                yaw_attempted
            ))
            {
                std::cerr
                    << "Some release messages could not be queued.\n";

                exit_code = 1;
                break;
            }
        }

        // Store axis output

        current_roll = update.roll.output;
        current_pitch = update.pitch.output;
        current_yaw = update.yaw.output;

            if (std::chrono::duration<double>(
                now - last_display
            ).count() >= 0.2)
        {
            const bool all_fresh =
                roll_fresh &&
                pitch_fresh &&
                yaw_fresh &&
                throttle_fresh;

            std::cout << (
                controls.send
                ? "SENDING"
                : (
                    all_fresh
                    ? (keys.space ? "SYNC_THROTTLE" : "READY")
                    : "WAIT_INPUT"
                  )
            );

            print_axis(
                "ROLL", "bank_deg",
                update.roll,
                roll_feedback,
                now
            );

            print_axis(
                "PITCH", "pitch_deg",
                update.pitch,
                pitch_feedback,
                now
            );

            print_axis(
                "YAW", "true_heading_deg",
                update.yaw,
                yaw_feedback,
                now
            );

            std::cout
                << " | THROTTLE out="
                << throttle_state.value
                << " rx[1..4]=";

            if (throttle_fresh)
            {
                for (std::size_t engine = 0;
                     engine < 4;
                     ++engine)
                {
                    if (engine != 0)
                    {
                        std::cout << '/';
                    }

                    std::cout
                        << throttle_feedback.input_ratios[engine];
                }
            }
            else
            {
                std::cout << "STALE";
            }

            std::cout
                << " | samples R/P/Y/T="
                << roll_feedback.samples << '/'
                << pitch_feedback.samples << '/'
                << yaw_feedback.samples << '/'
                << throttle_feedback.samples
                << '\n';

            last_display = now;
        }

         Sleep(50);
    }


    // Clean Exit

     if (!release_attempted_axes(
        channel,
        simulator,
        roll_attempted,
        pitch_attempted,
        yaw_attempted
    ))
    {
        std::cerr
            << "Cleanup messages could not all be queued.\n";

        exit_code = 1;
    }

    closesocket(channel);
    WSACleanup();

     std::cout
        << "\nQuad keyboard stopped. Input samples R/P/Y/T: "
        << roll_feedback.samples << '/'
        << pitch_feedback.samples << '/'
        << yaw_feedback.samples << '/'
        << throttle_feedback.samples
        << '\n';

    std::cout
        << "Exiting does NOT reset throttle. Native X-Plane F1 can lower it.\n";

    if (!roll_feedback.have_input ||
        !pitch_feedback.have_input ||
        !yaw_feedback.have_input ||
        !throttle_feedback.have_input)
    {
        std::cerr
            << "Missing valid feedback. Enable UDP rows 8 and 25.\n";

        return 1;
    }

    return exit_code;

}








