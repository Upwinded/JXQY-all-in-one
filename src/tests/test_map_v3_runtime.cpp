#include "../Game/Data/Map.h"
#include "../File/File.h"
#include "MapV3ContractFixture.h"
#include "TestTemporaryDirectory.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <string>
#include <utility>

namespace
{
constexpr char MixedCaseMpcPath[] = "MPC/Map/Shared-Scene/";
constexpr char MixedCaseMpcName[] = "Shared-Tile.MPC";

std::vector<uint8_t> buildVersion2Map(bool utf8Strings)
{
	constexpr size_t headerLength = 192;
	constexpr size_t nameLength = 32;
	constexpr size_t infoLength = 64;
	constexpr size_t mpcCount = 255;
	constexpr size_t tileLength = 10;
	std::vector<uint8_t> buffer(headerLength + mpcCount * infoLength + tileLength, 0);
	std::memcpy(buffer.data(), "MAP File Ver2.0", 16);
	MapV3ContractFixture::writeInt32(buffer, 64, static_cast<int32_t>(tileLength));
	MapV3ContractFixture::writeInt32(buffer, 68, 1);
	MapV3ContractFixture::writeInt32(buffer, 72, 1);
	MapV3ContractFixture::writeInt32(buffer, 76, static_cast<int32_t>(infoLength));
	MapV3ContractFixture::writeInt32(buffer, 80, static_cast<int32_t>(nameLength));

	const uint8_t utf8Path[] = {'m', 'p', 'c', '/', 0xE4, 0xB8, 0xAD, '/'};
	const uint8_t gbkPath[] = {'m', 'p', 'c', '/', 0xD6, 0xD0, '/'};
	const uint8_t utf8Name[] = {0xE4, 0xB8, 0xAD, '.', 'm', 'p', 'c'};
	const uint8_t gbkName[] = {0xD6, 0xD0, '.', 'm', 'p', 'c'};
	const uint8_t* path = utf8Strings ? utf8Path : gbkPath;
	const size_t pathLength = utf8Strings ? sizeof(utf8Path) : sizeof(gbkPath);
	const uint8_t* name = utf8Strings ? utf8Name : gbkName;
	const size_t mapNameLength = utf8Strings ? sizeof(utf8Name) : sizeof(gbkName);
	std::memcpy(buffer.data() + 32, path, pathLength);
	std::memcpy(buffer.data() + headerLength, name, mapNameLength);

	const size_t tileOffset = headerLength + mpcCount * infoLength;
	for (int layer = 0; layer < 3; ++layer)
	{
		buffer[tileOffset + layer * 2] = MapV3ContractFixture::LayerFrames[layer];
		buffer[tileOffset + layer * 2 + 1] = MapV3ContractFixture::LayerMpcs[layer];
	}
	buffer[tileOffset + 6] = MapV3ContractFixture::TileObstacle;
	buffer[tileOffset + 7] = MapV3ContractFixture::TileTrap;
	buffer[tileOffset + 8] = MapV3ContractFixture::TileEnd[0];
	buffer[tileOffset + 9] = MapV3ContractFixture::TileEnd[1];
	return buffer;
}

std::unique_ptr<char[]> copyToInput(const std::vector<uint8_t>& buffer)
{
	std::unique_ptr<char[]> input = std::make_unique<char[]>(buffer.size());
	std::memcpy(input.get(), buffer.data(), buffer.size());
	return input;
}

std::vector<uint8_t> makeOnePixelMpc()
{
	constexpr size_t headerSize = 128;
	constexpr size_t paletteSize = 4;
	constexpr size_t offsetTableSize = 4;
	constexpr size_t pictureHeaderSize = 20;
	const std::vector<uint8_t> payload = { 1, 0 };
	std::vector<uint8_t> mpc(
		headerSize + paletteSize + offsetTableSize +
		pictureHeaderSize + payload.size(),
		0);
	std::memcpy(mpc.data(), "MPC File Ver2.0", 16);
	MapV3ContractFixture::writeInt32(mpc, 68, 1);
	MapV3ContractFixture::writeInt32(mpc, 72, 1);
	MapV3ContractFixture::writeInt32(mpc, 76, 1);
	MapV3ContractFixture::writeInt32(mpc, 80, 8);
	MapV3ContractFixture::writeInt32(mpc, 84, 1);
	MapV3ContractFixture::writeInt32(mpc, 88, 66);
	const size_t frameOffset =
		headerSize + paletteSize + offsetTableSize;
	MapV3ContractFixture::writeInt32(
		mpc,
		headerSize + paletteSize,
		0);
	MapV3ContractFixture::writeInt32(
		mpc,
		frameOffset,
		static_cast<int32_t>(pictureHeaderSize + payload.size()));
	MapV3ContractFixture::writeInt32(mpc, frameOffset + 4, 1);
	MapV3ContractFixture::writeInt32(mpc, frameOffset + 8, 1);
	std::copy(
		payload.begin(),
		payload.end(),
		mpc.begin() + frameOffset + pictureHeaderSize);
	return mpc;
}

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

}

bool runMapV3RuntimeTests()
{
	std::vector<uint8_t> buffer = MapV3ContractFixture::build();
	std::memcpy(
		buffer.data() + MapV3ContractFixture::BaseHeaderLength,
		MixedCaseMpcPath,
		sizeof(MixedCaseMpcPath) - 1);
	std::memcpy(
		buffer.data() + MapV3ContractFixture::HeaderLength,
		MixedCaseMpcName,
		sizeof(MixedCaseMpcName) - 1);
	const size_t duplicateMpcOffset =
		static_cast<size_t>(MapV3ContractFixture::HeaderLength) +
		static_cast<size_t>(MapV3ContractFixture::InfoLength);
	std::memcpy(
		buffer.data() + duplicateMpcOffset,
		MixedCaseMpcName,
		sizeof(MixedCaseMpcName) - 1);

	auto resourceRoot = makeUniqueTestDirectory("jxqy_map_v3_runtime_test");
	std::error_code errorCode;
	std::filesystem::remove_all(resourceRoot, errorCode);
	std::filesystem::create_directories(resourceRoot, errorCode);
	File::setAssetsCollectionRoot(resourceRoot.string());
	File::setActiveResourceRoot(resourceRoot.string());
	File::setResourceFallbackRoots({});
	const std::filesystem::path mapMpcPath =
		resourceRoot / "mpc" / "map" / "shared-scene" /
		MapV3ContractFixture::MpcName;
	std::filesystem::create_directories(mapMpcPath.parent_path(), errorCode);
	{
		const std::vector<uint8_t> mpc = makeOnePixelMpc();
		std::ofstream mpcOutput(mapMpcPath, std::ios::binary);
		mpcOutput.write(
			reinterpret_cast<const char*>(mpc.data()),
			static_cast<std::streamsize>(mpc.size()));
		if (!mpcOutput.good())
		{
			std::filesystem::remove_all(resourceRoot, errorCode);
			return check(false, "write worker-prepared MPC fixture");
		}
	}

	std::unique_ptr<char[]> input = copyToInput(buffer);

	bool ok = true;
	{
		Map map;
		ok = check(map.load(input, static_cast<int>(buffer.size())),
			"runtime loads a complete MAP File Ver3.0 buffer") && ok;
		if (map.data == nullptr)
		{
			std::filesystem::remove_all(resourceRoot, errorCode);
			return check(false, "runtime publishes parsed MAP File Ver3.0 data");
		}

		const MpcInfo& mpc = map.data->mpc.mpc[0];
		const MapTile& tile = map.data->tile[0][0];
		ok = check(map.data->head.width == 1 && map.data->head.height == 1,
			"runtime preserves MAP File Ver3.0 dimensions") && ok;
		ok = check(map.data->mpcPath == MapV3ContractFixture::MpcPath,
			"runtime canonicalizes ASCII case in the MAP File Ver3.0 MPC path") && ok;
		ok = check(mpc.name != nullptr &&
			std::string(mpc.name.get()) == MapV3ContractFixture::MpcName &&
			mpc.index == MapV3ContractFixture::MpcIndex &&
			mpc.dynamic == MapV3ContractFixture::MpcDynamic &&
			mpc.obstacle == MapV3ContractFixture::MpcObstacle &&
			mpc.nil == MapV3ContractFixture::MpcNil,
			"runtime canonicalizes ASCII case and reads MAP File Ver3.0 MPC metadata") && ok;
		ok = check(tile.layer[0].frame == MapV3ContractFixture::LayerFrames[0] &&
			tile.layer[0].mpc == MapV3ContractFixture::LayerMpcs[0] &&
			tile.layer[1].frame == MapV3ContractFixture::LayerFrames[1] &&
			tile.layer[1].mpc == MapV3ContractFixture::LayerMpcs[1] &&
			tile.layer[2].frame == MapV3ContractFixture::LayerFrames[2] &&
			tile.layer[2].mpc == MapV3ContractFixture::LayerMpcs[2] &&
			tile.obstacle == MapV3ContractFixture::TileObstacle &&
			tile.trap == MapV3ContractFixture::TileTrap &&
			tile.end[0] == MapV3ContractFixture::TileEnd[0] &&
			tile.end[1] == MapV3ContractFixture::TileEnd[1],
			"runtime reads all MAP File Ver3.0 tile fields") && ok;
		ok = check(
			map.mapMpc != nullptr &&
			map.mapMpc->mpc[0].img != nullptr &&
			map.mapMpc->mpc[1].img == map.mapMpc->mpc[0].img &&
			map.mapMpc->mpc[2].img == nullptr,
			"runtime skips empty MPC slots and reuses one decoded image for duplicate normalized paths") &&
			ok;

		const std::shared_ptr<MapData> version3Data = map.data;
		const std::shared_ptr<MapMpc> version3Mpc = map.mapMpc;
		const _shared_imp version3FirstMpc = map.mapMpc->mpc[0].img;
		std::unique_ptr<char[]> cancelledPreparationInput =
			copyToInput(buffer);
		int preparationCheckpointCount = 0;
		int mutationCount = 0;
		ok = check(
			!map.load(
				cancelledPreparationInput,
				static_cast<int>(buffer.size()),
				[&mutationCount]()
				{
					++mutationCount;
				},
				[&preparationCheckpointCount]()
				{
					return ++preparationCheckpointCount < 4;
				}) &&
				preparationCheckpointCount == 4 &&
				mutationCount == 0 &&
				map.data == version3Data &&
				map.mapMpc == version3Mpc,
			"runtime map preparation checkpoints can cancel inside the MPC loop before mutation") &&
			ok;

		const std::vector<uint8_t> originalVersion2 = buildVersion2Map(false);
		std::unique_ptr<char[]> originalVersion2Input = copyToInput(originalVersion2);
		ok = check(!map.load(originalVersion2Input,
			static_cast<int>(originalVersion2.size())) &&
			map.data == version3Data && map.mapMpc == version3Mpc,
			"runtime rejects complete original GBK MAP File Ver2.0 transactionally") && ok;

		const std::vector<uint8_t> transitionalVersion2 = buildVersion2Map(true);
		std::unique_ptr<char[]> transitionalVersion2Input = copyToInput(transitionalVersion2);
		ok = check(!map.load(transitionalVersion2Input,
			static_cast<int>(transitionalVersion2.size())) &&
			map.data == version3Data && map.mapMpc == version3Mpc,
			"runtime rejects complete UTF-8 transitional MAP File Ver2.0 transactionally") && ok;

		std::vector<uint8_t> invalidPathVersion3 = MapV3ContractFixture::build();
		invalidPathVersion3[MapV3ContractFixture::BaseHeaderLength] = 0xD6;
		invalidPathVersion3[MapV3ContractFixture::BaseHeaderLength + 1] = 0xD0;
		invalidPathVersion3[MapV3ContractFixture::BaseHeaderLength + 2] = 0;
		std::unique_ptr<char[]> invalidPathInput = copyToInput(invalidPathVersion3);
		ok = check(!map.load(invalidPathInput,
			static_cast<int>(invalidPathVersion3.size())) &&
			map.data == version3Data && map.mapMpc == version3Mpc,
			"runtime rejects MAP File Ver3.0 with a non-UTF-8 MPC path") && ok;

		std::vector<uint8_t> invalidNameVersion3 = MapV3ContractFixture::build();
		invalidNameVersion3[MapV3ContractFixture::HeaderLength] = 0xD6;
		invalidNameVersion3[MapV3ContractFixture::HeaderLength + 1] = 0xD0;
		invalidNameVersion3[MapV3ContractFixture::HeaderLength + 2] = 0;
		std::unique_ptr<char[]> invalidNameInput = copyToInput(invalidNameVersion3);
		ok = check(!map.load(invalidNameInput,
			static_cast<int>(invalidNameVersion3.size())) &&
			map.data == version3Data && map.mapMpc == version3Mpc,
			"runtime rejects MAP File Ver3.0 with a non-UTF-8 MPC name") && ok;

		const std::filesystem::path preparedMapPath =
			resourceRoot / "prepared.map";
		std::vector<uint8_t> workerPreparedBuffer = buffer;
		std::fill(
			workerPreparedBuffer.begin() +
				MapV3ContractFixture::BaseHeaderLength,
			workerPreparedBuffer.begin() +
				MapV3ContractFixture::HeaderLength,
			0);
		{
			std::ofstream preparedMapOutput(
				preparedMapPath,
				std::ios::binary);
			preparedMapOutput.write(
				reinterpret_cast<const char*>(workerPreparedBuffer.data()),
				static_cast<std::streamsize>(workerPreparedBuffer.size()));
			ok = check(
				preparedMapOutput.good(),
				"write prepared MAP candidate fixture") &&
				ok;
		}
		Map::PreparedLoadCandidate preparedCandidate;
		bool preparedOnWorker = false;
		const std::thread::id ownerThreadId =
			std::this_thread::get_id();
		std::thread prepareWorker(
			[&]()
			{
				preparedOnWorker =
					std::this_thread::get_id() != ownerThreadId &&
					Map::prepareLoadCandidate(
						"prepared.map",
						preparedCandidate,
						{},
						"mpc/map/shared-scene");
			});
		prepareWorker.join();
		ok = check(
			preparedOnWorker,
			"worker-facing MAP preparation parses the source and decodes fallback-folder MPC data once") &&
			ok;
		std::filesystem::remove(preparedMapPath, errorCode);
		std::filesystem::remove(mapMpcPath, errorCode);
		int preparedMutationCount = 0;
		ok = check(
			map.commitPreparedLoadCandidate(
				std::move(preparedCandidate),
				[&preparedMutationCount]()
				{
					++preparedMutationCount;
				}) &&
				preparedMutationCount == 1 &&
				map.data != version3Data &&
				map.data != nullptr &&
				map.data->head.width == 1 &&
				map.data->head.height == 1 &&
				map.mapMpc != nullptr &&
				map.mapMpc->mpc[0].img != nullptr &&
				map.mapMpc->mpc[0].img != version3FirstMpc &&
				map.mapMpc->mpc[1].img == map.mapMpc->mpc[0].img,
			"a prepared MAP candidate commits after MAP and MPC sources are removed without rereading or redecoding them") &&
			ok;
		if (map.data != nullptr && map.mapMpc != nullptr &&
			map.mapMpc->mpc[0].img != nullptr)
		{
			_shared_imp visibleImage = map.mapMpc->mpc[0].img;
			visibleImage->directions = 1;
			visibleImage->interval = 100;
			visibleImage->frame.clear();
			visibleImage->frame.resize(3);
			for (IMPFrame& frame : visibleImage->frame)
			{
				frame.pixelWidth = 1;
				frame.pixelHeight = 1;
				frame.pixelData = { 0, 0, 0, 0 };
			}
			map.data->mpc.mpc[0].dynamic = 0;
			for (int layer = 0; layer < MAP_TILE_LAYER; ++layer)
			{
				map.data->tile[0][0].layer[layer].mpc = 1;
				map.data->tile[0][0].layer[layer].frame = 0;
			}
			std::size_t warmedFrameCount = 0;
			std::size_t attemptedFrameCount = 0;
			int warmupCheckpointCount = 0;
			ok = check(
				map.warmVisibleTextures(
					1280,
					720,
					{ 0, 0 },
					[&warmupCheckpointCount]()
					{
						++warmupCheckpointCount;
						return true;
					},
					1,
					&warmedFrameCount,
					&attemptedFrameCount) &&
				attemptedFrameCount == 1 &&
				warmedFrameCount <= attemptedFrameCount &&
				(warmedFrameCount == 0 ||
				 visibleImage->frame[0].image != nullptr) &&
				warmupCheckpointCount == 1,
				"visible texture warmup deduplicates repeated tile frames, reports best-effort attempts and yields at the configured batch boundary") &&
				ok;

			for (int layer = 0; layer < MAP_TILE_LAYER; ++layer)
			{
				map.data->tile[0][0].layer[layer].frame =
					static_cast<unsigned char>(layer);
			}
			warmedFrameCount = 0;
			attemptedFrameCount = 0;
			warmupCheckpointCount = 0;
			ok = check(
				!map.warmVisibleTextures(
					1280,
					720,
					{ 0, 0 },
					[&warmupCheckpointCount]()
					{
						++warmupCheckpointCount;
						return false;
					},
					1,
					&warmedFrameCount,
					&attemptedFrameCount) &&
				attemptedFrameCount == 1 &&
				warmedFrameCount <= attemptedFrameCount &&
				warmupCheckpointCount == 1,
				"visible texture warmup honors owner cancellation between texture batches") &&
				ok;
		}
		map.createDataMap();
		ok = check(
			map.dataMap.tile.size() == 1 &&
			map.dataMap.tile.front().size() == 1,
			"owner commit consumes the worker-prepared DataMap grid") &&
			ok;
	}

	std::filesystem::remove_all(resourceRoot, errorCode);
	return ok;
}
