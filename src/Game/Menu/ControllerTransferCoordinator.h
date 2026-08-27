#pragma once

#include "../../Element/Element.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class ControllerSlotKind
{
	Goods,
	Magic,
	PartnerGoods
};

enum class ControllerSlotDomain
{
	GoodsBag,
	GoodsQuick,
	PlayerEquipment,
	MagicList,
	MagicQuick,
	Practice,
	PartnerBag,
	PartnerEquipment
};

struct ControllerSlotAddress
{
	ControllerSlotKind kind = ControllerSlotKind::Goods;
	ControllerSlotDomain domain = ControllerSlotDomain::GoodsBag;
	int logicalIndex = -1;
	std::shared_ptr<const void> context;

	bool operator==(const ControllerSlotAddress& other) const;
};

struct ControllerSlotIdentity
{
	std::shared_ptr<const void> object;
	std::string key;

	bool valid() const;
	bool operator==(const ControllerSlotIdentity& other) const;
};

enum class ControllerTransferSubmitResult
{
	Completed,
	Rejected,
	SourceLost
};

class ControllerTransferCoordinator
{
public:
	struct Policy
	{
		std::vector<ControllerSlotDomain> domainOrder;
		std::function<ControllerSlotIdentity(const ControllerSlotAddress&)>
			identifySource;
		std::function<ControllerTransferSubmitResult(
			const ControllerSlotAddress& source,
			const ControllerSlotAddress& target,
			std::string& message)> submit;
	};

	struct DomainBinding
	{
		ControllerSlotKind kind = ControllerSlotKind::Goods;
		ControllerSlotDomain domain = ControllerSlotDomain::GoodsBag;
		std::function<bool()> activate;
		std::function<void()> deactivate;
		std::function<PElement()> owner;
		std::function<void()> refreshSelection;
	};

	ControllerTransferCoordinator();
	~ControllerTransferCoordinator();

	void clear();
	void setPolicy(ControllerSlotKind kind, Policy policy);
	void registerDomain(DomainBinding binding);
	void setStateChangedHandler(std::function<void()> handler);

	bool start(ControllerSlotKind kind, ControllerSlotDomain domain);
	bool begin(const ControllerSlotAddress& source);
	bool interact(
		const ControllerSlotAddress& target,
		std::string& message);
	ControllerTransferSubmitResult submit(
		const ControllerSlotAddress& target,
		std::string& message);
	bool reactivateCurrentDomain();
	bool cycleDomain(int direction);
	bool cancelSource();
	void cancel();

	bool active() const;
	bool active(ControllerSlotKind kind) const;
	bool hasSource() const;
	bool isSource(const ControllerSlotAddress& address) const;
	std::optional<ControllerSlotAddress> source() const;
	std::optional<ControllerSlotDomain> activeDomain() const;
	PElement activeOwner() const;

private:
	struct PolicyEntry
	{
		ControllerSlotKind kind = ControllerSlotKind::Goods;
		Policy policy;
	};

	std::vector<PolicyEntry> policies;
	std::vector<DomainBinding> domains;
	std::optional<ControllerSlotKind> sessionKind;
	std::optional<ControllerSlotDomain> originDomain;
	std::optional<ControllerSlotDomain> currentDomain;
	std::optional<ControllerSlotAddress> sourceAddress;
	ControllerSlotIdentity sourceIdentity;
	std::function<void()> stateChangedHandler;

	Policy* findPolicy(ControllerSlotKind kind);
	const Policy* findPolicy(ControllerSlotKind kind) const;
	DomainBinding* findDomain(
		ControllerSlotKind kind, ControllerSlotDomain domain);
	const DomainBinding* findDomain(
		ControllerSlotKind kind, ControllerSlotDomain domain) const;
	void clearSourceState();
	void notifyStateChanged();
};
