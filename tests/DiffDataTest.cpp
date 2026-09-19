#include "../src/components/DiffData.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using nifly::Vector3;

namespace {

constexpr size_t EntrySize = sizeof(uint32_t) + sizeof(Vector3);

/* A .bsd is a uint32 entry count followed by that many (uint32 index, Vector3
   offset) pairs. */
void WriteBsd(const std::filesystem::path& path, uint32_t count, size_t entries) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(&count), sizeof(count));
	for (size_t i = 0; i < entries; i++) {
		uint32_t index = static_cast<uint32_t>(i);
		Vector3 diff(static_cast<float>(i), 0.0f, 0.0f);
		out.write(reinterpret_cast<const char*>(&index), sizeof(index));
		out.write(reinterpret_cast<const char*>(&diff), sizeof(diff));
	}
}

class TempDir {
	std::filesystem::path path;

public:
	TempDir() {
		path = std::filesystem::temp_directory_path() /
			   ("bsos-diffdata-" + std::to_string(::rand()) + "-" + std::to_string(::rand()));
		std::filesystem::create_directories(path);
	}

	~TempDir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}

	const std::filesystem::path& Path() const { return path; }
};

} // namespace

TEST_CASE("DiffDataSets::LoadSet refuses a path that is a directory", "[diffdata]") {
	/* This is the shape of the crash this guards: SliderSet::LoadSetDiffData
	   passes its base path when a .bsd is not in any data folder, and a
	   directory opens successfully on POSIX - only reading it fails. The entry
	   count was then read from an uninitialized variable and handed to
	   reserve(), which on a real load order asked for tens of GB. */
	TempDir dir;
	DiffDataSets sets;

	// The old code returned 1 only when open() failed, which it does not here.
	REQUIRE(sets.LoadSet("Slider", "Shape", dir.Path().string()) != 0);
	REQUIRE_FALSE(sets.TargetMatch("Slider", "Shape"));
	REQUIRE(sets.GetDiffSet("Slider") == nullptr);
}

TEST_CASE("DiffDataSets::LoadSet refuses an entry count the file cannot hold", "[diffdata]") {
	TempDir dir;
	auto file = dir.Path() / "Truncated.bsd";
	// Claims 0xDEADBEEF entries while holding two: reserve() would ask for GBs.
	WriteBsd(file, 0xDEADBEEFu, 2);

	DiffDataSets sets;
	REQUIRE(sets.LoadSet("Slider", "Shape", file.string()) != 0);
	REQUIRE_FALSE(sets.TargetMatch("Slider", "Shape"));
}

TEST_CASE("DiffDataSets::LoadSet refuses an empty or truncated file", "[diffdata]") {
	TempDir dir;
	auto empty = dir.Path() / "Empty.bsd";
	std::ofstream(empty, std::ios::binary).close();

	auto noCount = dir.Path() / "NoCount.bsd";
	{
		std::ofstream out(noCount, std::ios::binary);
		uint32_t count = 5; // says five entries, then stops
		out.write(reinterpret_cast<const char*>(&count), sizeof(count));
	}

	DiffDataSets sets;
	REQUIRE(sets.LoadSet("EmptySlider", "Shape", empty.string()) != 0);
	REQUIRE(sets.LoadSet("ShortSlider", "Shape", noCount.string()) != 0);
	REQUIRE_FALSE(sets.TargetMatch("EmptySlider", "Shape"));
	REQUIRE_FALSE(sets.TargetMatch("ShortSlider", "Shape"));
}

TEST_CASE("DiffDataSets::LoadSet still reads a valid .bsd", "[diffdata]") {
	TempDir dir;
	auto file = dir.Path() / "Valid.bsd";
	WriteBsd(file, 3, 3);

	DiffDataSets sets;
	REQUIRE(sets.LoadSet("Slider", "Shape", file.string()) == 0);
	REQUIRE(sets.TargetMatch("Slider", "Shape"));

	auto* diffs = sets.GetDiffSet("Slider");
	REQUIRE(diffs != nullptr);
	REQUIRE(diffs->size() == 3);
	REQUIRE(diffs->count(2) == 1);
	REQUIRE(std::fabs(diffs->at(2).x - 2.0f) < 0.0001f);
}

TEST_CASE("DiffDataSets::LoadSet reads a .bsd of exactly the file's length", "[diffdata]") {
	/* The bound must not reject the largest legitimate file. */
	TempDir dir;
	auto file = dir.Path() / "Full.bsd";
	WriteBsd(file, 4096, 4096);
	REQUIRE(std::filesystem::file_size(file) == sizeof(uint32_t) + 4096 * EntrySize);

	DiffDataSets sets;
	REQUIRE(sets.LoadSet("Slider", "Shape", file.string()) == 0);
	auto* diffs = sets.GetDiffSet("Slider");
	REQUIRE(diffs != nullptr);
	REQUIRE(diffs->size() == 4096);
}
