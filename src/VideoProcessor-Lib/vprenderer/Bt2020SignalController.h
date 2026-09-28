#pragma once

#include <cstdint>
#include <map>
#include <string>

// Backend supplies a freshly read AVI frame and changes only its colorimetry.
// Time and I/O are injected so mode changes and delayed drivers can be tested.
namespace Bt2020Signal
{
    struct Color
    {
        unsigned standard = 0;
        unsigned extended = 0;
        bool operator==(const Color& other) const
        { return standard == other.standard && extended == other.extended; }
    };
    inline Color Rec709() { return { 2, 0 }; }
    inline Color Bt2020() { return { 3, 6 }; }
    inline Color ReleaseColor(Color original)
    {
        // Never restore an inherited BT.2020 flag when disengaging VP.
        return original.standard == 3 && (original.extended == 5 || original.extended == 6)
            ? Rec709() : original;
    }

    template<class Backend> class Controller
    {
    public:
        explicit Controller(Backend& backend) : io(backend) {}

        void Request(const std::string& display, bool enabled, uint64_t now, bool restart)
        {
            retiring = false;
            if (display != target)
            {
                auto old = outputs.find(target);
                if (old != outputs.end()) Start(old->second, Mode::Release, now);
                target = display;
                restart = true;
            }
            desired = enabled;
            if (display.empty()) return; // No primary-display fallback.
            auto found = outputs.find(display);
            if (found == outputs.end())
            {
                if (!enabled) return; // Do not take ownership of an unrelated output.
                found = outputs.emplace(display, Output{}).first;
                restart = true;
            }
            const Mode mode = enabled ? Mode::Enable : Mode::Disable;
            if (restart || found->second.mode != mode) Start(found->second, mode, now);
        }

        void Pump(uint64_t now)
        {
            if (retiring) return;
            for (auto& item : outputs)
            {
                auto& state = item.second;
                if (!state.pending || now < state.next) continue;
                Attempt(item.first, state);
                ++state.attempt;
                // Repeat even after matching readback: the projector can change
                // mode after acceptance. No catch-up burst if the worker is late.
                static const uint64_t gaps[] = { 500, 1000, 1500, 2000, 3000 };
                if (state.attempt <= 5) state.next = now + gaps[state.attempt - 1];
                else
                {
                    state.pending = false;
                    if (state.mode == Mode::Release && state.accepted) state.dirty = false;
                }
            }
        }

        bool Retire()
        {
            retiring = true; // Cancels every delayed enable before any cleanup.
            bool complete = true;
            for (auto& item : outputs)
            {
                auto& state = item.second;
                state.pending = false;
                if (!state.dirty) continue;
                state.mode = Mode::Release;
                if (!Attempt(item.first, state)) complete = false;
                else state.dirty = false;
            }
            return complete;
        }
        void Abandon() { retiring = true; outputs.clear(); target.clear(); }
        bool IsActive() const
        {
            auto it = outputs.find(target);
            return !retiring && desired && it != outputs.end() && it->second.mode == Mode::Enable && it->second.accepted;
        }
        bool IsVerified() const
        {
            auto it = outputs.find(target);
            return !retiring && desired && it != outputs.end() && it->second.mode == Mode::Enable && it->second.verified;
        }
        bool Failed() const
        {
            auto it = outputs.find(target);
            return desired && (target.empty() || (it != outputs.end() &&
                !it->second.pending && !it->second.accepted));
        }
        std::string Status() const
        {
            const auto it = outputs.find(target);
            if (it == outputs.end()) return desired ? "BT.2020 target unavailable" : "not requested";
            const auto& state = it->second;
            const std::string label = desired ? "BT.2020" : "Rec.709 / BT.2020 off";
            if (state.pending) return label + " (retrying; wire unverified)";
            if (!state.accepted) return label + " (FAILED; wire unverified)";
            return label + (state.verified ? " (driver readback matched; wire unverified)" :
                " (SET accepted; wire unverified)");
        }
    private:
        enum class Mode { Enable, Disable, Release };
        struct Output
        {
            Mode mode = Mode::Enable;
            Color original{};
            bool saved = false, dirty = false, pending = false;
            bool accepted = false, verified = false;
            unsigned attempt = 0;
            uint64_t next = 0;
        };
        static void Start(Output& state, Mode mode, uint64_t now)
        {
            if (state.mode == Mode::Release && !state.dirty) state.saved = false;
            state.mode = mode;
            state.pending = true;
            state.attempt = 0;
            state.next = now;
            state.accepted = state.verified = false;
        }
        bool Attempt(const std::string& display, Output& state)
        {
            typename Backend::Frame frame{};
            state.accepted = state.verified = false;
            if (!io.Read(display, frame))
            {
                io.Report(display, state.mode == Mode::Enable, state.attempt + 1, false, false);
                return false;
            }
            if (!state.saved)
            {
                state.original = io.GetColor(frame);
                state.saved = true;
            }
            const Color wanted = state.mode == Mode::Enable ? Bt2020() :
                state.mode == Mode::Disable ? Rec709() : ReleaseColor(state.original);
            io.SetColor(frame, wanted);
            // A failed SET can still leave ambiguous driver state. Retain cleanup
            // ownership before calling it, including when readback later fails.
            state.dirty = true;
            state.accepted = io.Write(display, frame);
            if (state.accepted)
            {
                typename Backend::Frame readback{};
                state.verified = io.Read(display, readback) && io.GetColor(readback) == wanted;
            }
            io.Report(display, state.mode == Mode::Enable, state.attempt + 1,
                state.accepted, state.verified);
            return state.accepted;
        }
        Backend& io;
        std::map<std::string, Output> outputs;
        std::string target;
        bool desired = false, retiring = false;
    };
}
