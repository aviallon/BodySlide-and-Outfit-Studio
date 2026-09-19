#include "../src/components/SliderSet.h"
#include "../src/utils/ConfigurationManager.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using nifly::Vector3;

/* The components here reach the application's global Config through
   NormalGenLayers. The application defines it in BodySlideApp.cpp, which these
   tests do not link, so they provide it. */
ConfigurationManager Config;

namespace {

constexpr size_t EntrySize = sizeof(uint32_t) + sizeof(Vector3);

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
			   ("bsos-sliderset-" + std::to_string(::rand()) + "-" + std::to_string(::rand()));
		std::filesystem::create_directories(path);
	}

	~TempDir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}

	const std::filesystem::path& Path() const { return path; }
};

/* A set with one slider whose single .bsd lives in *dataFolder*. */
SliderSet MakeSet(const std::filesystem::path& basePath, const std::string& fileName) {
	SliderSet set;
	set.SetBaseDataPath(basePath.string());
	set.SetDataFolder("DataFolder");
	set.AddShapeTarget("MyShape", "BaseShape");

	size_t sliderIndex = set.CreateSlider("MySlider");
	set[sliderIndex].AddDataFile("BaseShape", "MyData", fileName, false);
	return set;
}

} // namespace

TEST_CASE("A .bsd that is in no data folder is reported, not read", "[sliderset]") {
	/* The real-world case: ZaZ 8 references 55 .bsd files from a data folder
	   ('CalienteBody') that ships in no installed mod. The set must build
	   without them AND say which ones are missing - the base path handed to the
	   reader here used to be a directory, whose entry count was read from an
	   uninitialized variable. */
	TempDir dir;
	SliderSet set = MakeSet(dir.Path(), "NotInstalled.bsd");

	DiffDataSets diffs;
	std::vector<UnresolvedSliderData> unresolved;
	set.LoadSetDiffData(diffs, "", &unresolved);

	REQUIRE(unresolved.size() == 1);
	REQUIRE(unresolved[0].sliderName == "MySlider");
	REQUIRE(unresolved[0].dataName == "MyData");
	REQUIRE(unresolved[0].fileName == "NotInstalled.bsd");
	REQUIRE(unresolved[0].reason == "not found in any data folder");
	// and nothing was loaded for it
	REQUIRE(diffs.GetDiffSet("MyData") == nullptr);
}

TEST_CASE("A .bsd that is present and valid loads and is not reported", "[sliderset]") {
	TempDir dir;
	std::filesystem::create_directories(dir.Path() / "DataFolder");
	WriteBsd(dir.Path() / "DataFolder" / "Present.bsd", 2, 2);

	SliderSet set = MakeSet(dir.Path(), "Present.bsd");

	DiffDataSets diffs;
	std::vector<UnresolvedSliderData> unresolved;
	set.LoadSetDiffData(diffs, "", &unresolved);

	REQUIRE(unresolved.empty());
	auto* loaded = diffs.GetDiffSet("MyData");
	REQUIRE(loaded != nullptr);
	REQUIRE(loaded->size() == 2);
}

TEST_CASE("A .bsd that exists but is corrupt is reported", "[sliderset]") {
	TempDir dir;
	std::filesystem::create_directories(dir.Path() / "DataFolder");
	// Claims 100000 entries while holding two.
	WriteBsd(dir.Path() / "DataFolder" / "Corrupt.bsd", 100000u, 2);

	SliderSet set = MakeSet(dir.Path(), "Corrupt.bsd");

	DiffDataSets diffs;
	std::vector<UnresolvedSliderData> unresolved;
	set.LoadSetDiffData(diffs, "", &unresolved);

	REQUIRE(unresolved.size() == 1);
	REQUIRE(unresolved[0].fileName == "Corrupt.bsd");
	REQUIRE(unresolved[0].reason == "file could not be read");
}

TEST_CASE("Reporting does not leak between calls", "[sliderset]") {
	TempDir dir;
	SliderSet set = MakeSet(dir.Path(), "NotInstalled.bsd");

	DiffDataSets diffs;
	std::vector<UnresolvedSliderData> unresolved;
	set.LoadSetDiffData(diffs, "", &unresolved);
	REQUIRE(unresolved.size() == 1);

	// A second set of calls must not append to the previous report, or an
	// outfit built later in a batch would look like it had the earlier set's
	// problems.
	SliderSet empty;
	empty.SetBaseDataPath(dir.Path().string());
	empty.SetDataFolder("DataFolder");
	std::vector<UnresolvedSliderData> secondReport;
	empty.LoadSetDiffData(diffs, "", &secondReport);
	REQUIRE(secondReport.empty());
}
