#pragma once

#include "Element.hpp"

#include <climits>

// ListElement keeps track of a currently highlighted piece, as well as handling scrolling as a unit
class ListElement : public Element
{
public:
	int highlighted = -1;
	int initialTouchDown = -1;
	int minYScroll = 0;      // limite superior de y (0 = inicio de la lista)
	int lowerYScroll = INT_MIN; // limite inferior de y (<= 0): hasta donde se puede scrollear hacia abajo (sin limite por defecto)
	void clampScroll()
	{
		if (y < lowerYScroll)
			y = lowerYScroll;
		if (y > minYScroll)
			y = minYScroll;
	}
	bool process(InputEvents* event);
	bool handleInertiaScroll(InputEvents* event);
	bool processUpDown(InputEvents* event);
};
