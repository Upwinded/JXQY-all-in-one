#include "../Game/Menu/ControllerTransferCoordinator.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}
}

int main()
{
	bool ok = true;
	ControllerTransferCoordinator coordinator;
	std::vector<std::shared_ptr<int>> objects =
	{
		std::make_shared<int>(0),
		std::make_shared<int>(1),
		std::make_shared<int>(2),
		std::make_shared<int>(3)
	};
	std::vector<std::string> keys = { "a", "b", "c", "d" };
	std::vector<bool> present = { true, true, true, true };
	int stateChanges = 0;
	int bagActivations = 0;
	int bagDeactivations = 0;
	int quickActivations = 0;
	int quickDeactivations = 0;

	ControllerTransferCoordinator::Policy policy;
	policy.domainOrder =
	{
		ControllerSlotDomain::GoodsBag,
		ControllerSlotDomain::GoodsQuick,
		ControllerSlotDomain::PlayerEquipment
	};
	policy.identifySource = [&objects, &keys, &present](
		const ControllerSlotAddress& address)
	{
		if (address.logicalIndex < 0
			|| address.logicalIndex >= static_cast<int>(present.size())
			|| !present[address.logicalIndex])
		{
			return ControllerSlotIdentity();
		}
		return ControllerSlotIdentity
		{
			std::static_pointer_cast<const void>(objects[address.logicalIndex]),
			keys[address.logicalIndex]
		};
	};
	policy.submit = [](const ControllerSlotAddress&,
		const ControllerSlotAddress& target, std::string& message)
	{
		if (target.logicalIndex == 3)
		{
			message = "target rejected";
			return ControllerTransferSubmitResult::Rejected;
		}
		return ControllerTransferSubmitResult::Completed;
	};
	coordinator.setPolicy(ControllerSlotKind::Goods, std::move(policy));

	ControllerTransferCoordinator::DomainBinding bag;
	bag.kind = ControllerSlotKind::Goods;
	bag.domain = ControllerSlotDomain::GoodsBag;
	bag.activate = [&bagActivations]()
	{
		bagActivations++;
		return true;
	};
	bag.deactivate = [&bagDeactivations]() { bagDeactivations++; };
	bag.owner = []() { return PElement(); };
	coordinator.registerDomain(std::move(bag));

	ControllerTransferCoordinator::DomainBinding quick;
	quick.kind = ControllerSlotKind::Goods;
	quick.domain = ControllerSlotDomain::GoodsQuick;
	quick.activate = [&quickActivations]()
	{
		quickActivations++;
		return true;
	};
	quick.deactivate = [&quickDeactivations]() { quickDeactivations++; };
	quick.owner = []() { return PElement(); };
	coordinator.registerDomain(std::move(quick));
	coordinator.setStateChangedHandler([&stateChanges]() { stateChanges++; });

	std::string message;
	present[3] = false;
	const ControllerSlotAddress emptyBagSlot =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsBag,
		3
	};
	ok = check(coordinator.interact(emptyBagSlot, message),
		"empty slot interaction starts a registered edit domain") && ok;
	ok = check(coordinator.active(ControllerSlotKind::Goods),
		"session records its slot kind") && ok;
	ok = check(!coordinator.hasSource(),
		"empty slot starts without inventing a source") && ok;
	const ControllerSlotAddress wrongKindSlot =
	{
		ControllerSlotKind::Magic,
		ControllerSlotDomain::MagicList,
		0
	};
	ok = check(!coordinator.interact(wrongKindSlot, message)
		&& coordinator.active(ControllerSlotKind::Goods),
		"an active edit session rejects another slot kind") && ok;

	const ControllerSlotAddress bagSource =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsBag,
		0
	};
	ok = check(coordinator.begin(bagSource),
		"existing slot becomes the unique source") && ok;
	ok = check(coordinator.isSource(bagSource),
		"source address is queryable for highlighting") && ok;
	auto otherContext = std::make_shared<int>(99);
	ControllerSlotAddress contextScopedAddress = bagSource;
	contextScopedAddress.context =
		std::static_pointer_cast<const void>(otherContext);
	ok = check(!coordinator.isSource(contextScopedAddress),
		"source highlighting does not cross slot contexts") && ok;
	ok = check(coordinator.cycleDomain(1),
		"panel-next cycles to the next registered compatible domain") && ok;
	ok = check(coordinator.activeDomain().has_value()
		&& coordinator.activeDomain().value() == ControllerSlotDomain::GoodsQuick,
		"quick domain becomes active") && ok;
	ok = check(bagDeactivations == 1 && quickActivations == 1,
		"domain switch suspends the old domain and activates the new one") && ok;
	ok = check(coordinator.reactivateCurrentDomain()
		&& quickActivations == 2
		&& coordinator.activeDomain().has_value()
		&& coordinator.activeDomain().value()
			== ControllerSlotDomain::GoodsQuick,
		"current transfer domain can restore focus without changing session state") && ok;

	const ControllerSlotAddress rejectedTarget =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsQuick,
		3
	};
	ok = check(coordinator.submit(rejectedTarget, message)
		== ControllerTransferSubmitResult::Rejected,
		"policy rejection is returned") && ok;
	ok = check(coordinator.hasSource() && message == "target rejected",
		"rejected submit keeps the source and its reason") && ok;

	const ControllerSlotAddress completedTarget =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsQuick,
		1
	};
	ok = check(coordinator.submit(completedTarget, message)
		== ControllerTransferSubmitResult::Completed,
		"valid submit completes") && ok;
	ok = check(coordinator.active() && !coordinator.hasSource(),
		"completed submit keeps edit domain but clears source") && ok;

	const ControllerSlotAddress quickSource =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsQuick,
		2
	};
	ok = check(coordinator.begin(quickSource),
		"a quick slot can become the next source") && ok;
	keys[2] = "changed";
	ok = check(coordinator.submit(completedTarget, message)
		== ControllerTransferSubmitResult::SourceLost,
		"changed source identity aborts stale submit") && ok;
	ok = check(!coordinator.active(),
		"source loss ends the edit session") && ok;
	ok = check(quickDeactivations == 1 && bagActivations == 2,
		"ending a switched session restores its origin domain") && ok;

	ok = check(coordinator.interact(bagSource, message)
		&& coordinator.hasSource(),
		"slot interaction can start and select a source in one action") && ok;
	const ControllerSlotAddress sameDomainTarget =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsBag,
		1
	};
	ok = check(coordinator.interact(sameDomainTarget, message),
		"slot interaction submits to a same-domain target") && ok;
	ok = check(!coordinator.active(),
		"same-domain completion exits the edit session") && ok;

	ok = check(stateChanges >= 6,
		"state changes notify registered views") && ok;

	ControllerTransferCoordinator noAlternateDomain;
	ControllerTransferCoordinator::Policy isolatedPolicy;
	isolatedPolicy.domainOrder =
	{
		ControllerSlotDomain::GoodsBag,
		ControllerSlotDomain::GoodsQuick
	};
	isolatedPolicy.identifySource = [&objects](
		const ControllerSlotAddress& address)
	{
		if (address.logicalIndex != 0)
		{
			return ControllerSlotIdentity();
		}
		return ControllerSlotIdentity
		{
			std::static_pointer_cast<const void>(objects[0]),
			"isolated"
		};
	};
	isolatedPolicy.submit = [](const ControllerSlotAddress&,
		const ControllerSlotAddress&, std::string&)
	{
		return ControllerTransferSubmitResult::Completed;
	};
	noAlternateDomain.setPolicy(
		ControllerSlotKind::Goods, std::move(isolatedPolicy));
	ControllerTransferCoordinator::DomainBinding isolatedBag;
	isolatedBag.kind = ControllerSlotKind::Goods;
	isolatedBag.domain = ControllerSlotDomain::GoodsBag;
	isolatedBag.activate = []() { return true; };
	noAlternateDomain.registerDomain(std::move(isolatedBag));
	int unavailableQuickActivations = 0;
	ControllerTransferCoordinator::DomainBinding unavailableQuick;
	unavailableQuick.kind = ControllerSlotKind::Goods;
	unavailableQuick.domain = ControllerSlotDomain::GoodsQuick;
	unavailableQuick.activate = [&unavailableQuickActivations]()
	{
		unavailableQuickActivations++;
		return false;
	};
	noAlternateDomain.registerDomain(std::move(unavailableQuick));
	ok = check(noAlternateDomain.start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsBag)
		&& noAlternateDomain.begin(bagSource),
		"isolated source starts normally") && ok;
	ok = check(!noAlternateDomain.cycleDomain(1),
		"cycle reports an unavailable candidate instead of silently consuming input") && ok;
	ok = check(unavailableQuickActivations == 1,
		"cycle probes a registered but currently unavailable domain once") && ok;
	ok = check(noAlternateDomain.hasSource(),
		"unavailable domain preserves the source for caller fallback") && ok;
	return ok ? 0 : 1;
}
