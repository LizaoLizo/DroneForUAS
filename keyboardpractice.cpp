#include <iostream>
#include <windows.h>

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

// Calculate the command for one axis
// magnitude must be positive; function chooses sign

double axis_command(bool negative_pressed, bool positive_pressed, double magnitude);

// These queries read key states
KeyboardInput read_keyboard();

// Receive an input object and return an output object
// inputs used to test logics
PyrCommand calculate_command(KeyboardInput input);

// Recieve copies of data and display
void display_command(KeyboardInput input, PyrCommand command);

// Check logic

bool run_logic_checks();

int main()
{
    // Display bool value as True / False instead of 1 / 0
    std::cout << std::boolalpha;

    // Run checks before the loop
    if (!run_logic_checks())
    {
        std::cout << "Logic checks failed. The keyboard loop was not started.\n";
        return 1;
    }

    std::cout << "Hold SPACE to enable command created.\n";
    std::cout << "While holding SPACE: A/D = roll, W/S = pitch, Q/E = yaw.\n";
    std::cout << "The unit of ROLL/PITCH: deg. The unit of YAW-RATE: deg/s.\n";
    std::cout << "Release SPACE to set all three commands to zero.\n";
    std::cout << "ESC exits.\n\n";

    // Keep checking the keyboard
    while (true)
    {
        keyboardInput input = read_keyboard();
        if (input.exit_pressed)
        {
            break;
        }

        PyrCommand command = calculate_command(input);
        display_command(input, command);
        Sleep(50);     // Approximately 50 milliseconds between every updates
 
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
    input.enable_pressed = (GetAsyncKeyState(VK_SPACE) < 0);
    input.exit_pressed = (GetAstncKeyState(VK_ESCAPE) < 0);
    return input;
}

PyrCommand calculate_command(KeyboardInput input)
{
    PyrCommand command;

    // Space enables LOCAL command generation. It does not arm an aircraft
    // If no space, return default zero-valued output directly
    if (!input.enable_pressed)
    {
        return command;
    }

    const double roll_limit_deg = 10.0;
    const double pitch_limit_deg = 10.0;
    const double yaw_rate_limit_deg_s = 30.0;

    command.roll_deg = axis_command(input.a_pressed, input.d_pressed, roll_limit_deg);
    command.pitch_deg = axis_command(input.w_pressed, input.s_pressed, pitch_limit_deg);
    command.yaw_rate_deg_s = axis_command(input.q_pressed, input.e_pressed, yaw_rate_limit_deg_s);
    return command;
}

void display_command(KeyboardInput input, PyrCommand command)
{
    std::cout << "\rEnable: " << input.enable_pressed
              << " Roll: "    << command.roll_deg
              << " Pitch: "   << command.pitch_deg
              << " Yaw rate: "<< command.yaw_rate_limit_deg_s
              << "         "  << std::flush;
}

bool run_logic_checks()
{
    // Four combinations of a pair of opposite keys
    bool axis_checks = axis_command(true, false, 10.0) == -10.0 &&
                       axis_command(false, true, 10.0) == 10.0  &&
                       axis_command(true, true, 10.0) == 0.0 &&
                       axis_command(false, false, 10.0) == 0.0;

    KeyboardInput sample;
    sample.a_pressed = true;
    sample.w_pressed = true;
    sample.e_pressed = true;

    PyrCommand disabled = calculate_command(sample);
    bool disabled_check = disabled.roll_deg = 0.0 &&
         disabled.pitch_deg = 0.0 && disabled.yaw_rate_deg_s = 0.0;

    smaple.enable_pressed = true;
    PyrCommand enabled = calculate_command(sample);
    bool enabled_check = enabled.roll_deg == -10.0 &&
         enabled.pitch_deg == -10.0 && enabled.yaw_rate_deg_s == 30.0;

    std::cout << "Axis checks: " << axis_checks << '\n';
    std::cout << "Disabled command check: " << disabled_check << '\n';
    std::cout << "Enabled command check: " << enabled_check << '\n';
    return axis_checks && disabled_check && enabled_check;
}
