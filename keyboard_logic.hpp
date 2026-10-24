#pragma once

// This header contains the drone-command logic only.
// It does not depend on Windows, ROS, PX4, ArduPilot, or a particular flight board.
// Keeping it separate lets us test the keyboard mapping without any hardware.
namespace uas
{
    // These are example command limits for the keyboard prototype.
    // They are design choices, not limits supplied by the club or an aircraft.
    // The unit is part of each name: degrees for angles, degrees/second for rates.
    inline constexpr double max_tilt_deg = 10.0;
    inline constexpr double max_yaw_rate_deg_s = 30.0;

    // A bool remembers whether one key is currently being held.
    // The {} initializers make every key false when a Keys object is created.
    struct Keys
    {
        bool w = false;
        bool s = false;
        bool a = false;
        bool d = false;
        bool q = false;
        bool e = false;
    };

    // This object carries THREE output values from one function call.
    // These values are REQUESTS, not measured aircraft attitude or motor power.
    //
    // Coordinate convention chosen for this module: body X forward, Y right,
    // Z down. Positive roll tilts right; positive pitch raises the nose;
    // positive yaw rate turns the nose right, clockwise when viewed from above.
    // A later flight-controller adapter must match this convention and units.
    struct PyrCommand
    {
        double roll_deg = 0.0;          // Requested roll attitude angle.
        double pitch_deg = 0.0;         // Requested pitch attitude angle.
        double yaw_rate_deg_s = 0.0;    // Requested angular rate about body Z.
    };

    // Return true when the key belongs to our six flight controls.
    // Windows supplies uppercase letter codes even when Shift is not pressed.
    // This function uses ordinary characters so it can also run in unit tests.
    inline bool set_key(Keys & keys, unsigned key, bool pressed)
    {
        switch (key)
        {
        case 'W': keys.w = pressed; return true;
        case 'S': keys.s = pressed; return true;
        case 'A': keys.a = pressed; return true;
        case 'D': keys.d = pressed; return true;
        case 'Q': keys.q = pressed; return true;
        case 'E': keys.e = pressed; return true;
        default: return false; // An unrelated key does not change the controls.
        }
    }

    inline void release_all(Keys & keys)
    {
        keys = Keys{}; // Replace the old key states with six false values.
    }

    // The const reference means we read the key states without copying/changing
    // them. The returned struct contains roll, pitch, AND yaw-rate commands.
    inline PyrCommand make_command(const Keys & keys, bool window_has_focus)
    {
        if (!window_has_focus)
        {
            return PyrCommand{}; // No keyboard control while another app is active.
        }

        // A bool converts to 1 for true and 0 for false.
        // Subtracting opposite keys produces -1, 0, or +1.
        // Both pressed: 1 - 1 = 0, so opposing commands cancel.
        const int roll_direction = static_cast<int>(keys.d) - static_cast<int>(keys.a);
        const int pitch_direction = static_cast<int>(keys.s) - static_cast<int>(keys.w);
        const int yaw_direction = static_cast<int>(keys.e) - static_cast<int>(keys.q);

        PyrCommand command;
        command.roll_deg = roll_direction * max_tilt_deg;
        command.pitch_deg = pitch_direction * max_tilt_deg;
        command.yaw_rate_deg_s = yaw_direction * max_yaw_rate_deg_s;
        return command;
    }
}
