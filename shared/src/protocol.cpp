#include "hyprdictate/protocol.hpp"

#include <string>

namespace hyprdictate {

    using json = nlohmann::json;

    namespace {

        // std::visit helper. Overloads a lambda pack into a single
        // callable so each variant alternative gets its own tiny
        // handler and adding a new alternative to Command/Event forces
        // a compile error in every serialize() until the overload is
        // added. Same pattern hyprwsmode uses on its Mode variant.
        template <class... Ts>
        struct Overloaded : Ts... { using Ts::operator()...; };
        template <class... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

        // "window" is optional in the JSON schema; missing or non-
        // object returns nullopt so the daemon can distinguish "client
        // didn't send window context" (CLI use) from "client sent an
        // empty context" (unused today; the two would collapse to the
        // same behaviour in M1 anyway).
        std::optional<WindowContext> readWindow(const json& j) {
            if (!j.contains("window") || !j["window"].is_object())
                return std::nullopt;

            const auto& w = j["window"];
            WindowContext wc;
            if (w.contains("class") && w["class"].is_string())
                wc.cls = w["class"].get<std::string>();
            if (w.contains("title") && w["title"].is_string())
                wc.title = w["title"].get<std::string>();
            return wc;
        }

        json writeWindow(const WindowContext& w) {
            return json{
                {"class", w.cls},
                {"title", w.title},
            };
        }

    }

    json serialize(const Command& c) {
        return std::visit(Overloaded{
            [](const command::Toggle&) -> json { return {{"cmd", "toggle"}}; },
            [](const command::Stop&)   -> json { return {{"cmd", "stop"}}; },
            [](const command::Cancel&) -> json { return {{"cmd", "cancel"}}; },
            [](const command::Status&) -> json { return {{"cmd", "status"}}; },
            [](const command::Reload&) -> json { return {{"cmd", "reload"}}; },
            [](const command::PttUp&)  -> json { return {{"cmd", "ptt_up"}}; },
            [](const command::Start& x) -> json {
                json j{{"cmd", "start"}};
                if (x.window) j["window"] = writeWindow(*x.window);
                return j;
            },
            [](const command::PttDown& x) -> json {
                json j{{"cmd", "ptt_down"}};
                if (x.window) j["window"] = writeWindow(*x.window);
                return j;
            },
        }, c);
    }

    json serialize(const Event& e) {
        return std::visit(Overloaded{
            [](const event::StateChanged& x) -> json {
                return {
                    {"event", "state"},
                    {"value", formatState(x.value)},
                };
            },
            [](const event::Transcript& x) -> json {
                return {
                    {"event", "transcript"},
                    {"text",  x.text},
                };
            },
            [](const event::StatusReply& x) -> json {
                json j{
                    {"event", "status"},
                    {"state", formatState(x.state)},
                };
                if (x.model_path)
                    j["model_path"] = *x.model_path;
                return j;
            },
            [](const event::Error& x) -> json {
                return {
                    {"event",   "error"},
                    {"message", x.message},
                };
            },
        }, e);
    }

    namespace {

    Command parseCommandJson(const json& j) {
        if (!j.is_object())
            throw ProtocolError("command payload must be a JSON object");
        if (!j.contains("cmd") || !j["cmd"].is_string())
            throw ProtocolError("command payload missing string 'cmd' field");

        const auto cmd = j["cmd"].get<std::string>();

        if (cmd == "toggle")   return command::Toggle{};
        if (cmd == "stop")     return command::Stop{};
        if (cmd == "cancel")   return command::Cancel{};
        if (cmd == "status")   return command::Status{};
        if (cmd == "reload")   return command::Reload{};
        if (cmd == "ptt_up")   return command::PttUp{};
        if (cmd == "start")    return command::Start{ .window = readWindow(j) };
        if (cmd == "ptt_down") return command::PttDown{ .window = readWindow(j) };

        throw ProtocolError("unknown command: " + cmd);
    }

    } // namespace

    Command parseCommand(std::string_view line) {
        try {
            // json::parse takes an iterator range, avoiding a string
            // copy over the wire payload. string_view over an in-place
            // socket buffer therefore doesn't need to be materialised.
            return parseCommandJson(json::parse(line.begin(), line.end()));
        } catch (const json::parse_error& e) {
            throw ProtocolError(std::string{"json parse error: "} + e.what());
        }
    }

    namespace {

    Event parseEventJson(const json& j) {
        if (!j.is_object())
            throw ProtocolError("event payload must be a JSON object");
        if (!j.contains("event") || !j["event"].is_string())
            throw ProtocolError("event payload missing string 'event' field");

        const auto ev = j["event"].get<std::string>();

        if (ev == "state") {
            if (!j.contains("value") || !j["value"].is_string())
                throw ProtocolError("state event missing 'value'");
            const auto s = parseState(j["value"].get<std::string>());
            if (!s)
                throw ProtocolError("state event has unknown value");
            return event::StateChanged{ .value = *s };
        }
        if (ev == "transcript") {
            if (!j.contains("text") || !j["text"].is_string())
                throw ProtocolError("transcript event missing 'text'");
            return event::Transcript{ .text = j["text"].get<std::string>() };
        }
        if (ev == "status") {
            State s = State::Idle;
            if (j.contains("state") && j["state"].is_string()) {
                if (auto parsed = parseState(j["state"].get<std::string>()))
                    s = *parsed;
            }
            std::optional<std::string> mp;
            if (j.contains("model_path") && j["model_path"].is_string())
                mp = j["model_path"].get<std::string>();
            return event::StatusReply{ .state = s, .model_path = mp };
        }
        if (ev == "error") {
            std::string msg;
            if (j.contains("message") && j["message"].is_string())
                msg = j["message"].get<std::string>();
            return event::Error{ .message = std::move(msg) };
        }

        throw ProtocolError("unknown event: " + ev);
    }

    } // namespace

    Event parseEvent(std::string_view line) {
        try {
            return parseEventJson(json::parse(line.begin(), line.end()));
        } catch (const json::parse_error& e) {
            throw ProtocolError(std::string{"json parse error: "} + e.what());
        }
    }

}
