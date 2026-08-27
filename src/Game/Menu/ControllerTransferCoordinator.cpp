#include "ControllerTransferCoordinator.h"

#include <algorithm>
#include <utility>

bool ControllerSlotAddress::operator==(
	const ControllerSlotAddress& other) const
{
	return kind == other.kind
		&& domain == other.domain
		&& logicalIndex == other.logicalIndex
		&& context == other.context;
}

bool ControllerSlotIdentity::valid() const
{
	return object != nullptr && !key.empty();
}

bool ControllerSlotIdentity::operator==(
	const ControllerSlotIdentity& other) const
{
	return object == other.object && key == other.key;
}

ControllerTransferCoordinator::ControllerTransferCoordinator()
{
}

ControllerTransferCoordinator::~ControllerTransferCoordinator()
{
	clear();
}

void ControllerTransferCoordinator::clear()
{
	policies.clear();
	domains.clear();
	sessionKind.reset();
	originDomain.reset();
	currentDomain.reset();
	clearSourceState();
	stateChangedHandler = std::function<void()>();
}

void ControllerTransferCoordinator::setPolicy(
	ControllerSlotKind kind, Policy policy)
{
	for (auto& entry : policies)
	{
		if (entry.kind == kind)
		{
			entry.policy = std::move(policy);
			return;
		}
	}
	policies.push_back({ kind, std::move(policy) });
}

void ControllerTransferCoordinator::registerDomain(DomainBinding binding)
{
	for (auto& current : domains)
	{
		if (current.kind == binding.kind && current.domain == binding.domain)
		{
			current = std::move(binding);
			return;
		}
	}
	domains.push_back(std::move(binding));
}

void ControllerTransferCoordinator::setStateChangedHandler(
	std::function<void()> handler)
{
	stateChangedHandler = std::move(handler);
}

bool ControllerTransferCoordinator::start(
	ControllerSlotKind kind, ControllerSlotDomain domain)
{
	if (active() && sessionKind.value() == kind
		&& currentDomain.value() == domain)
	{
		return true;
	}
	Policy* policy = findPolicy(kind);
	DomainBinding* domainBinding = findDomain(kind, domain);
	if (policy == nullptr || domainBinding == nullptr
		|| !domainBinding->activate)
	{
		return false;
	}
	if (active())
	{
		cancel();
	}
	if (!domainBinding->activate())
	{
		return false;
	}
	sessionKind = kind;
	originDomain = domain;
	currentDomain = domain;
	clearSourceState();
	notifyStateChanged();
	return true;
}

bool ControllerTransferCoordinator::begin(
	const ControllerSlotAddress& source)
{
	if (source.logicalIndex < 0)
	{
		return false;
	}
	if (!active())
	{
		if (!start(source.kind, source.domain))
		{
			return false;
		}
	}
	if (!active(source.kind) || !currentDomain.has_value()
		|| currentDomain.value() != source.domain || hasSource())
	{
		return false;
	}
	Policy* policy = findPolicy(source.kind);
	if (policy == nullptr || !policy->identifySource)
	{
		return false;
	}
	ControllerSlotIdentity identity = policy->identifySource(source);
	if (!identity.valid())
	{
		return false;
	}
	sourceAddress = source;
	sourceIdentity = std::move(identity);
	notifyStateChanged();
	return true;
}

bool ControllerTransferCoordinator::interact(
	const ControllerSlotAddress& target,
	std::string& message)
{
	message.clear();
	if (target.logicalIndex < 0)
	{
		return false;
	}
	if (!active())
	{
		if (!start(target.kind, target.domain))
		{
			return false;
		}
	}
	if (!active(target.kind) || !currentDomain.has_value()
		|| currentDomain.value() != target.domain)
	{
		return false;
	}
	if (!hasSource())
	{
		begin(target);
		return true;
	}

	const std::optional<ControllerSlotAddress> previousSource = source();
	const ControllerTransferSubmitResult result = submit(target, message);
	if (result == ControllerTransferSubmitResult::Completed
		&& previousSource.has_value()
		&& previousSource->domain == target.domain)
	{
		cancel();
	}
	return true;
}

ControllerTransferSubmitResult ControllerTransferCoordinator::submit(
	const ControllerSlotAddress& target,
	std::string& message)
{
	message.clear();
	if (!hasSource() || !active(target.kind) || !currentDomain.has_value()
		|| currentDomain.value() != target.domain || target.logicalIndex < 0)
	{
		return ControllerTransferSubmitResult::Rejected;
	}
	Policy* policy = findPolicy(target.kind);
	if (policy == nullptr || !policy->identifySource || !policy->submit)
	{
		return ControllerTransferSubmitResult::Rejected;
	}
	const ControllerSlotIdentity currentIdentity =
		policy->identifySource(sourceAddress.value());
	if (!currentIdentity.valid() || !(currentIdentity == sourceIdentity))
	{
		message = "拿起内容已发生变化";
		cancel();
		return ControllerTransferSubmitResult::SourceLost;
	}
	const ControllerTransferSubmitResult result = policy->submit(
		sourceAddress.value(), target, message);
	if (result == ControllerTransferSubmitResult::Completed)
	{
		clearSourceState();
		notifyStateChanged();
	}
	else if (result == ControllerTransferSubmitResult::SourceLost)
	{
		cancel();
	}
	return result;
}

bool ControllerTransferCoordinator::reactivateCurrentDomain()
{
	if (!active())
	{
		return false;
	}
	DomainBinding* binding = findDomain(
		sessionKind.value(), currentDomain.value());
	return binding != nullptr && binding->activate && binding->activate();
}

bool ControllerTransferCoordinator::cycleDomain(int direction)
{
	if (!active() || direction == 0 || !currentDomain.has_value())
	{
		return false;
	}
	Policy* policy = findPolicy(sessionKind.value());
	if (policy == nullptr || policy->domainOrder.empty())
	{
		return false;
	}
	auto current = std::find(
		policy->domainOrder.begin(),
		policy->domainOrder.end(),
		currentDomain.value());
	const std::size_t currentIndex = current == policy->domainOrder.end()
		? 0
		: static_cast<std::size_t>(
			std::distance(policy->domainOrder.begin(), current));
	if (policy->domainOrder.size() < 2)
	{
		return false;
	}
	DomainBinding* currentBinding = findDomain(
		sessionKind.value(), currentDomain.value());
	for (std::size_t offset = 1;
		offset <= policy->domainOrder.size(); offset++)
	{
		const std::size_t candidateIndex = direction < 0
			? (currentIndex + policy->domainOrder.size()
				- offset % policy->domainOrder.size())
				% policy->domainOrder.size()
			: (currentIndex + offset) % policy->domainOrder.size();
		const ControllerSlotDomain candidateDomain =
			policy->domainOrder[candidateIndex];
		DomainBinding* candidateBinding = findDomain(
			sessionKind.value(), candidateDomain);
		if (candidateBinding == nullptr || candidateBinding == currentBinding
			|| !candidateBinding->activate)
		{
			continue;
		}
		if (currentBinding != nullptr && currentBinding->deactivate)
		{
			currentBinding->deactivate();
		}
		if (candidateBinding->activate())
		{
			currentDomain = candidateDomain;
			notifyStateChanged();
			return true;
		}
		if (currentBinding != nullptr && currentBinding->activate)
		{
			currentBinding->activate();
		}
	}
	return false;
}

bool ControllerTransferCoordinator::cancelSource()
{
	if (!hasSource())
	{
		return false;
	}
	clearSourceState();
	notifyStateChanged();
	return true;
}

void ControllerTransferCoordinator::cancel()
{
	if (!active())
	{
		return;
	}
	const ControllerSlotKind previousKind = sessionKind.value();
	const ControllerSlotDomain previousDomain = currentDomain.value();
	const ControllerSlotDomain restoreDomain = originDomain.value();
	DomainBinding* previousBinding = findDomain(previousKind, previousDomain);
	DomainBinding* restoreBinding = findDomain(previousKind, restoreDomain);
	if (previousDomain != restoreDomain)
	{
		if (previousBinding != nullptr && previousBinding->deactivate)
		{
			previousBinding->deactivate();
		}
		if (restoreBinding != nullptr && restoreBinding->activate)
		{
			restoreBinding->activate();
		}
	}
	sessionKind.reset();
	originDomain.reset();
	currentDomain.reset();
	clearSourceState();
	notifyStateChanged();
}

bool ControllerTransferCoordinator::active() const
{
	return sessionKind.has_value() && originDomain.has_value()
		&& currentDomain.has_value();
}

bool ControllerTransferCoordinator::active(ControllerSlotKind kind) const
{
	return active() && sessionKind.value() == kind;
}

bool ControllerTransferCoordinator::hasSource() const
{
	return sourceAddress.has_value() && sourceIdentity.valid();
}

bool ControllerTransferCoordinator::isSource(
	const ControllerSlotAddress& address) const
{
	return hasSource() && sourceAddress.value() == address;
}

std::optional<ControllerSlotAddress>
ControllerTransferCoordinator::source() const
{
	return sourceAddress;
}

std::optional<ControllerSlotDomain>
ControllerTransferCoordinator::activeDomain() const
{
	return currentDomain;
}

PElement ControllerTransferCoordinator::activeOwner() const
{
	if (!active())
	{
		return nullptr;
	}
	const DomainBinding* binding = findDomain(
		sessionKind.value(), currentDomain.value());
	return binding != nullptr && binding->owner ? binding->owner() : nullptr;
}

ControllerTransferCoordinator::Policy*
ControllerTransferCoordinator::findPolicy(ControllerSlotKind kind)
{
	for (auto& entry : policies)
	{
		if (entry.kind == kind)
		{
			return &entry.policy;
		}
	}
	return nullptr;
}

const ControllerTransferCoordinator::Policy*
ControllerTransferCoordinator::findPolicy(ControllerSlotKind kind) const
{
	for (const auto& entry : policies)
	{
		if (entry.kind == kind)
		{
			return &entry.policy;
		}
	}
	return nullptr;
}

ControllerTransferCoordinator::DomainBinding*
ControllerTransferCoordinator::findDomain(
	ControllerSlotKind kind, ControllerSlotDomain domain)
{
	for (auto& binding : domains)
	{
		if (binding.kind == kind && binding.domain == domain)
		{
			return &binding;
		}
	}
	return nullptr;
}

const ControllerTransferCoordinator::DomainBinding*
ControllerTransferCoordinator::findDomain(
	ControllerSlotKind kind, ControllerSlotDomain domain) const
{
	for (const auto& binding : domains)
	{
		if (binding.kind == kind && binding.domain == domain)
		{
			return &binding;
		}
	}
	return nullptr;
}

void ControllerTransferCoordinator::clearSourceState()
{
	sourceAddress.reset();
	sourceIdentity = ControllerSlotIdentity();
}

void ControllerTransferCoordinator::notifyStateChanged()
{
	for (const auto& binding : domains)
	{
		if (binding.refreshSelection)
		{
			binding.refreshSelection();
		}
	}
	if (stateChangedHandler)
	{
		stateChangedHandler();
	}
}
