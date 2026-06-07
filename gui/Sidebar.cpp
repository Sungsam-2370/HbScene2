#include "MainDisplay.hpp"
#include "../libs/chesto/src/Constraint.hpp"
#include <algorithm>
#include "ThemeManager.hpp"

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

Sidebar::Sidebar()
	: logo(LOGO_PATH)
#if defined(DEBUG_BUILD) && !defined(WII_MOCK)
	, title("hb-appstore Dev Build", 22)
	, subtitle("v" APP_VERSION " (" __DATE__ ")", 18)
#else
	, title("Switch Scene Eshop", 22)
	, subtitle(APP_VERSION " para " PLATFORM, 18)
#endif
{
	// a sidebar consists of:
	//		a collection of category labels (TextElements),
	//		and an image (ImageElement) and a logo (TextElement)

	// it also can process input if the cursor goes over it, or a touch

	// there's no back color to the sidebar, as the background is already the right color

	// for every entry in cat names, create a text element
	// elements 0 through TOTAL_CATS are the sidebar texts (for highlighting)
	for (int x = 0; x < TOTAL_CATS; x++)
	{
		category[x].icon = new ImageElement((std::string(RAMFS "res/") + cat_value[x] + ".png").c_str());
		category[x].icon->resize(40/SCALER, 40/SCALER);
		category[x].icon->position(30/SCALER, 150/SCALER + x * 70/SCALER - 5/SCALER - scrollOffset);
		super::append(category[x].icon);

		category[x].name = new TextElement(i18n(cat_names[x]), 25);
		category[x].name->position(105/SCALER, 150/SCALER + x * 70/SCALER - scrollOffset);
		super::append(category[x].name);
	}

	// if we're a dev build, rotate the icon upside down
#if defined(DEBUG_BUILD) && !defined(WII_MOCK)
	logo.angle = 180;
#endif

	// create image in top left
	logo.resize(45/SCALER, 45/SCALER);
	logo.position(30/SCALER, 50/SCALER);
	super::append(&logo);

	// create title for logo, top left
	title.position(105/SCALER , 45/SCALER);
	super::append(&title);

	subtitle.position(105/SCALER, 75/SCALER);
	super::append(&subtitle);

#if defined(USE_OSC_BRANDING)
	// make the icon larger
	logo.setScaleMode(SCALE_PROPORTIONAL_NO_BG);
	logo.resize(85, 85);
	logo.position(10, 20);

	title.setText("HB App Store v" APP_VERSION);
	title.update();
	subtitle.setText("Pwrd by OSCWii.org");
	subtitle.setSize(22);
	subtitle.update();

	title.position(110, 35);
	subtitle.position(110, 65);
#endif

	// currentSelection in this class is used to keep track of which element is being pressed down on in touch mode
	// TODO: currentSelection belongs to element and should really be renamed (it's for general purpose animations)
	currentSelection = -1;

	if (isEarthDay()) {
		// easter egg for earth day https://www.earthday.org
		title.setText(i18n("listing.earthday"));
		title.update();

		// draw a an icon over the logo
		logo.hide();
		ImageElement* earth = new ImageElement(RAMFS "res/earth.png");
		earth->resize(60/SCALER, 60/SCALER);
		earth->position(23/SCALER, 40/SCALER);
		super::append(earth);
	}
}

Sidebar::~Sidebar()
{
	super::removeAll();
	for (int x = 0; x < TOTAL_CATS; x++)
	{
		delete category[x].icon;
		delete category[x].name;
	}
	if (hider)
		delete hider;
	if (hint)
		delete hint;
}

void Sidebar::addHints()
{
	if (hider == nullptr) {
		// small indicator to switch to advanced view using L
		hider = new ImageElement(Button::getControllerButtonImageForPlatform(L_BUTTON, false, false));
		hider->resize(20/SCALER, 20/SCALER);
		super::append(hider);
	}

	if (hint == nullptr) {
		hint = new TextElement(i18n("sidebar.hide"), 15);
		super::append(hint);
	}

	// Position both elements at the bottom of the sidebar.
	// Use fixed coordinates: icon on the left, text to its right.
	const int hintAreaHeight = 55/SCALER;
	const int bottomY = SCREEN_HEIGHT - hintAreaHeight + 17/SCALER;
	// Center the icon and text horizontally in the sidebar
	const int totalWidth = hider->width + 5/SCALER + hint->width;
	const int iconX = (width - totalWidth) / 2;
	hider->position(iconX, bottomY);
	hint->position(iconX + hider->width + 5/SCALER, bottomY + 2);

	// Make sure both are visible (they may have been hidden when sidebar collapsed)
	hider->unhide();
	hint->unhide();

	showCurrentCategory = true;
}

bool Sidebar::process(InputEvents* event)
{
	bool ret = false;
	int origHighlighted = highlighted;

	auto mainDisplay = (MainDisplay*)RootDisplay::mainDisplay;

	// process some joycon input events
	if (event->isKeyDown())
	{
		// if highlighted isn't set, don't do anything (applist will set it)
		if (this->highlighted < 0)
			return false;

		// if we got a RIGHT key, send it back to the applist
		if (event->held(RIGHT_BUTTON))
		{
			this->highlighted = -1;
			this->appList->highlighted = 0;
			this->appList->y = 0; // reset scroll TODO: maintain scroll when switching between sidebar and app list
			event->update();
			mainDisplay->playSFX();
			return true;
		}

		// adjust the cursor by 1 for up or down
		this->highlighted += -1 * (event->held(UP_BUTTON)) + (event->held(DOWN_BUTTON));

		// don't let the cursor go out of bounds
		if (this->highlighted < 0) this->highlighted = 0;
		if (this->highlighted >= TOTAL_CATS) this->highlighted = TOTAL_CATS - 1;
		updateCategoryScroll();
	}

#if !defined(_3DS) && !defined(_3DS_MOCK)
	// saw click down, set dragging state
	if (event->isTouchDown() || event->isTouchDrag())
	{
		this->dragging = true;
		this->highlighted = -1;
		this->elasticCounter = -1;
		this->currentSelection = -1;

		// go through the categories and see if this touch down was in one of them, to show it highlighted
		// TODO: uses similar code from below... really all this sidebar stuff shoould be refactored to use ListElement
		// and every category itself should be a CategoryLabel just like an AppCard consists of images + text
		for (int x = 0; x < TOTAL_CATS; x++)
		{
			int xc = 0,
				yc = 150/SCALER + x * 70/SCALER - 15 / SCALER - scrollOffset,
				// width = getWidth(),
				height = 60;

			if (event->touchIn(xc, yc, width, height))
			{
				// touch is over an element of the sidebar, set the currentSelection
				currentSelection = x;

				if (event->isTouchDrag()) {
					elasticCounter = THICK_HIGHLIGHT;
				}

				break;
			}
		}

		return true;
	}

#endif

	// detect if a click is on one of the sidebar elements
	// (or we saw the A button be pressed)
	if ((event->isTouchUp() && this->dragging) || (event->isKeyDown() && event->held(A_BUTTON)))
	{
		this->dragging = false;
		int previouslySelected = currentSelection;
		currentSelection = -1; // reset highlighted one

		// check if it's one of the text elements
		for (int x = 0; x < TOTAL_CATS; x++)
		{
			int xc = 0,
				yc = 150/SCALER + x * 70/SCALER - 15 / SCALER - scrollOffset,
				// width = getWidth(),
				height = 60; // TODO: extract formula into method (same as AppList x value)
			if ((event->touchIn(xc, yc, width, height) && event->isTouchUp()) || (event->held(A_BUTTON) && this->highlighted == x))
			{
				// if it's a touch up, let's make sure this is the same one we touched down on
				if (event->isTouchUp() && previouslySelected >= 0 && x != previouslySelected)
					return true;

				// saw touchup on a category, adjust active category
				this->curCategory = x;

				// since we updated the active category, we need to update the app listing
				if (this->appList != NULL)
				{
					this->appList->y = 0;
					this->appList->update();
				}
			}
		}

		return true;
	}

	if (origHighlighted != highlighted) {
		mainDisplay->playSFX();
		ret |= true;
	}

	return ret;
}

void Sidebar::render(Element* parent)
{
#if defined(_3DS) || defined(_3DS_MOCK)
  // no sidebar on 3ds
  return;
#endif
	// The Y where the category list begins (below the logo/title area)
	const int catAreaTop = 130/SCALER;

	// --- Clip drawing to the category area so items don't overdraw the logo/title ---
	const int hintAreaHeight = 55/SCALER;
	CST_Rect clipRect = { 0, catAreaTop, width, SCREEN_HEIGHT - catAreaTop - hintAreaHeight };
	SDL_RenderSetClipRect(RootDisplay::renderer, &clipRect);

	// draw the background-colored rect behind the active category
	// Use ThemeManager::background (the main app background, not the sidebar color)
	auto& bg = HBAS::ThemeManager::background;
	CST_Color consoleColor = { bg.r, bg.g, bg.b, 0xff };

	CST_Rect dimens = { 0, 0, width, (int)(60/SCALER) }; // TODO: extract this to a method too
	dimens.y = 150/SCALER + this->curCategory * 70/SCALER - 15 / SCALER - scrollOffset;					   // TODO: extract formula into method

	CST_SetDrawColor(RootDisplay::renderer, consoleColor);

	if (this->showCurrentCategory)
		CST_FillRect(RootDisplay::renderer, &dimens);

	if (appList && appList->touchMode && (this->currentSelection >= 0 && this->elasticCounter != THICK_HIGHLIGHT))
	{
		CST_Rect dimens2 = { 0, 0, 400, 60 };
		dimens.y = 150/SCALER + this->currentSelection * 70/SCALER - 15 / SCALER - scrollOffset;					   // TODO: extract formula into method
		CST_SetDrawBlend(RootDisplay::renderer, true);
		CST_Color highlight = { 0x10, 0xD9, 0xD9, 0x40 };
		CST_SetDrawColor(RootDisplay::renderer, highlight); // TODO: matches the DEEP_HIGHLIGHT color
		CST_FillRect(RootDisplay::renderer, &dimens2);
	}

	// draw the selected category, if one should be highlighted
	if (this->highlighted >= 0 || (this->currentSelection >= 0 && this->elasticCounter == THICK_HIGHLIGHT))
	{
		// for drag events, we want to use the thick highlight
		int highlightValue = (this->currentSelection >= 0 && this->elasticCounter == THICK_HIGHLIGHT) ? this->currentSelection : this->highlighted;

		int y = 150/SCALER  + highlightValue * 70/SCALER - 15 / SCALER - scrollOffset;
		//        rectangleRGBA(RootDisplay::renderer, 0, y, dimens.w, y + dimens.h, 0xff, 0x00, 0xff, 0xff);

		for (int x=-2; x<3; x++) {
			CST_rectangleRGBA(MainDisplay::renderer,
				dimens.x + x, y + x,
				dimens.x + dimens.w - x, y + dimens.h - x,
				0x10, 0xD9, 0xD9, 0xff
			);
		}
	}

	// Render category icons and names with clipping active (hides them behind the logo area)
	for (int x = 0; x < TOTAL_CATS; x++)
	{
		category[x].icon->render(this);
		category[x].name->render(this);
	}

	// Remove clip rect before rendering logo/title/subtitle so they always show
	SDL_RenderSetClipRect(RootDisplay::renderer, NULL);

	// Render logo, title and subtitle without clipping
	logo.render(this);
	title.render(this);
	subtitle.render(this);

	// Render any remaining hint elements (also unclipped)
	if (hider) hider->render(this);
	if (hint)  hint->render(this);
}

std::string Sidebar::currentCatName()
{
	if (this->curCategory >= 0 && this->curCategory < TOTAL_CATS)
		return i18n(this->cat_names[this->curCategory]);

	return std::string("?");
}

std::string Sidebar::currentCatValue()
{
	if (this->curCategory >= 0 && this->curCategory < TOTAL_CATS)
		return std::string(this->cat_value[this->curCategory]);

	return std::string("?");
}

#if defined(USE_OSC_BRANDING)
rgb getOSCCategoryColor(std::string category)
{
	if (category == "emulators") {
		return fromRGB(0x34, 0xED, 0x90);
	} else if (category == "games") {
		return fromRGB(0xED, 0x34, 0x9F);
	} else if (category == "utilities") {
		return fromRGB(0x34, 0xBE, 0xED);
	} else if (category == "media") {
		return fromRGB(0xff, 0xd3, 0x24);
	} else {
		return fromRGB(0xFF, 0xFF, 0xFF);
	}
}
#endif

void Sidebar::updateCategoryScroll()
{
	// screen height for the platform (720 on Switch, adjustable via SCREEN_HEIGHT)
	// Leave room for logo area at top (150px) and some bottom padding (30px)
	const int visibleStart = 150/SCALER;
	const int hintAreaHeight = 55/SCALER;
	const int visibleEnd   = SCREEN_HEIGHT - hintAreaHeight;
	const int itemHeight   = 70/SCALER;

	// Y position of the highlighted item (before scroll offset)
	int itemTop    = visibleStart + this->highlighted * itemHeight - scrollOffset;
	int itemBottom = itemTop + itemHeight;

	if (itemBottom > visibleEnd)
	{
		// item is below the visible area: scroll down
		scrollOffset += itemBottom - visibleEnd;
	}
	else if (itemTop < visibleStart)
	{
		// item is above the visible area: scroll up
		scrollOffset -= visibleStart - itemTop;
	}

	// clamp scroll so we never go negative or scroll too far
	if (scrollOffset < 0) scrollOffset = 0;
	int maxScroll = visibleStart + TOTAL_CATS * itemHeight - visibleEnd;
	if (maxScroll < 0) maxScroll = 0;
	if (scrollOffset > maxScroll) scrollOffset = maxScroll;

	// reposition all category elements to reflect new scroll
	for (int x = 0; x < TOTAL_CATS; x++)
	{
		category[x].icon->position(30/SCALER,  visibleStart + x * itemHeight - 5/SCALER - scrollOffset);
		category[x].name->position(105/SCALER, visibleStart + x * itemHeight - scrollOffset);
	}
}
