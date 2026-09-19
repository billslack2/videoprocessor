#pragma once

// Finish child layout before the renderer observes the new size. Window placement
// belongs to Windows and explicit UI actions; a resize must not restore old bounds.
namespace WindowResizeLayout
{
	template<class LayoutChildren, class NotifyRenderer>
	void HandleSize(int width, int height,
		LayoutChildren&& layoutChildren, NotifyRenderer&& notifyRenderer)
	{
		layoutChildren(width, height);
		notifyRenderer();
	}
}
