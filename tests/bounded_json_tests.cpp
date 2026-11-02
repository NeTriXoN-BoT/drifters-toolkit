#include "../src/bounded_json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace fs = std::filesystem;
int main() {
    unsigned checks = 0;
    auto check = [&](bool good, const char* label) {
        ++checks;
        if (!good) throw std::runtime_error(label);
    };
    auto reject = [&](auto operation, const char* label) {
        bool failed = false;
        try {
            operation();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, label);
    };
    try {
        using drifter::parseBoundedJson;
        using drifter::readBoundedJson;
        check(parseBoundedJson("{\"a\":1,\"b\":[{\"a\":2},{\"a\":3}]}")["b"].size() == 2,
              "ordinary sibling objects rejected");
        reject([&] { parseBoundedJson("{\"a\":1,\"a\":2}"); }, "duplicate root key accepted");
        reject([&] { parseBoundedJson("{\"a\":{\"b\":1,\"b\":2}}"); }, "duplicate nested key accepted");
        reject([&] { parseBoundedJson("{\"a\":1,\"\\u0061\":2}"); }, "escaped duplicate key accepted");
        check(parseBoundedJson(std::string(64, '[') + "0" + std::string(64, ']')).is_array(), "depth64 rejected");
        reject([&] { parseBoundedJson(std::string(65, '[') + "0" + std::string(65, ']')); }, "depth65 accepted");
        reject([&] { parseBoundedJson("[0,1,2,3,4,5,6,7]", 100, 8); }, "node/event limit accepted");
        reject([&] { parseBoundedJson("[0,1,2]", 6); }, "text byte limit accepted");
        std::istringstream exact("[1]");
        check(readBoundedJson(exact, 3)[0] == 1, "exact byte limit rejected");
        std::istringstream oversized(std::string(200000, ' '));
        reject([&] { readBoundedJson(oversized, 70000); }, "oversized stream accepted");
        check(oversized.tellg() == std::streampos(70001), "stream read beyond byte limit plus one");
        std::istringstream broken("{}");
        broken.setstate(std::ios::badbit);
        reject([&] { readBoundedJson(broken); }, "stream I/O failure accepted");
        auto path =
            fs::temp_directory_path() /
            ("drifter-json-growth-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            fs::path file;
            ~Cleanup() {
                std::error_code ignored;
                fs::remove(file, ignored);
            }
        } cleanup{path};
        {
            std::ofstream file(path, std::ios::binary);
            file << "{}";
        }
        check(fs::file_size(path) == 2, "growing fixture initial size");
        std::ifstream opened(path, std::ios::binary);
        {
            std::ofstream append(path, std::ios::binary | std::ios::app);
            append << std::string(200, ' ');
        }
        reject([&] { readBoundedJson(opened, 100); }, "file growing after size check bypassed read bound");
        check(opened.tellg() == std::streampos(101), "growing file read exceeded limit plus one");
        opened.close();
        std::cout << "Bounded durable JSON checks passed: " << checks << "\n";
        return 0;
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << "\n";
        return 1;
    }
}
