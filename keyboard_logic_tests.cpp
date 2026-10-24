#include "keyboard_logic.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
    // Explicit checks keep running in Release builds, where assert() may be removed.
    struct TestResults
    {
        unsigned checks = 0;
        unsigned failures = 0;

        void check(bool passed, const std::string & description)
        {
            ++checks;
            if (!passed)
            {
                ++failures;
                std::cerr << "FAILED: " << description << '\n';
            }
        }

        void command(const uas::PyrCommand & actual,
                     const uas::PyrCommand & expected,
                     const std::string & description)
        {
            // Each nonzero output is exactly one configured limit or its negative,
            // so equality is intentional. This is not a general floating-point
            // comparison rule for calculations involving rounding.
            const bool passed = actual.roll_deg == expected.roll_deg
                && actual.pitch_deg == expected.pitch_deg
                && actual.yaw_rate_deg_s == expected.yaw_rate_deg_s;
            check(passed, description);
            if (!passed)
            {
                std::cerr << "  expected roll/pitch/yaw rate: "
                          << expected.roll_deg << "/" << expected.pitch_deg << "/"
                          << expected.yaw_rate_deg_s << '\n'
                          << "  actual roll/pitch/yaw rate:   "
                          << actual.roll_deg << "/" << actual.pitch_deg << "/"
                          << actual.yaw_rate_deg_s << '\n';
            }
        }
    };

    bool same_keys(const uas::Keys & left, const uas::Keys & right)
    {
        return left.w == right.w && left.s == right.s
            && left.a == right.a && left.d == right.d
            && left.q == right.q && left.e == right.e;
    }
}

int main()
{
    TestResults results;
    const uas::PyrCommand neutral{};
    const double tilt = uas::max_tilt_deg;
    const double yaw_rate = uas::max_yaw_rate_deg_s;

    uas::Keys keys;
    results.check(same_keys(keys, uas::Keys{}), "Every key starts released");
    results.command(uas::make_command(keys, true), neutral, "Startup command is zero");
    results.check(std::isfinite(tilt) && tilt > 0.0,
                  "Configured tilt limit is finite and positive");
    results.check(std::isfinite(yaw_rate) && yaw_rate > 0.0,
                  "Configured yaw-rate limit is finite and positive");

    // Each example is a separate expected answer, including the intended sign.
    // The expected values do not call the mapping function being tested.
    struct DirectionCase
    {
        unsigned key;
        uas::PyrCommand expected;
    };
    const std::array<DirectionCase, 6> directions{{
        {'W', {  0.0, -tilt,       0.0}},
        {'S', {  0.0,  tilt,       0.0}},
        {'A', {-tilt,   0.0,       0.0}},
        {'D', { tilt,   0.0,       0.0}},
        {'Q', {  0.0,   0.0, -yaw_rate}},
        {'E', {  0.0,   0.0,  yaw_rate}},
    }};

    for (const DirectionCase & example : directions)
    {
        keys = uas::Keys{};
        const std::string label(1, static_cast<char>(example.key));
        results.check(uas::set_key(keys, example.key, true), label + " is recognized");
        results.command(uas::make_command(keys, true), example.expected,
                        label + " has the correct direction and units");

        // Keyboard autorepeat may report another press before any release.
        results.check(uas::set_key(keys, example.key, true),
                      label + " repeated press is recognized");
        results.command(uas::make_command(keys, true), example.expected,
                        label + " repeated press does not increase the command");

        results.check(uas::set_key(keys, example.key, false), label + " release is recognized");
        results.command(uas::make_command(keys, true), neutral, label + " release returns to zero");
        results.check(uas::set_key(keys, example.key, false),
                      label + " repeated release is recognized");
        results.command(uas::make_command(keys, true), neutral,
                        label + " repeated release stays at zero");
    }

    // A pair may cancel while BOTH keys remain held. Releasing one key must
    // restore the other key's command, rather than incorrectly leaving zero.
    for (unsigned index = 0; index < directions.size(); index += 2)
    {
        keys = uas::Keys{};
        const DirectionCase & negative = directions[index];
        const DirectionCase & positive = directions[index + 1];
        const std::string label = std::string(1, static_cast<char>(negative.key))
            + "/" + static_cast<char>(positive.key);

        uas::set_key(keys, negative.key, true);
        uas::set_key(keys, positive.key, true);
        results.command(uas::make_command(keys, true), neutral, label + " opposite keys cancel");

        uas::set_key(keys, positive.key, false);
        results.command(uas::make_command(keys, true), negative.expected,
                        label + " releasing the positive key restores the negative key");

        uas::set_key(keys, positive.key, true);
        uas::set_key(keys, negative.key, false);
        results.command(uas::make_command(keys, true), positive.expected,
                        label + " releasing the negative key restores the positive key");
    }

    // Exhaustively cover 2^6 = 64 possible combinations of the six held keys.
    // Each axis has four independently listed answers: neither, negative,
    // positive, and both. This table is separate from the production arithmetic.
    const std::array<unsigned, 6> key_codes{{'W', 'S', 'A', 'D', 'Q', 'E'}};
    const std::array<double, 4> angle_answers{{0.0, -tilt, tilt, 0.0}};
    const std::array<double, 4> rate_answers{{0.0, -yaw_rate, yaw_rate, 0.0}};

    for (unsigned mask = 0; mask < 64; ++mask)
    {
        keys = uas::Keys{};
        for (unsigned index = 0; index < key_codes.size(); ++index)
        {
            uas::set_key(keys, key_codes[index], (mask & (1u << index)) != 0);
        }
        const uas::Keys before_reading = keys;
        const uas::PyrCommand expected{
            angle_answers[(mask >> 2) & 3u],
            angle_answers[mask & 3u],
            rate_answers[(mask >> 4) & 3u],
        };
        const std::string label = "Key combination " + std::to_string(mask);
        results.command(uas::make_command(keys, true), expected, label + " while focused");
        results.command(uas::make_command(keys, false), neutral, label + " without focus");
        results.check(same_keys(keys, before_reading), label + " reading does not change the keys");
    }

    keys = uas::Keys{};
    uas::set_key(keys, 'W', true);
    uas::set_key(keys, 'D', true);
    uas::set_key(keys, 'E', true);
    results.command(uas::make_command(keys, true), {tilt, -tilt, yaw_rate},
                    "Three axes can be commanded independently at the same time");
    uas::release_all(keys);
    results.check(same_keys(keys, uas::Keys{}), "Reset releases every key");
    results.command(uas::make_command(keys, true), neutral, "Reset clears every command");
    uas::release_all(keys);
    results.command(uas::make_command(keys, true), neutral, "Repeated reset remains neutral");

    // An unrecognized input must not clear or alter an already-held control.
    uas::set_key(keys, 'W', true);
    uas::set_key(keys, 'D', true);
    uas::set_key(keys, 'E', true);
    const uas::Keys before_unknown_key = keys;
    const std::array<unsigned, 5> unrelated_keys{{0u, 'X', ' ', 'w', 0xFFFFu}};
    for (unsigned key : unrelated_keys)
    {
        results.check(!uas::set_key(keys, key, true), "Unrelated key press is rejected");
        results.check(!uas::set_key(keys, key, false), "Unrelated key release is rejected");
        results.check(same_keys(keys, before_unknown_key), "Unrelated key leaves held controls unchanged");
    }

    if (results.failures != 0)
    {
        std::cerr << results.failures << " of " << results.checks << " keyboard logic checks failed.\n";
        return 1;
    }

    std::cout << "All " << results.checks << " keyboard logic checks passed.\n"
              << "All 64 key combinations were checked with and without focus.\n";
    return 0;
}
