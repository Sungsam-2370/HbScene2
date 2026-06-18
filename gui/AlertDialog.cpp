#include "AlertDialog.hpp"
#include "../libs/chesto/src/Container.hpp"
#include "../libs/chesto/src/Constraint.hpp"
#include "../libs/chesto/src/Button.hpp"
#include "../libs/chesto/src/DrawUtils.hpp"

AlertDialog::AlertDialog(const std::string& title, const std::string& message)
    : title(title), message(message)
{
    hidden = true;

    messageText->setText(message);
    messageText->update();

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

    // overlay completamente transparente (sin semitransparencia)
    overlay->width = RootDisplay::mainDisplay->width;
    overlay->height = RootDisplay::mainDisplay->height;
    overlay->backgroundColor = fromRGB(0, 0, 0);
    overlay->backgroundOpacity = 0x00;
    overlay->cornerRadius = 1;
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
    hidden = false;

    if (useAnimation) {
        animate(250, [this](float progress) {
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

    this->width = dialogWidth;
    this->height = dialogHeight;
}

void AlertDialog::render(Element* parent) {
    // Renderizar siempre al frente encima de todos los demas elementos
    Element* target = (parent && parent != this) ? parent : this;
    overlay->render(target);

    // Borde negro alrededor del cuadro
    if (!hidden) {
        int bx = vStack->x - 2;
        int by = vStack->y - 2;
        int bw = vStack->width + 4;
        int bh = vStack->height + 4;
        CST_Color borderColor = { 0x00, 0x00, 0x00, 0xff };
        CST_SetDrawColor(RootDisplay::renderer, borderColor);
        CST_Rect borderRect = { bx, by, bw, bh };
        CST_DrawRect(RootDisplay::renderer, &borderRect);
    }
}

bool AlertDialog::process(InputEvents* event) {
    return super::process(event);
}
