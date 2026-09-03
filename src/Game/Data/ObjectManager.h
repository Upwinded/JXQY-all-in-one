#pragma once
#include "Object.h"
#include "ImageResourcePathResolver.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <deque>

inline std::vector<std::string> buildObjectImageResourceCandidates(const std::string& imageName)
{
	return buildImageResourceCandidatesForCategory(
		imageName,
		"object",
		OBJECT_RES_FOLDER_ASF,
		OBJECT_RES_FOLDER);
}

class INIReader;
class ObjectManager;

class PreparedObjectLoad final
{
public:
	PreparedObjectLoad() = default;
	PreparedObjectLoad(PreparedObjectLoad&& other) noexcept = default;
	PreparedObjectLoad& operator=(PreparedObjectLoad&& other) noexcept = default;

	PreparedObjectLoad(const PreparedObjectLoad&) = delete;
	PreparedObjectLoad& operator=(const PreparedObjectLoad&) = delete;

	bool isPrepared() const noexcept;
	std::size_t objectCount() const noexcept;

private:
	std::shared_ptr<INIReader> reader;
	int count = 0;

	friend class ObjectManager;
};

class ObjectManager:
	public Element
{
private:
	void releaseManagedObjects(bool clearObjectImages);

public:
	ObjectManager();
	virtual ~ObjectManager();

	int clickIndex = -1;

	bool findObj(std::shared_ptr<Object> object);
	std::shared_ptr<Object> findObj(const std::string & name);
	std::shared_ptr<Object> findNearestScriptViewObj(Point pos, int radius);
	std::vector<std::shared_ptr<Object>> findRadiusScriptViewObj(Point pos, int radius);
	bool drawOBJSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawOBJ(std::shared_ptr<Object> obj, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle);

	std::vector<std::shared_ptr<Object>> objectList;

	void deleteObject(std::string nName);
	void deleteObject(std::shared_ptr<Object> object);
	void deleteObjectFromOtherPlace(std::shared_ptr<Object> obj);
	std::shared_ptr<Object> addObject(std::string iniName, int x, int y, int dir, PointEx offset = { 0, 0 });
	std::vector<std::shared_ptr<Object>> takeBodiesInRadius(Point pos, int radius);
	void clearBody();
	void clearObj();
	void checkDamage();

	
	void clearSelected();

	std::map<std::string, _shared_imp> objectImageList;
	void clearObjectImageList();
	void tryCleanObjectImageList();
	_shared_imp loadObjectImage(const std::string & imageName);

	void freeResource();
	virtual bool load(
		const std::string& fileName,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {},
		bool allowIncompleteSectionList = false);
	bool prepareLoad(
		const std::string& fileName,
		PreparedObjectLoad& preparedLoad,
		bool allowIncompleteSectionList = false) const;
	bool prepareExactResourceBytes(
		const std::string& virtualPath,
		const std::vector<std::uint8_t>& bytes,
		PreparedObjectLoad& preparedLoad) const;
	bool commitPreparedLoad(
		PreparedObjectLoad&& preparedLoad,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {});
	bool validate(const std::string& fileName);
	bool loadExactResourceBytes(
		const std::string& virtualPath,
		const std::vector<std::uint8_t>& bytes,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {});
	virtual bool save(const std::string & fileName);
	virtual void onEvent();
	virtual void onUpdate();

protected:
	virtual bool shouldUpdateChild(PElement child) override;
};
