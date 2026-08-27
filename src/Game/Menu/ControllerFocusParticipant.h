#pragma once

#include "../../Element/Element.h"

#include <vector>

enum class ControllerFocusTarget
{
	Default,
	GoodsBag,
	GoodsQuick,
	PlayerEquipment,
	MagicList,
	MagicQuick,
	Practice,
	PartnerList,
	PartnerBag,
	PartnerEquipment
};

class ControllerFocusParticipant
{
public:
	virtual ~ControllerFocusParticipant() = default;

	virtual bool activateControllerFocus(
		ControllerFocusTarget target) = 0;
	virtual bool isControllerFocusActive() const = 0;
	virtual void deactivateControllerFocus() = 0;
	virtual PElement controllerFocusedElement() const
	{
		return nullptr;
	}
	virtual std::vector<PElement> controllerFocusCandidates() const
	{
		return {};
	}
	virtual bool focusControllerElement(const PElement&)
	{
		return false;
	}
	// Shared visual owners can expose more than one semantic pane (for
	// example, the integrated equipment and magic lists). This query lets the
	// central dispatcher retain the correct semantic role after spatial or
	// pointer focus moves without mutating focus to probe a pane.
	virtual bool controllerFocusElementMatchesTarget(
		const PElement&,
		ControllerFocusTarget) const
	{
		return false;
	}
};

class ControllerTransferParticipant :
	public ControllerFocusParticipant
{
public:
	virtual ~ControllerTransferParticipant() = default;

	virtual void refreshControllerTransferHighlight() = 0;
};
