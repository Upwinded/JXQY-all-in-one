#include "../Update/OnlineUpdateCatalog.h"
#include "../Update/ResourceDownloadPlanner.h"
#include "../Update/ArtifactChecksum.h"

#include <iostream>
#include <limits>
#include <string>

namespace
{
int failureCount = 0;

void expect(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		failureCount++;
	}
}

std::string validCatalog()
{
	return
		"[Catalog]\n"
		"SchemaVersion=1\n"
		"ProgramTargets=windows,android\n"
		"\n"
		"[Common]\n"
		"Version=1.1\n"
		"Artifact=resources/common.zip\n"
		"Size=50\n"
		"Crc32=abcdef12\n"
		"ReleaseNotes=Common update\n"
		"\n"
		"[Resource.JXQY2]\n"
		"Version=1.0\n"
		"MinimumEngineVersion=2.0.0\n"
		"Artifact=resources/jxqy2.zip\n"
		"Size=100\n"
		"Crc32=00000000\n"
		"\n"
		"[Resource.YYCS]\n"
		"Name=月影传说\n"
		"Author=西山居\n"
		"Version=1.03 preview\n"
		"MinimumEngineVersion=2.0.0\n"
		"Artifact=resources/yycs.zip\n"
		"Size=200\n"
		"Crc32=11111111\n"
		"IncrementalArtifact=resources/yycs-incremental.zip\n"
		"IncrementalSize=20\n"
		"IncrementalCrc32=AAAAAAAA\n"
		"Dependencies=jxqy2\n"
		"ReleaseNotes=Moon Shadow update\n"
		"\n"
		"[Resource.STORY_MOD]\n"
		"Version=江湖篇\n"
		"MinimumEngineVersion=2.0.0\n"
		"Artifact=resources/story_mod.zip\n"
		"Size=300\n"
		"Crc32=22222222\n"
		"Dependencies=YYCS\n"
		"\n"
		"[Program.windows]\n"
		"Version=2.1.0\n"
		"Artifact=program/windows-2.1.0.zip\n"
		"Size=400\n"
		"Crc32=33333333\n"
		"\n"
		"[Program.android]\n"
		"Version=2.1.0\n"
		"Artifact=program/jxqy-2.1.0.apk\n"
		"Size=500\n"
		"Crc32=44444444\n";
}

void testCatalogParsing()
{
	using namespace OnlineUpdate;
	const CatalogParseResult parsed = parseCatalog(validCatalog());
	expect(parsed.succeeded(), "valid update catalog parses");
	expect(parsed.catalog.schemaVersion == 1 &&
		parsed.catalog.resourcePackages.size() == 3 &&
		parsed.catalog.programPackages.size() == 2 &&
		parsed.catalog.commonPackage.has_value() &&
		parsed.catalog.commonPackage->versionText == "1.1" &&
		parsed.catalog.commonPackage->artifactSize == 50,
		"catalog discovers resource sections without a total ID list");
	const auto yycs = parsed.catalog.resourcePackages.find("yycs");
	expect(yycs != parsed.catalog.resourcePackages.end() &&
		yycs->second.gameId == "YYCS" &&
		yycs->second.displayName == u8"月影传说" &&
		yycs->second.author == u8"西山居" &&
		yycs->second.versionText == "1.03 preview" &&
		yycs->second.dependencyGameIds.size() == 1 &&
		yycs->second.dependencyGameIds.front() == "jxqy2" &&
		yycs->second.artifactSize == 200 &&
		yycs->second.incrementalPackage.has_value() &&
		yycs->second.incrementalPackage->artifactPath ==
			"resources/yycs-incremental.zip" &&
		yycs->second.incrementalPackage->artifactSize == 20 &&
		yycs->second.incrementalPackage->crc32Hex == "aaaaaaaa",
		"resource identity, display metadata and plain dependencies parse");

	std::string incompleteIncremental = validCatalog();
	const std::string incrementalSize = "IncrementalSize=20\n";
	incompleteIncremental.erase(
		incompleteIncremental.find(incrementalSize), incrementalSize.size());
	expect(!parseCatalog(incompleteIncremental).succeeded(),
		"an incremental resource package declares path, size and CRC together");

	std::string invalid = validCatalog();
	const std::size_t displayName = invalid.find("Name=月影传说");
	invalid.replace(displayName,
		std::string("Name=月影传说").size(),
		"Name=bad\tname");
	expect(!parseCatalog(invalid).succeeded(),
		"resource display metadata rejects control characters");
	expect(parsed.catalog.programPackages.at("windows").versionText == "2.1.0",
		"program semantic version parses exactly");

	const CatalogParseResult resourceOnly = parseCatalog(
		"[Catalog]\n"
		"SchemaVersion=1\n"
		"[Resource.YYCS]\n"
		"Version=1.0\n"
		"MinimumEngineVersion=2.0.0\n"
		"Artifact=yycs.zip\n"
		"Size=1\n"
		"Crc32=00000000\n"
		"ResourceOnly=1\n");
	expect(resourceOnly.succeeded() &&
		resourceOnly.catalog.resourcePackages.size() == 1 &&
		resourceOnly.catalog.resourcePackages.at("yycs").resourceOnly &&
		resourceOnly.catalog.programPackages.empty(),
		"resource-only packages parse without requiring program entries");

	const CatalogParseResult applicationOnly = parseCatalog(
		"[Catalog]\n"
		"SchemaVersion=1\n"
		"ProgramTargets=windows,android,macos,ios,linux\n"
		"[Program.windows]\n"
		"Version=2.0.0\n"
		"Artifact=windows.zip\n"
		"Size=1\n"
		"Crc32=00000000\n"
		"[Program.android]\n"
		"Version=2.0.0\n"
		"Artifact=android.apk\n"
		"Size=1\n"
		"Crc32=00000000\n"
		"[Program.macos]\n"
		"Version=2.0.0\n"
		"Artifact=appcast.xml\n"
		"Size=1\n"
		"Crc32=00000000\n"
		"[Program.ios]\n"
		"Version=2.0.0\n"
		"Artifact=install.html\n"
		"Size=1\n"
		"Crc32=00000000\n"
		"[Program.linux]\n"
		"Version=2.0.0\n"
		"Artifact=linux.zip\n"
		"Size=1\n"
		"Crc32=00000000\n");
	expect(applicationOnly.succeeded() &&
		applicationOnly.catalog.resourcePackages.empty() &&
		applicationOnly.catalog.programPackages.size() == 5,
		"application catalog supports every platform without resource entries");

	const CatalogParseResult unsupportedFutureSchema = parseCatalog(
		"[Catalog]\n"
		"SchemaVersion=2\n"
		"ProgramTargets=windows\n"
		"[Program.windows]\n"
		"Version=2.0.0\n"
		"Artifact=windows.zip\n"
		"Size=1\n"
		"Crc32=00000000\n");
	expect(!unsupportedFutureSchema.succeeded(),
		"an undefined future catalog schema is rejected");

	std::string duplicateProgramTarget = validCatalog();
	const std::string programTargets =
		"ProgramTargets=windows,android";
	duplicateProgramTarget.replace(
		duplicateProgramTarget.find(programTargets),
		programTargets.size(),
		"ProgramTargets=windows,android,windows");
	expect(!parseCatalog(duplicateProgramTarget).succeeded(),
		"each platform target declares exactly one online program package");

	expect(isValidOnlineGameId("YYCS") &&
		isValidOnlineGameId("STORY_MOD") &&
		isValidOnlineGameId("MOD..STORY") &&
		isValidOnlineGameId(u8"月影自定义") &&
		!isValidOnlineGameId("YYCS,TEST") &&
		!isValidOnlineGameId("[YYCS]") &&
		!isValidOnlineGameId(" YYCS") &&
		foldGameId("YyCs") == "yycs",
		"online Game.Id only rejects delimiters that break the catalog");
	expect(isSafeArtifactPath("resources/pack.zip") &&
		!isSafeArtifactPath("../pack.zip") &&
		!isSafeArtifactPath("https://host/pack.zip") &&
		!isSafeArtifactPath("resources\\pack.zip"),
		"artifact paths are confined relative URL paths");
	expect(isValidCrc32Hex("A1B2C3D4"),
		"CRC32 parser accepts eight hexadecimal characters");

	invalid = validCatalog();
	const std::size_t dependency = invalid.find("Dependencies=jxqy2");
	invalid.replace(dependency,
		std::string("Dependencies=jxqy2").size(),
		"Dependencies=MISSING_BASE");
	const CatalogParseResult missingDependency = parseCatalog(invalid);
	expect(!missingDependency.succeeded() &&
		missingDependency.catalog.resourcePackages.empty(),
		"unknown dependencies fail closed without a partial catalog");

	invalid = validCatalog();
	const std::size_t cycleSource = invalid.find("Crc32=00000000\n");
	invalid.insert(
		cycleSource + std::string("Crc32=00000000\n").size(),
		"Dependencies=STORY_MOD\n");
	const CatalogParseResult dependencyCycle = parseCatalog(invalid);
	expect(!dependencyCycle.succeeded() &&
		!dependencyCycle.issues.empty() &&
		dependencyCycle.issues.front().error ==
			CatalogParseError::DependencyCycle &&
		dependencyCycle.catalog.resourcePackages.empty(),
		"dependency cycles fail catalog parsing before download planning");

	invalid = validCatalog();
	const std::size_t artifact = invalid.find(
		"Artifact=resources/yycs.zip");
	invalid.replace(artifact,
		std::string("Artifact=resources/yycs.zip").size(),
		"Artifact=../outside.zip");
	expect(!parseCatalog(invalid).succeeded(),
		"unsafe artifact paths fail catalog parsing");

	invalid = validCatalog();
	const std::size_t engineVersion = invalid.find(
		"MinimumEngineVersion=2.0.0");
	invalid.replace(engineVersion,
		std::string("MinimumEngineVersion=2.0.0").size(),
		"MinimumEngineVersion=2.00.0");
	expect(!parseCatalog(invalid).succeeded(),
		"minimum engine versions still require strict SemVer");

	invalid = validCatalog() +
		"\n[Resource.yycs]\n"
		"Version=duplicate\n"
		"MinimumEngineVersion=2.0.0\n"
		"Artifact=resources/duplicate.zip\n"
		"Size=1\n"
		"Crc32=55555555\n";
	expect(!parseCatalog(invalid).succeeded(),
		"resource section IDs are unique ignoring ASCII case");

	invalid = validCatalog() +
		"\n[COMMON]\n"
		"Version=duplicate\n"
		"Artifact=resources/common-duplicate.zip\n"
		"Size=1\n"
		"Crc32=55555555\n";
	expect(!parseCatalog(invalid).succeeded(),
		"the optional Common section is unique ignoring ASCII case");
}

void testCommonVersionMarker()
{
	using namespace OnlineUpdate;
	std::string version;
	expect(parseCommonPackageVersion(
		"[Common]\nVersion= 1.1 \n", version) && version == "1.1",
		"common version marker parses its trimmed display version");
	expect(!parseCommonPackageVersion(
		"[Common]\nVersion=bad\tversion\n", version) && version.empty(),
		"common version marker rejects control characters");
	expect(!parseCommonPackageVersion(
		std::string(MaximumCommonVersionFileBytes + 1, 'x'), version),
		"common version marker keeps a small fixed read bound");
	CommonPackageInstallation installation;
	expect(parseCommonPackageInstallation(
		"[Common]\nVersion=1.1\nInstalledArtifactCrc32=ABCDEF12\n",
		installation) && installation.versionText == "1.1" &&
		installation.installedArtifactCrc32 == "abcdef12",
		"common installation receipt parses and normalizes its artifact CRC");
	expect(parseCommonPackageInstallation(
		"[Common]\nVersion=1.1\nInstalledArtifactCrc32=invalid\n",
		installation) && installation.versionText == "1.1" &&
		installation.installedArtifactCrc32.empty(),
		"a malformed common receipt requests repair without hiding common");

	Catalog catalog;
	expect(!commonPackageNeedsDownload(catalog, "abcdef12"),
		"a catalog without common does not request a common download");
	CommonPackage common;
	common.versionText = "1.1";
	common.crc32Hex = "abcdef12";
	catalog.commonPackage = common;
	expect(!commonPackageNeedsDownload(catalog, "ABCDEF12") &&
		commonPackageNeedsDownload(catalog, "00000000") &&
		commonPackageNeedsDownload(catalog, ""),
		"common downloads only when its installed artifact receipt differs");
}

void testCatalogLimits()
{
	using namespace OnlineUpdate;
	const CatalogParseResult oversized = parseCatalog(
		std::string(MaximumCatalogBytes + 1, 'x'));
	expect(!oversized.succeeded() &&
		!oversized.issues.empty() &&
		oversized.issues.front().error == CatalogParseError::TooLarge &&
		oversized.catalog.resourcePackages.empty() &&
		oversized.catalog.programPackages.empty(),
		"catalog input larger than the fixed memory bound is rejected");

	std::string tooManyPackages =
		"[Catalog]\n"
		"SchemaVersion=1\n";
	for (std::size_t index = 0;
		index <= MaximumResourcePackageCount;
		index++)
	{
		const std::string identifier = "R" + std::to_string(index);
		tooManyPackages +=
			"\n[Resource." + identifier + "]\n"
			"Version=1\n"
			"MinimumEngineVersion=2.0.0\n"
			"Artifact=resources/" + identifier + ".zip\n"
			"Size=1\n"
			"Crc32=00000000\n";
	}
	const CatalogParseResult tooMany = parseCatalog(tooManyPackages);
	expect(!tooMany.succeeded() &&
		!tooMany.issues.empty() &&
		tooMany.issues.front().error == CatalogParseError::TooManyPackages &&
		tooMany.catalog.resourcePackages.empty(),
		"catalog resource package count is bounded independently of byte size");
}

void testResourcePlanning()
{
	using namespace OnlineUpdate;
	Catalog catalog = parseCatalog(validCatalog()).catalog;
	ResourceDownloadPlan plan = planResourceDownload(
		catalog, "story_mod", "2.0.0");
	expect(plan.succeeded() && plan.downloadOrder.size() == 3 &&
		plan.downloadOrder[0].package->gameId == "JXQY2" &&
		plan.downloadOrder[1].package->gameId == "YYCS" &&
		plan.downloadOrder[2].package->gameId == "STORY_MOD" &&
		plan.downloadOrder[1].artifactKind ==
			ResourceDownloadPlan::ArtifactKind::FullAndIncremental &&
		plan.downloadOrder[1].downloadSize == 220 &&
		plan.totalDownloadBytes == 620,
		"missing receipts select full packages and every declared incremental"
		" overlay for the dependency closure");

	InstalledResourceArtifactMap installed;
	installed["JXQY2"] = { "00000000", "", true };
	installed["YYCS"] = { "11111111", "aaaaaaaa", true };
	plan = planResourceDownload(catalog, "STORY_MOD", "2.0.0", installed);
	expect(plan.succeeded() && plan.downloadOrder.size() == 1 &&
		plan.downloadOrder.front().package->gameId == "STORY_MOD" &&
		plan.totalDownloadBytes == 300,
		"matching full and incremental receipts reuse dependencies");

	installed["YYCS"].incrementalArtifactCrc32.clear();
	plan = planResourceDownload(catalog, "STORY_MOD", "2.0.0", installed);
	expect(plan.succeeded() && plan.downloadOrder.size() == 2 &&
		plan.downloadOrder[0].package->gameId == "YYCS" &&
		plan.downloadOrder[0].artifactKind ==
			ResourceDownloadPlan::ArtifactKind::Incremental &&
		plan.downloadOrder[0].downloadSize == 20 &&
		plan.downloadOrder[1].package->gameId == "STORY_MOD" &&
		plan.totalDownloadBytes == 320,
		"a matching full package downloads only its differing incremental overlay");

	installed["YYCS"] = { "ffffffff", "aaaaaaaa", true };
	plan = planResourceDownload(catalog, "STORY_MOD", "2.0.0", installed);
	expect(plan.succeeded() && plan.downloadOrder.size() == 2 &&
		plan.downloadOrder[0].package->gameId == "YYCS" &&
		plan.downloadOrder[0].artifactKind ==
			ResourceDownloadPlan::ArtifactKind::FullAndIncremental &&
		plan.downloadOrder[0].downloadSize == 220 &&
		plan.totalDownloadBytes == 520,
		"a full-package mismatch downloads the full package followed by its"
		" declared incremental overlay");

	installed["YYCS"] = { "11111111", "", false };
	plan = planResourceDownload(catalog, "STORY_MOD", "2.0.0", installed);
	expect(plan.succeeded() && plan.downloadOrder.size() == 2 &&
		plan.downloadOrder[0].package->gameId == "YYCS" &&
		plan.downloadOrder[0].artifactKind ==
			ResourceDownloadPlan::ArtifactKind::FullAndIncremental &&
		plan.downloadOrder[0].downloadSize == 220,
		"a non-filesystem installation is rebuilt from full and then receives"
		" the declared incremental overlay");

	installed["YYCS"] = { "11111111", "aaaaaaaa", true };
	installed["STORY_MOD"] = { "22222222", "", true };
	plan = planResourceDownload(catalog, "STORY_MOD", "2.0.0", installed);
	expect(plan.succeeded() && plan.downloadOrder.empty() &&
		plan.totalDownloadBytes == 0,
		"a fully matching closure does not download an artifact");
	plan = planResourceDownload(
		catalog,
		"STORY_MOD",
		"2.0.0",
		installed,
		RequestedResourceDownloadMode::ForceFullPackage);
	expect(plan.succeeded() && plan.downloadOrder.size() == 1 &&
		plan.downloadOrder.front().package->gameId == "STORY_MOD" &&
		plan.downloadOrder.front().artifactKind ==
			ResourceDownloadPlan::ArtifactKind::Full &&
		plan.totalDownloadBytes == 300,
		"an explicit reinstall forces only the requested full package");
	plan = planResourceDownload(
		catalog,
		"YYCS",
		"2.0.0",
		installed,
		RequestedResourceDownloadMode::ForceFullPackage);
	expect(plan.succeeded() && plan.downloadOrder.size() == 1 &&
		plan.downloadOrder.front().package->gameId == "YYCS" &&
		plan.downloadOrder.front().artifactKind ==
			ResourceDownloadPlan::ArtifactKind::FullAndIncremental &&
		plan.totalDownloadBytes == 220,
		"an explicit reinstall also downloads a declared incremental overlay");

	catalog.resourcePackages.at("jxqy2").resourceOnly = true;
	plan = planResourceDownload(catalog, "story_mod", "2.0.0");
	expect(plan.succeeded() && plan.downloadOrder.size() == 3 &&
		plan.downloadOrder.front().package->resourceOnly,
		"resource-only packages remain available inside dependency closures");
	plan = planResourceDownload(catalog, "jxqy2", "2.0.0");
	expect(plan.status == ResourcePlanStatus::ResourceOnlyTarget &&
		plan.downloadOrder.empty(),
		"resource-only packages cannot be requested as a launch target");
	catalog.resourcePackages.at("jxqy2").resourceOnly = false;

	plan = planResourceDownload(catalog, "STORY_MOD", "1.9.9");
	expect(plan.status == ResourcePlanStatus::RequiresNewerEngine &&
		plan.downloadOrder.empty() && plan.totalDownloadBytes == 0,
		"planner rejects resources that require a newer engine");

	catalog.resourcePackages.at("jxqy2").dependencyGameIds.push_back(
		"STORY_MOD");
	plan = planResourceDownload(catalog, "STORY_MOD", "2.0.0");
	expect(plan.status == ResourcePlanStatus::DependencyCycle &&
		plan.downloadOrder.empty() && plan.totalDownloadBytes == 0,
		"planner rejects recursive dependency cycles without a partial plan");

	plan = planResourceDownload(catalog, "MISSING", "2.0.0");
	expect(plan.status == ResourcePlanStatus::TargetNotFound,
		"planner reports a missing requested Game.Id");

	catalog = parseCatalog(validCatalog()).catalog;
	catalog.resourcePackages.at("jxqy2").artifactSize =
		std::numeric_limits<std::uint64_t>::max();
	plan = planResourceDownload(catalog, "STORY_MOD", "2.0.0");
	expect(plan.status == ResourcePlanStatus::TotalSizeOverflow &&
		plan.downloadOrder.empty() && plan.totalDownloadBytes == 0,
		"planner rejects an overflowing dependency download total");
}

void testProgramUpdateCheck()
{
	using namespace OnlineUpdate;
	const Catalog catalog = parseCatalog(validCatalog()).catalog;

	ProgramUpdateCheck check = checkProgramUpdate(
		catalog, "windows", "2.0.9");
	expect(check.hasUpdate() && check.package &&
		check.package->versionText == "2.1.0" &&
		check.versionComparison > 0,
		"a newer semantic Version is offered as a program update");

	check = checkProgramUpdate(catalog, "windows", "2.1.0");
	expect(check.status == ProgramUpdateStatus::UpToDate &&
		check.package && !check.hasUpdate() &&
		check.hasOnlinePackage() && check.versionComparison == 0,
		"the sole online package remains available at an equal Version");

	check = checkProgramUpdate(catalog, "windows", "2.2.0");
	expect(check.status == ProgramUpdateStatus::UpToDate &&
		check.package && !check.hasUpdate() &&
		check.hasOnlinePackage() && check.versionComparison < 0,
		"the sole online package remains available below the current Version");

	check = checkProgramUpdate(catalog, "linux", "2.0.0");
	expect(check.status == ProgramUpdateStatus::TargetNotFound &&
		check.package == nullptr,
		"a catalog without the exact platform target is reported plainly");

	check = checkProgramUpdate(catalog, "", "2.0.0");
	expect(check.status == ProgramUpdateStatus::InvalidInput,
		"an unsupported current platform target is rejected");

	check = checkProgramUpdate(catalog, "windows", "not-semver");
	expect(check.status == ProgramUpdateStatus::InvalidInput,
		"an invalid current semantic version is rejected");
}

void testArtifactHashing()
{
	using namespace OnlineUpdate;
	std::uint32_t checksum = 0;
	const std::string abc = "abc";
	expect(calculateCrc32(
		reinterpret_cast<const std::uint8_t*>(abc.data()),
		abc.size(), checksum) &&
		crc32ToLowerHex(checksum) == "352441c2",
		"miniz CRC32 matches the published abc vector");
	checksum = 1;
	expect(!calculateCrc32(nullptr, 1, checksum) && checksum == 0,
		"invalid CRC32 input does not retain a stale checksum");
}
}

int main()
{
	testCatalogParsing();
	testCommonVersionMarker();
	testCatalogLimits();
	testResourcePlanning();
	testProgramUpdateCheck();
	testArtifactHashing();
	if (failureCount != 0)
	{
		std::cerr << failureCount << " online update test(s) failed"
			<< std::endl;
		return 1;
	}
	std::cout << "All online update catalog tests passed" << std::endl;
	return 0;
}
