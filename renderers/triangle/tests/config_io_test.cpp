#include "render_config.hpp"
#include "render_config_json.hpp"

#include <renderer/config_io/json_document.hpp>
#include <renderer/config_io/json_fields.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using Json = nlohmann::json;
using renderer::config::RenderConfig;
namespace io = renderer::config_io;
int passed = 0;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class Function>
void test(const char* name, Function function)
{
    try { function(); }
    catch (const std::exception& error)
    {
        throw std::runtime_error(std::string(name) + ": " + error.what());
    }
    ++passed;
    std::cout << "PASS: " << name << '\n';
}

template<class Function>
void rejects(Function function)
{
    try { function(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid input was not rejected");
}

Json defaults()
{
    return Json(RenderConfig{});
}

void rejects_at(const char* name, const char* pointer, Json value)
{
    test(name, [&]
    {
        auto document = defaults();
        document[Json::json_pointer(pointer)] = std::move(value);
        rejects([&] { static_cast<void>(document.get<RenderConfig>()); });
    });
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::invalid_argument("Usage: triangle_config_io_test TEST_DIRECTORY");

        test("default document round trip", []
        {
            const auto document = defaults();
            require(Json(document.get<RenderConfig>()) == document, "round trip changed settings");
        });
        test("minimal document and defaults", []
        {
            const auto config = Json{{"schema_version", 1}}.get<RenderConfig>();
            require(config.device.ordinal == 0, "default device changed");
            require(config.renderer.width == 800 && config.renderer.height == 600,
                "default dimensions changed");
            require(config.output.path == "triangle.png", "default output path changed");
        });
        test("empty nested objects keep app defaults", []
        {
            const auto document = Json::parse(
                R"({"schema_version":1,"device":{},"renderer":{},"output":{},"diagnostics":{}})");
            require(Json(document.get<RenderConfig>()) == defaults(), "nested defaults changed");
        });
        test("device ordinal is actually applied", []
        {
            auto document = defaults();
            document["device"]["ordinal"] = 3;
            const auto config = document.get<RenderConfig>();
            require(config.device.ordinal == 3, "ordinal was ignored");
            require(Json(config) == document, "non-default ordinal lost in serialization");
        });
        test("all non-default supported values round trip", []
        {
            RenderConfig config;
            config.device.ordinal = 2;
            config.device.optix_validation = true;
            config.renderer.width = 641;
            config.renderer.height = 479;
            config.output.path = std::filesystem::path(u8"../画像/結果.png");
            config.output.transform.exposure_ev = 0.1f;
            const Json document = config;
            const auto restored = document.get<RenderConfig>();
            require(Json(restored) == document, "non-default round trip changed settings");
            require(restored.output.transform.exposure_ev == config.output.transform.exposure_ev,
                "float value was not restored");
        });
        test("valid fixed-scene diagnostics", []
        {
            auto document = defaults();
            document["diagnostics"]["verify_triangle"] = true;
            require(document.get<RenderConfig>().diagnostics.verify_triangle,
                "diagnostics flag was ignored");
        });
        test("negative and positive exposure endpoints", []
        {
            for (float endpoint : {-32.0f, 32.0f})
            {
                auto document = defaults();
                document["output"]["exposure_ev"] = endpoint;
                require(document.get<RenderConfig>().output.transform.exposure_ev == endpoint,
                    "valid endpoint rejected");
            }
        });
        test("missing schema version", []
        {
            auto document = defaults();
            document.erase("schema_version");
            rejects([&] { static_cast<void>(document.get<RenderConfig>()); });
        });
        test("root must be an object", []
        {
            for (const auto& document : {Json(nullptr), Json::array(), Json(true), Json(1)})
                rejects([&] { static_cast<void>(document.get<RenderConfig>()); });
        });
        test("unknown root key", []
        {
            auto document = defaults();
            document["unexpected"] = 1;
            rejects([&] { static_cast<void>(document.get<RenderConfig>()); });
        });
        test("unknown nested keys", []
        {
            for (const char* section : {"device", "renderer", "output", "diagnostics"})
            {
                auto document = defaults();
                document[section]["unexpected"] = 1;
                rejects([&] { static_cast<void>(document.get<RenderConfig>()); });
            }
        });
        test("nested objects reject null and arrays", []
        {
            for (const char* section : {"device", "renderer", "output", "diagnostics"})
                for (const auto& wrong : {Json(nullptr), Json::array()})
                {
                    auto document = defaults();
                    document[section] = wrong;
                    rejects([&] { static_cast<void>(document.get<RenderConfig>()); });
                }
        });
        rejects_at("unknown version", "/schema_version", 2);
        rejects_at("floating version", "/schema_version", 1.0);
        rejects_at("boolean version", "/schema_version", true);
        rejects_at("negative device", "/device/ordinal", -1);
        rejects_at("floating device", "/device/ordinal", 1.5);
        rejects_at("overflowing device", "/device/ordinal",
            std::uint64_t{static_cast<std::uint64_t>((std::numeric_limits<int>::max)())} + 1);
        rejects_at("boolean device", "/device/ordinal", true);
        rejects_at("nonboolean validation", "/device/optix_validation", 1);
        rejects_at("negative width", "/renderer/width", -1);
        rejects_at("zero width", "/renderer/width", 0);
        rejects_at("floating width", "/renderer/width", 800.5);
        rejects_at("integral-looking float width", "/renderer/width", 800.0);
        rejects_at("boolean width", "/renderer/width", true);
        rejects_at("overflowing width", "/renderer/width", std::uint64_t{4294967296ULL});
        rejects_at("PNG dimension limit", "/renderer/width", 65536);
        rejects_at("zero height", "/renderer/height", 0);
        rejects_at("unknown renderer", "/renderer/type", "other");
        rejects_at("numeric renderer", "/renderer/type", 0);
        rejects_at("unknown format", "/output/format", "exr");
        rejects_at("unknown tone mapper", "/output/tone_mapping", "other");
        rejects_at("unknown transfer choice", "/output/color_encoding", "other");
        rejects_at("empty output path", "/output/path", "");
        rejects_at("NUL in output path", "/output/path", std::string("a\0b.png", 7));
        rejects_at("nonnumeric exposure", "/output/exposure_ev", "1");
        rejects_at("boolean exposure", "/output/exposure_ev", true);
        rejects_at("exposure above limit before float conversion", "/output/exposure_ev", 32.0000001);
        rejects_at("exposure below limit before float conversion", "/output/exposure_ev", -32.0000001);
        rejects_at("NaN in in-memory JSON", "/output/exposure_ev",
            (std::numeric_limits<double>::quiet_NaN)());
        rejects_at("infinity in in-memory JSON", "/output/exposure_ev",
            (std::numeric_limits<double>::infinity)());
        test("diagnostic image dimensions", []
        {
            auto document = defaults();
            document["diagnostics"]["verify_triangle"] = true;
            document["renderer"]["width"] = 801;
            rejects([&] { static_cast<void>(document.get<RenderConfig>()); });
        });
        test("conversion failure does not partially modify settings", []
        {
            RenderConfig original;
            original.device.ordinal = 7;
            const Json before = original;
            auto invalid = defaults();
            invalid["device"]["ordinal"] = 2;
            invalid["output"]["exposure_ev"] = 100;
            rejects([&] { invalid.get_to(original); });
            require(Json(original) == before, "partial update escaped");
        });
        test("whole document does not inherit previous configuration", []
        {
            RenderConfig value;
            value.device.ordinal = 7;
            Json{{"schema_version", 1}}.get_to(value);
            require(value.device.ordinal == 0, "old value leaked into a new configuration");
        });
        test("invalid settings cannot be serialized", []
        {
            auto value = RenderConfig{};
            value.renderer.type = static_cast<renderer::config::RendererType>(99);
            rejects([&] { static_cast<void>(Json(value)); });
            value = {};
            value.output.transform.exposure_ev = (std::numeric_limits<float>::infinity)();
            rejects([&] { static_cast<void>(Json(value)); });
        });
        test("duplicate JSON keys", []
        {
            for (const char* text : {
                R"({"schema_version":1,"schema_version":1})",
                R"({"device":{"ordinal":0,"ordinal":1}})",
                R"({"items":[{"a":1,"a":2}]})"})
            {
                std::istringstream input(text);
                rejects([&] { static_cast<void>(io::parse_json_document(input)); });
            }
        });
        test("same spelling in different objects is valid", []
        {
            std::istringstream input(R"({"a":{"x":1},"b":{"x":2}})");
            require(io::parse_json_document(input)["b"]["x"] == 2,
                "keys from unrelated objects were confused");
        });
        test("malformed JSON is rejected", []
        {
            std::istringstream input("{\"schema_version\":");
            bool rejected = false;
            try { static_cast<void>(io::parse_json_document(input)); }
            catch (const Json::parse_error&) { rejected = true; }
            require(rejected, "malformed JSON accepted");
        });
        test("field mapping drives both directions and unknown-key rejection", []
        {
            struct Settings { int count = 4; bool enabled = false; };
            const auto fields = [](auto& archive, auto& value)
            {
                archive.field("count", value.count);
                archive.field("enabled", value.enabled);
            };
            Settings value;
            io::read_json_fields(Json{{"count", 9}}, value, fields);
            require(value.count == 9 && !value.enabled, "field/default handling failed");
            require(io::write_json_fields(value, fields)
                == Json{{"count", 9}, {"enabled", false}}, "writer did not use same mapping");
            rejects([&] { io::read_json_fields(Json{{"other", 1}}, value, fields); });
        });

        const auto root = std::filesystem::absolute(argv[1]).lexically_normal();
        std::filesystem::create_directories(root / "original");
        std::filesystem::create_directories(root / "saved" / "nested");
        const auto source = root / "original" / "source.json";
        const auto destination = root / "saved" / "nested" / "restored.json";
        test("save as preserves output target and all settings", [&]
        {
            auto document = defaults();
            document["device"]["ordinal"] = 3;
            document["output"]["path"] = "../images/result.png";
            document["output"]["exposure_ev"] = 0.25;
            io::save_json_document(source, document);
            const auto original = renderer::config::load_render_config(source);
            require(original.output.path == root / "images" / "result.png",
                "load did not resolve path against config directory");
            renderer::config::save_render_config(destination, original);
            const auto restored = renderer::config::load_render_config(destination);
            require(Json(restored) == Json(original), "Save As changed effective settings");
        });
        test("invalid save does not truncate an existing configuration", [&]
        {
            const auto before = io::load_json_document(destination);
            auto invalid = RenderConfig{};
            invalid.output.transform.exposure_ev = 100.0f;
            rejects([&] { renderer::config::save_render_config(destination, invalid); });
            require(io::load_json_document(destination) == before, "invalid save changed the file");
        });
        test("empty config paths rejected", []
        {
            rejects([] { static_cast<void>(renderer::config::load_render_config({})); });
            rejects([] { renderer::config::save_render_config({}, RenderConfig{}); });
        });
#ifdef _WIN32
        rejects_at("drive-relative output path", "/output/path", "C:result.png");
#endif
        std::cout << "Config I/O: " << passed << " tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Config I/O test failed: " << error.what() << '\n';
        return 1;
    }
}
