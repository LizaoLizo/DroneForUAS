#include <cmath>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <windows.h>
#include <fstream>

// Link the Windows keyboard library
#pragma comment(lib, "user32.lib")

// Move one number toward a target without passing it.
// Invalid input returns zero. A zero max_change is valid and preserves current.
double move_towards(double current, double target, double max_change);
double axis_command(bool negative_pressed, bool positive_pressed, double magnitude);

int main()
{
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "A/D = Roll, W/S = Pitch, Q/E = Yaw rate. ESC exits.\n";
    std::cout << "Release keys to ramp toward zero. No Enter is needed.\n";
    std::cout << "Each pair is OUTPUT / TARGET.\n";
    std::cout << "Roll/Pitch: deg. Yaw rate: deg/s.\n";
    std::cout << "These are command values, not aircraft measurements.\n\n";

    // Each output remembers its own value between loop iterations.
    double current_roll = 0.0;
    double current_pitch = 0.0;
    double current_yaw_rate = 0.0;

    // Target magnitudes. Yaw uses ANGULAR RATE, not an angle.
    const double roll_limit_deg = 10.0;
    const double pitch_limit_deg = 10.0;
    const double yaw_limit_deg_s = 30.0;

    // Limits on how quickly the COMMAND VALUES may change.
    const double roll_slew_deg_s = 10.0;
    const double pitch_slew_deg_s = 10.0;
    const double yaw_slew_deg_s2 = 60.0;

    // Open one CSV file for this run. A new run replaces the previous log.
    std::ofstream log_file("keyboard_log.csv", std::ios::trunc);
    if (!log_file)
    {
        std::cerr << "Could not open keyboard_log.csv.\n";
        return 1;
    }

    // File formatting is separate from the console formatting.
    log_file << std::fixed << std::setprecision(6);

    // Write the column names once, before entering the loop.
    log_file << "elapsed_s,dt_s,"
         << "target_roll_deg,output_roll_deg,"
         << "target_pitch_deg,output_pitch_deg,"
         << "target_yaw_rate_deg_s,output_yaw_rate_deg_s\n";

    using Clock = std::chrono::steady_clock;
    const auto start_time = Clock::now();
    auto previous_time = start_time;

    while (true)
    {
        if (GetAsyncKeyState(VK_ESCAPE) < 0)
        {
            break;
        }

        // Read a new set of physical key states every iteration.
        bool a_pressed = (GetAsyncKeyState('A') < 0);
        bool d_pressed = (GetAsyncKeyState('D') < 0);
        bool w_pressed = (GetAsyncKeyState('W') < 0);
        bool s_pressed = (GetAsyncKeyState('S') < 0);
        bool q_pressed = (GetAsyncKeyState('Q') < 0);
        bool e_pressed = (GetAsyncKeyState('E') < 0);

        // Reuse ONE function to choose three independent targets.
        double target_roll =
            axis_command(a_pressed, d_pressed, roll_limit_deg);

        double target_pitch =
            axis_command(w_pressed, s_pressed, pitch_limit_deg);

        double target_yaw_rate =
            axis_command(q_pressed, e_pressed, yaw_limit_deg_s);

        // Measure one elapsed interval and use it for all three axes.
        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - previous_time).count();
        previous_time = now;

        // Convert each command-change rate into a per-update step.
        double roll_step = roll_slew_deg_s * dt;
        double pitch_step = pitch_slew_deg_s * dt;
        double yaw_rate_step = yaw_slew_deg_s2 * dt;

        // Save each new output for the next iteration.
        current_roll =
            move_towards(current_roll, target_roll, roll_step);

        current_pitch =
            move_towards(current_pitch, target_pitch, pitch_step);

        current_yaw_rate =
            move_towards(current_yaw_rate, target_yaw_rate, yaw_rate_step);

        // Record the result of THIS update after applying smoothing.
    double elapsed =
        std::chrono::duration<double>(now - start_time).count();

    log_file << elapsed << ',' << dt << ','
         << target_roll << ',' << current_roll << ','
         << target_pitch << ',' << current_pitch << ','
         << target_yaw_rate << ',' << current_yaw_rate << '\n';

    if (!log_file)
    {
        std::cerr << "\nCould not write the CSV log.\n";
        return 1;
    }  

        std::cout << "\rRoll " << std::setw(6) << current_roll
                  << '/' << std::setw(6) << target_roll
                  << " | Pitch " << std::setw(6) << current_pitch
                  << '/' << std::setw(6) << target_pitch
                  << " | Yaw " << std::setw(6) << current_yaw_rate
                  << '/' << std::setw(6) << target_yaw_rate
                  << "        " << std::flush;

        Sleep(50);
    }

    // Finish buffered writes and check for a closing error
    log_file.close();
    if (!log_file)
    {
        std::cerr << "\nCould not finish writing the CSV log.\n";
        return 1;
    }

    std::cout << "\nSaved keyboard_log.csv in the working folder.\n";
    std::cout << "Program stopped.\n";
    return 0;
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

// Convert one pair of opposite keys into a target value.
// The caller chooses the positive magnitude and its units.
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
        // Neither key or both keys requests zero.
        return 0.0;
    }
}

