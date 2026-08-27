#pragma once

namespace LogicalResolutionPolicy
{
constexpr int MinimumWidth = 640;
constexpr int MinimumHeight = 480;

constexpr int constrainWidth(int width)
{
	return width < MinimumWidth ? MinimumWidth : width;
}

constexpr int constrainHeight(int height)
{
	return height < MinimumHeight ? MinimumHeight : height;
}

inline void constrain(int& width, int& height)
{
	width = constrainWidth(width);
	height = constrainHeight(height);
}
}
