#include "AlertDialog.hpp"
#include "Container.hpp"
#include "Constraint.hpp"
#include "Button.hpp"
#include "DrawUtils.hpp"

AlertDialog::AlertDialog(const std::string& title, const std::string& message)
    : title(title), message(message)
{
    /**
     *    AlertDialog
     *    - Overlay (full screen dimmer, 100% width/height)
     *      - VStack (border of window, centered with light bg, dialog dimensions)
     *         - VStack (inner content, sized to fit the inner content)
     *            - TextElement (message text, width to the inner content)
     *            - Button (OK button)  
     */
    hidden = true;

    messageText->setText(message);
    messageText->update(); // to set the size

    // just add the single button for now
    auto okButton = (new Button("OK", A_BUTTON));
    okButton->setAction([this]() {
        if (onConfirm) onConfirm();
    });
    okButton->cornerRadius = 10;
    okButton->updateBounds();

    auto innerVStack = new Container(COL_LAYOUT, 50);
    innerVStack->add(messageText);
    innerVStack->add(okButton);
    innerVStack->width = dialogWidth;
    // innerVStack->backgroundColor = fromRGB(0xff, 0, 0);
    // innerVStack->hasBackground = true;

    messageText->constrain(ALIGN_CENTER_HORIZONTAL, 0);
    okButton->constrain(ALIGN_CENTER_HORIZONTAL, 0);

    // fondo blanco opaco
    vStack->backgroundColor = fromRGB(0xff, 0xff, 0xff);
    vStack->hasBackground = true;
    vStack->cornerRadius = 15;
    vStack->width = dialogWidth;
    vStack->height = dialogHeight;
    vStack->add(innerVStack);
    
    innerVStack->constrain(ALIGN_CENTER_BOTH, 0);

    // overlay and shade bg color
    overlay->width = RootDisplay::mainDisplay->width;
    overlay->height = RootDisplay::mainDisplay->height;
    overlay->backgroundColor = fromRGB(0, 0, 0);
    overlay->backgroundOpacity = 0x00;
    overlay->cornerRadius = 1; // forces transparency to render properly (via sdl_gfx)
    overlay->hasBackground = true;
    
    overlay->child(vStack);

    vStack->constrain(ALIGN_CENTER_BOTH, 0);

    this->child(overlay);
}

void AlertDialog::setText(const std::string& newText) {
    messageText->setText(newText);
    messageText->update();
}

void AlertDialog::show() {
    // we have to go from being 100% transparent and small size to being opaque and full size
    // TODO: need opacity that affects all children elements
    hidden = false;

    if (useAnimation) {
        // start animation
        animate(250, [this](float progress) {
            // solo animar tamaño, overlay siempre transparente
            this->vStack->width = (int)(dialogWidth * progress);
            this->vStack->height = (int)(dialogHeight * progress);
            this->overlay->backgroundOpacity = 0x00;
        }, [this]() {
            this->vStack->width = dialogWidth;
            this->vStack->height = dialogHeight;
            this->overlay->backgroundOpacity = 0x00;
        });
        return;
    }

    // no animation, just do it!
    // this->setVisible(true);
    this->width = dialogWidth;
    this->width = dialogHeight;

}

void AlertDialog::render(Element* parent) {
    // No renderizar nada si esta oculto
    if (hidden) return;

    // Renderizar siempre al frente usando RootDisplay como target
    // para que aparezca encima de todos los elementos incluyendo subscreens
    overlay->render(RootDisplay::mainDisplay);

    // Borde negro alrededor del cuadro
    int bx = vStack->x - 2;
    int by = vStack->y - 2;
    int bw = vStack->width + 4;
    int bh = vStack->height + 4;
    CST_Color borderColor = { 0x00, 0x00, 0x00, 0xff };
    CST_SetDrawColor(RootDisplay::renderer, borderColor);
    CST_Rect borderRect = { bx, by, bw, bh };
    CST_DrawRect(RootDisplay::renderer, &borderRect);
}

bool AlertDialog::process(InputEvents* event) {
    // Implementation for processing input events
    return super::process(event);
}
