#include <iostream>
#include <windows.h>
#include <iomanip>
#include <cmath>
#include <chrono>

// Link the Windows keyboard library
#pragma comment(lib, "user32.lib")

// Group the INPUT values. Every key initially starts released

struct KeyboardInput
{
    bool a_pressed = false;
    bool d_pressed = false;
    bool w_pressed = false;
    bool s_pressed = false;
    bool q_pressed = false;
    bool e_pressed = false;
    bool enable_pressed = false;
    bool fine_pressed = false;
    bool exit_pressed = false;
};

// This structure groups three command values together
struct PyrCommand
{
    // Requested altitude angles, in degrees
    double roll_deg = 0.0;
    double pitch_deg = 0.0;

    // Requested yaw angular rate, in degrees per second
    double yaw_rate_deg_s = 0.0;
};

struct ControlSettings
{
    double roll_limit_deg = 10.0;
    double pitch_limit_deg = 10.0;
    double yaw_rate_limit_deg_s = 30.0;
    double fine_scale = 0.3;

    // Bounds on how quickly the OUTPUT COMMAND values change
    double roll_slew_deg_s = 10.0;
    double pitch_slew_deg_s = 10.0;
    double yaw_rate_slew_deg_s2 = 60.0;
};

// Check the consistency between the input and limit
bool settings_valid(ControlSettings settings);

// Calculate the command for one axis
// magnitude must be positive; function chooses sign

double axis_command(bool negative_pressed, bool positive_pressed, double magnitude);

// These queries read key states
KeyboardInput read_keyboard();

// Receive an input object and return an output object
// inputs used to test logics
PyrCommand calculate_command(KeyboardInput input, ControlSettings settings);

double move_towards(double current, double target, double max_change);

bool command_finite(PyrCommand command);

PyrCommand update_command(PyrCommand previous, PyrCommand target, double dt, ControlSettings settings, bool enabled);

// Recieve copies of data and display
void display_command(PyrCommand output, PyrCommand target);

// Fractional calculations should be checked with a tolerance

bool close_to(double actual, double expected);

bool command_is(PyrCommand command, double roll, double pitch, double yaw_rate);

bool run_smoothing_checks();

// Check logic

bool run_logic_checks();

int main()
{
    // Display bool value as True / False instead of 1 / 0
    std::cout << std::boolalpha << std::fixed << std::setprecision(2);

    // fixed fixture
    ControlSettings settings;
    settings.roll_limit_deg = 10.0;
    settings.pitch_limit_deg = 10.0;
    settings.yaw_rate_limit_deg_s = 30.0;
    settings.fine_scale = 0.3;
    settings.roll_slew_deg_s = 10.0;
    settings.pitch_slew_deg_s = 10.0;
    settings.yaw_rate_slew_deg_s2 = 60.0;

    if (!settings_valid(settings))
    {
        std::cout <<  "Invalid settings. Use positive finite magnitudes/rates and 0 < fine_scale <= 1.\n";
        return 1;
    }
    if (!run_logic_checks() || !run_smoothing_checks())
    {
        std::cout << "Logic checks failed. The keyboard loop was not started.\n";
        return 1;
    }

    std::cout << "\nHold SPACE to enable command created.\n";
    std::cout << "While holding SPACE: A/D = roll, W/S = pitch, Q/E = yaw.\n";
    std::cout << "The unit of ROLL/PITCH: deg. The unit of YAW-RATE: deg/s.\n";
    std::cout << "Release a direction: ramp toward zero while SPACE stays held.\n";
    std::cout << "Release SPACE to set all three commands to zero.\n";
    std::cout << "ESC exits. No aircraft is connected now\n\n";

    PyrCommand output_command;
    using Clock = std::chrono::steady_clock;
    auto previous_time = Clock::now();

    // Keep checking the keyboard
    while (true)
    {
         auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - previous_time).count();
        previous_time = now; // Update even while command generation is disabled.

        KeyboardInput input = read_keyboard();
        if (input.exit_pressed)
        {
            break;
        }

        PyrCommand target_command = calculate_command(input, settings);
        output_command = update_command(output_command, target_command, dt,
                                        settings, input.enable_pressed);
        display_command(output_command, target_command);
        Sleep(50);
 
    }

    std::cout << "\nProgram Stopped.\n";
    return 0;
}

double axis_command(bool negative_pressed, bool positive_pressed, double magnitude)
{
    if (negative_pressed && !positive_pressed)
    {
        return -magnitude;
    }
    else if (positive_pressed && !negative_pressed)
    {
        return magnitude;
    }
    else
    {
        return 0.0;
    }
}

KeyboardInput read_keyboard()
{
    KeyboardInput input;
    input.a_pressed = (GetAsyncKeyState('A') < 0);
    input.d_pressed = (GetAsyncKeyState('D') < 0);
    input.w_pressed = (GetAsyncKeyState('W') < 0);
    input.s_pressed = (GetAsyncKeyState('S') < 0);
    input.q_pressed = (GetAsyncKeyState('Q') < 0);
    input.e_pressed = (GetAsyncKeyState('E') < 0);
    input.enable_pressed = (GetAsyncKeyState(VK_SPACE) < 0);
    input.fine_pressed = (GetAsyncKeyState(VK_SHIFT) < 0);
    input.exit_pressed = (GetAsyncKeyState(VK_ESCAPE) < 0);
    return input;
}

PyrCommand calculate_command(KeyboardInput input, ControlSettings settings)
{
    PyrCommand command;

    // Space enables LOCAL command generation. It does not arm an aircraft
    // If no space, return default zero-valued output directly
    if (!input.enable_pressed || !settings_valid(settings))
    {
        return command;
    }

    double scale = 1.0;
    if (input.fine_pressed)
    {
        scale = settings.fine_scale;
    }

    // Fine control reduces target angle / rate magnitudes

    double roll_magnitude = settings.roll_limit_deg * scale;
    double pitch_magnitude = settings.pitch_limit_deg * scale;
    double yaw_rate_magnitude = settings.yaw_rate_limit_deg_s * scale;

    command.roll_deg =
    axis_command(input.a_pressed, input.d_pressed, roll_magnitude);

    command.pitch_deg =
    axis_command(input.w_pressed, input.s_pressed, pitch_magnitude);

    command.yaw_rate_deg_s =
    axis_command(input.q_pressed, input.e_pressed, yaw_rate_magnitude);
    return command;
}

double move_towards(double current, double target, double max_change)
{
    if (!std::isfinite(current) || !std::isfinite(target) ||
        !std::isfinite(max_change) || max_change < 0.0)
    {
        return 0.0;
    }

    double difference = target - current;
    if (difference > max_change)
    {
        return current + max_change;
    }
    else if (difference < -max_change)
    {
        return current - max_change;
    }
    else
    {
        return target;
    }
}

bool command_finite(PyrCommand command)
{
    return std::isfinite(command.roll_deg) &&
        std::isfinite(command.pitch_deg) &&
        std::isfinite(command.yaw_rate_deg_s);
}

// dt is elapsed SECONDS
// Disabled or invalid data produces zero directly
PyrCommand update_command(PyrCommand previous, PyrCommand target, double dt,
                          ControlSettings settings, bool enabled)
{
    PyrCommand next;
    if (!enabled || !settings_valid(settings) || !std::isfinite(dt) || dt < 0.0 ||
        !command_finite(previous) || !command_finite(target))
    {
        return next;
    }

    double roll_step = settings.roll_slew_deg_s * dt;
    double pitch_step = settings.pitch_slew_deg_s * dt;
    double yaw_rate_step = settings.yaw_rate_slew_deg_s2 * dt;

    if (!std::isfinite(roll_step) || !std::isfinite(pitch_step) ||
        !std::isfinite(yaw_rate_step))
        {
            return next;
        }

    
    next.roll_deg = move_towards(previous.roll_deg, target.roll_deg, roll_step);
    next.pitch_deg = move_towards(previous.pitch_deg, target.pitch_deg, pitch_step);
    next.yaw_rate_deg_s = move_towards(previous.yaw_rate_deg_s, target.yaw_rate_deg_s,
                                     yaw_rate_step);
    
    return next;
}                


void display_command(PyrCommand output, PyrCommand target)
{
    std::cout << "\rRoll " << output.roll_deg << '/' << target.roll_deg
              << " Pitch " << output.pitch_deg << '/' << target.pitch_deg
              << " Yaw " << output.yaw_rate_deg_s << '/' << target.yaw_rate_deg_s
              << "        " << std::flush;
}


bool settings_valid(ControlSettings settings)
{
    return std::isfinite(settings.roll_limit_deg) &&
        settings.roll_limit_deg > 0.0 &&
        std::isfinite(settings.pitch_limit_deg) &&
        settings.pitch_limit_deg > 0.0 &&
        std::isfinite(settings.yaw_rate_limit_deg_s) &&
        settings.yaw_rate_limit_deg_s > 0.0 &&
        std::isfinite(settings.fine_scale) &&
        settings.fine_scale > 0.0 &&
        settings.fine_scale <= 1.0 &&
        std::isfinite(settings.roll_slew_deg_s) && settings.roll_slew_deg_s > 0.0 &&
        std::isfinite(settings.pitch_slew_deg_s) && settings.pitch_slew_deg_s > 0.0 &&
        std::isfinite(settings.yaw_rate_slew_deg_s2) && settings.yaw_rate_slew_deg_s2 > 0.0;
}

bool close_to(double actual, double expected)
{
    return std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) < 0.000000001;
}

bool command_is(PyrCommand command, double roll, double pitch, double yaw_rate)
{
    return close_to(command.roll_deg, roll) &&
         close_to(command.pitch_deg, pitch) &&
         close_to(command.yaw_rate_deg_s, yaw_rate);
}

bool run_smoothing_checks()
{
    bool move_check = close_to(move_towards(0.0, 10.0, 0.5), 0.5) &&
                      close_to(move_towards(2.0, -2.0, 0.5), 1.5) &&
                      close_to(move_towards(9.8, 10.0, 0.5), 10.0) &&
                      close_to(move_towards(10.0, 10.0, 0.5), 10.0) &&
                      close_to(move_towards(2.0, 10.0, 0.0), 2.0);

    ControlSettings fixture;
    fixture.roll_limit_deg = 10.0;
    fixture.pitch_limit_deg = 10.0;
    fixture.yaw_rate_limit_deg_s = 30.0;
    fixture.fine_scale = 0.3;
    fixture.roll_slew_deg_s = 10.0;
    fixture.pitch_slew_deg_s = 10.0;
    fixture.yaw_rate_slew_deg_s2 = 60.0;

    PyrCommand previous;
    PyrCommand target;
    target.roll_deg = 10.0;
    target.pitch_deg = -10.0;
    target.yaw_rate_deg_s = 30.0;

    PyrCommand first = update_command(previous, target, 0.05, fixture, true);
    bool first_check = command_is(first, 0.5, -0.5, 3.0);

    PyrCommand second = update_command(first, target, 0.05, fixture, true);
    bool second_check = command_is(second, 1.0, -1.0, 6.0);

    PyrCommand no_time = update_command(second, target, 0.0, fixture, true);
    bool zero_time_check = command_is(no_time, 1.0, -1.0, 6.0);

    PyrCommand disabled = update_command(second, target, 0.05, fixture, false);
    bool disabled_check = command_is(disabled, 0.0, 0.0, 0.0);

    PyrCommand invalid_time = update_command(second, target, -0.05, fixture, true);
    bool invalid_time_check = command_is(invalid_time, 0.0, 0.0, 0.0);

    std::cout << "Move-towards checks: " << move_check << '\n';
    std::cout << "First smoothing step: " << first_check << '\n';
    std::cout << "Second smoothing step: " << second_check << '\n';
    std::cout << "Zero elapsed time: " << zero_time_check << '\n';
    std::cout << "Disabled reset: " << disabled_check << '\n';
    std::cout << "Invalid elapsed time: " << invalid_time_check << '\n';

    return move_check && first_check && second_check &&
         zero_time_check && disabled_check && invalid_time_check;
}    

bool run_logic_checks()
{
    ControlSettings fixture;
    fixture.roll_limit_deg = 10.0;
    fixture.pitch_limit_deg = 10.0;
    fixture.yaw_rate_limit_deg_s = 30.0;
    fixture.fine_scale = 0.3;
    fixture.roll_slew_deg_s = 10.0;
    fixture.pitch_slew_deg_s = 10.0;
    fixture.yaw_rate_slew_deg_s2 = 60.0;

    ControlSettings invalid = fixture;
    invalid.fine_scale = 0.0;
    bool settings_check = settings_valid(fixture) && !settings_valid(invalid);

    // Four combinations of a pair of opposite keys
    bool axis_check = axis_command(true, false, 10.0) == -10.0 &&
                       axis_command(false, true, 10.0) == 10.0  &&
                       axis_command(true, true, 10.0) == 0.0 &&
                       axis_command(false, false, 10.0) == 0.0;

    KeyboardInput sample;
    sample.enable_pressed = true;
    sample.d_pressed = true;
    sample.w_pressed = true;
    sample.e_pressed = true;

    PyrCommand normal = calculate_command(sample, fixture);
    bool normal_check = command_is(normal, 10.0, -10.0, 30.0);

    sample.fine_pressed = true;
    PyrCommand fine = calculate_command(sample, fixture);
    bool fine_check = command_is(fine, 3.0, -3.0, 9.0);
    // If release space the value change to zero even if Shift and direction keys remain held
    sample.enable_pressed = false;
    PyrCommand disabled = calculate_command(sample, fixture);
    bool disabled_check = command_is(disabled, 0.0, 0.0, 0.0);

    // Enable command generation before checking invalid settings.
    sample.enable_pressed = true;
    PyrCommand rejected = calculate_command(sample, invalid);
    bool invalid_check = command_is(rejected, 0.0, 0.0, 0.0);

    // Keep Space enabled while checking opposite keys.
    sample.a_pressed = true;
    PyrCommand cancelled = calculate_command(sample, fixture);
    bool cancel_check = command_is(cancelled, 0.0, -3.0, 9.0);

    std::cout << "Settings check: " << settings_check << '\n';
    std::cout << "Axis checks: " << axis_check << '\n';
    std::cout << "Normal command check: " << normal_check << '\n';
    std::cout << "Fine command check: " << fine_check << '\n';
    std::cout << "Disabled command check: " << disabled_check << '\n';
    std::cout << "Invalid settings check: " << invalid_check << '\n';
    std::cout << "Opposite keys check: " << cancel_check << '\n';

    return settings_check && axis_check && normal_check && fine_check &&
         disabled_check && invalid_check && cancel_check;
}


