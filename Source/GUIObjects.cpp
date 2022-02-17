/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "GUIObjects.h"

extern "C"
{
#include <m_pd.h>
#include <g_canvas.h>
#include <m_imp.h>
}

#include "Box.h"
#include "Canvas.h"
#include "Connection.h"
#include "Edge.h"
#include "PluginEditor.h"

#include "LookAndFeel.h"

// False GATOM
typedef struct _fake_gatom
{
    t_text a_text;
    int a_flavor;          /* A_FLOAT, A_SYMBOL, or A_LIST */
    t_glist* a_glist;      /* owning glist */
    t_float a_toggle;      /* value to toggle to */
    t_float a_draghi;      /* high end of drag range */
    t_float a_draglo;      /* low end of drag range */
    t_symbol* a_label;     /* symbol to show as label next to box */
    t_symbol* a_symfrom;   /* "receive" name -- bind ourselves to this */
    t_symbol* a_symto;     /* "send" name -- send to this on output */
    t_binbuf* a_revertbuf; /* binbuf to revert to if typing canceled */
    int a_dragindex;       /* index of atom being dragged */
    int a_fontsize;
    unsigned int a_shift : 1;         /* was shift key down when drag started? */
    unsigned int a_wherelabel : 2;    /* 0-3 for left, right, above, below */
    unsigned int a_grabbed : 1;       /* 1 if we've grabbed keyboard */
    unsigned int a_doubleclicked : 1; /* 1 if dragging from a double click */
    t_symbol* a_expanded_to;
} t_fake_gatom;

GUIComponent::GUIComponent(const pd::Gui& pdGui, Box* parent, bool newObject) : box(parent), processor(*parent->cnv->pd), gui(pdGui), edited(false)

{
    // if(!box->pdObject) return;
    const CriticalSection* cs = box->cnv->pd->getCallbackLock();

    cs->enter();
    value = gui.getValue();
    min = gui.getMinimum();
    max = gui.getMaximum();
    cs->exit();

    if (gui.isIEM())
    {
        labelX = static_cast<t_iemgui*>(gui.getPointer())->x_ldx;
        labelY = static_cast<t_iemgui*>(gui.getPointer())->x_ldy;
        labelHeight = static_cast<t_iemgui*>(gui.getPointer())->x_fontsize * pd::Patch::zoom;
    }
    else if (gui.isAtom())
    {
        labelX = static_cast<int>(static_cast<t_fake_gatom*>(gui.getPointer())->a_wherelabel + 1);
    }

    updateLabel();

    sendSymbol = gui.getSendSymbol();
    receiveSymbol = gui.getReceiveSymbol();

    setWantsKeyboardFocus(true);

    addMouseListener(this, true);

    setLookAndFeel(dynamic_cast<PlugDataLook*>(&getLookAndFeel())->getPdLook());

    sendSymbol.addListener(this);
    receiveSymbol.addListener(this);
    primaryColour.addListener(this);
    secondaryColour.addListener(this);
    labelColour.addListener(this);
    labelX.addListener(this);
    labelY.addListener(this);
    labelHeight.addListener(this);
    labelText.addListener(this);
    min.addListener(this);
    max.addListener(this);
}

GUIComponent::~GUIComponent()
{
    sendSymbol.removeListener(this);
    receiveSymbol.removeListener(this);
    primaryColour.removeListener(this);
    secondaryColour.removeListener(this);
    labelColour.removeListener(this);
    labelX.removeListener(this);
    labelY.removeListener(this);
    labelHeight.removeListener(this);
    labelText.removeListener(this);
    min.removeListener(this);
    max.removeListener(this);

    box->removeComponentListener(this);
    auto* lnf = &getLookAndFeel();
    setLookAndFeel(nullptr);
    delete lnf;
}

void GUIComponent::lock(bool isLocked)
{
    setInterceptsMouseClicks(isLocked, isLocked);
}

void GUIComponent::mouseDown(const MouseEvent& e)
{
    if (box->commandLocked == true)
    {
        auto& sidebar = box->cnv->main.sidebar;
        inspectorWasVisible = !sidebar.isShowingConsole();
        sidebar.hideParameters();
    }
}

void GUIComponent::mouseUp(const MouseEvent& e)
{
    if (box->commandLocked == true && inspectorWasVisible)
    {
        box->cnv->main.sidebar.showParameters();
    }
}

void GUIComponent::initParameters(bool newObject)
{
    if (gui.getType() == pd::Type::Number)
    {
        auto color = Colour::fromString(secondaryColour.toString());
        secondaryColour = color.toString();
    }

    if (!gui.isIEM()) return;

    if (newObject)
    {
        primaryColour = findColour(Slider::thumbColourId).toString();
        secondaryColour = findColour(ComboBox::backgroundColourId).toString();
        labelColour = Colours::white.toString();

        gui.setForegroundColour(findColour(Slider::thumbColourId));
        gui.setBackgroundColour(findColour(ComboBox::backgroundColourId));
        gui.setLabelColour(Colours::white);

        labelHeight = gui.getFontHeight();
    }
    else
    {
        primaryColour = Colour(gui.getForegroundColor()).toString();
        secondaryColour = Colour(gui.getBackgroundColor()).toString();
        if (gui.isIEM()) labelColour = Colour(gui.getLabelColour()).toString();

        getLookAndFeel().setColour(TextButton::buttonOnColourId, Colour::fromString(primaryColour.toString()));
        getLookAndFeel().setColour(Slider::thumbColourId, Colour::fromString(primaryColour.toString()));

        getLookAndFeel().setColour(TextEditor::backgroundColourId, Colour::fromString(secondaryColour.toString()));
        getLookAndFeel().setColour(TextButton::buttonColourId, Colour::fromString(secondaryColour.toString()));

        auto sliderBackground = Colour::fromString(secondaryColour.toString());
        sliderBackground = sliderBackground.getBrightness() > 0.5f ? sliderBackground.darker() : sliderBackground.brighter();

        getLookAndFeel().setColour(Slider::backgroundColourId, sliderBackground);
    }

    repaint();
}

GUIComponent* GUIComponent::createGui(const String& name, Box* parent, bool newObject)
{
    auto* guiPtr = dynamic_cast<pd::Gui*>(parent->pdObject.get());

    if (!guiPtr) return nullptr;

    auto& gui = *guiPtr;

    if (gui.getType() == pd::Type::Bang)
    {
        return new BangComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Toggle)
    {
        return new ToggleComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::HorizontalSlider)
    {
        return new SliderComponent(false, gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::VerticalSlider)
    {
        return new SliderComponent(true, gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::HorizontalRadio)
    {
        return new RadioComponent(false, gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::VerticalRadio)
    {
        return new RadioComponent(true, gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Message)
    {
        return new MessageComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Number)
    {
        return new NumboxComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::AtomList)
    {
        return new ListComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Array)
    {
        return new ArrayComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::GraphOnParent)
    {
        return new GraphOnParent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Subpatch)
    {
        return new Subpatch(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::VuMeter)
    {
        return new VUMeter(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Panel)
    {
        return new PanelComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Comment)
    {
        return new CommentComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::AtomNumber)
    {
        return new NumboxComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::AtomSymbol)
    {
        return new MessageComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Mousepad)
    {
        return new MousePad(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Mouse)
    {
        return new MouseComponent(gui, parent, newObject);
    }
    if (gui.getType() == pd::Type::Keyboard)
    {
        return new KeyboardComponent(gui, parent, newObject);
    }

    return nullptr;
}

float GUIComponent::getValueOriginal() const noexcept
{
    return value;
}

void GUIComponent::setValueOriginal(float v)
{
    auto minimum = static_cast<float>(min.getValue());
    auto maximum = static_cast<float>(max.getValue());

    value = (minimum < maximum) ? std::max(std::min(v, maximum), minimum) : std::max(std::min(v, minimum), maximum);

    gui.setValue(value);
}

float GUIComponent::getValueScaled() const noexcept
{
    auto minimum = static_cast<float>(min.getValue());
    auto maximum = static_cast<float>(max.getValue());

    return (minimum < maximum) ? (value - minimum) / (maximum - minimum) : 1.f - (value - maximum) / (minimum - maximum);
}

void GUIComponent::setValueScaled(float v)
{
    auto minimum = static_cast<float>(min.getValue());
    auto maximum = static_cast<float>(max.getValue());

    value = (minimum < maximum) ? std::max(std::min(v, 1.f), 0.f) * (maximum - minimum) + minimum : (1.f - std::max(std::min(v, 1.f), 0.f)) * (minimum - maximum) + maximum;
    gui.setValue(value);
}

void GUIComponent::startEdition() noexcept
{
    edited = true;
    processor.enqueueMessages(stringGui, stringMouse, {1.f});

    value = gui.getValue();
}

void GUIComponent::stopEdition() noexcept
{
    edited = false;
    processor.enqueueMessages(stringGui, stringMouse, {0.f});
}

void GUIComponent::updateValue()
{
    if (edited == false)
    {
        box->cnv->pd->enqueueFunction(
            [this]()
            {
                float const v = gui.getValue();

                MessageManager::callAsync(
                    [this, v]() mutable
                    {
                        if (v != value)
                        {
                            value = v;
                            update();
                        }
                    });
            });
    }
}

void GUIComponent::componentMovedOrResized(Component& component, bool moved, bool resized)
{
    if (label)
    {
        Point<int> position = gui.getLabelPosition(box->getBounds().reduced(5));

        const int width = 100;
        const int height = 23;  // ??
        label->setBounds(position.x, position.y, width, height);
    }
}

void GUIComponent::updateLabel()
{
    const String text = gui.getLabelText();
    if (text.isNotEmpty())
    {
        label = std::make_unique<Label>();
        if (label == nullptr)
        {
            return;
        }

        Point<int> position = gui.getLabelPosition(box->getBounds().reduced(5));

        const int width = 100;
        const int height = static_cast<int>(labelHeight.getValue());
        label->setBounds(position.x, position.y, width, height);

        label->setFont(Font(static_cast<int>(labelHeight.getValue())));
        label->setJustificationType(Justification::left);
        label->setBorderSize(BorderSize<int>(0, 0, 0, 0));
        label->setMinimumHorizontalScale(1.f);
        label->setText(text, dontSendNotification);
        label->setEditable(false, false);
        label->setInterceptsMouseClicks(false, false);
        label->setColour(Label::textColourId, gui.getLabelColour());
        // label->setColour(Label::textColourId, Colour(static_cast<uint32>(lbl.getColor())));
        box->cnv->addAndMakeVisible(label.get());
        box->addComponentListener(this);
    }
}

pd::Gui GUIComponent::getGui()
{
    return gui;
}

// Called in destructor of subpatch and graph class
// Makes sure that any tabs refering to the now deleted patch will be closed
void GUIComponent::closeOpenedSubpatchers()
{
    auto& main = box->cnv->main;
    auto* tabbar = &main.tabbar;

    if (!tabbar) return;

    for (int n = 0; n < tabbar->getNumTabs(); n++)
    {
        auto* cnv = main.getCanvas(n);
        if (cnv && cnv->patch == *getPatch())
        {
            tabbar->removeTab(n);
            main.pd.patches.removeFirstMatchingValue(cnv->patch);
            main.canvases.removeObject(cnv);
        }
    }

    if (tabbar->getNumTabs() > 1)
    {
        tabbar->getTabbedButtonBar().setVisible(true);
        tabbar->setTabBarDepth(30);
    }
    else
    {
        tabbar->getTabbedButtonBar().setVisible(false);
        tabbar->setTabBarDepth(1);
    }
}

// BangComponent

BangComponent::BangComponent(const pd::Gui& pdGui, Box* parent, bool newObject) : GUIComponent(pdGui, parent, newObject)
{
    addAndMakeVisible(bangButton);

    bangButton.setTriggeredOnMouseDown(true);
    bangButton.setName("pd:bang");

    bangButton.onClick = [this]()
    {
        startEdition();
        setValueOriginal(1);
        stopEdition();
        update();
    };

    initParameters(newObject);
    box->restrainer.setSizeLimits(38, 38, 1200, 1200);
    box->restrainer.setFixedAspectRatio(1.0f);
    box->restrainer.checkComponentBounds(box);
}

void BangComponent::update()
{
    if (getValueOriginal() > std::numeric_limits<float>::epsilon())
    {
        bangButton.setToggleState(true, dontSendNotification);

        auto currentTime = Time::getCurrentTime().getMillisecondCounter();
        auto timeSinceLast = currentTime - lastBang;

        int holdTime = bangHold.getValue();

        if (timeSinceLast < static_cast<int>(bangHold.getValue()) * 2)
        {
            holdTime = timeSinceLast / 2;
        }
        if (holdTime < bangInterrupt)
        {
            holdTime = bangInterrupt.getValue();
        }

        lastBang = currentTime;

        auto button = SafePointer<TextButton>(&bangButton);
        Timer::callAfterDelay(holdTime,
                              [button]() mutable
                              {
                                  if (!button) return;
                                  button->setToggleState(false, dontSendNotification);
                                  if (button->isDown())
                                  {
                                      button->setState(Button::ButtonState::buttonNormal);
                                  }
                              });
    }
}

void BangComponent::resized()
{
    gui.setSize(box->getWidth(), box->getHeight());
    bangButton.setBounds(getLocalBounds().reduced(5));
}

// ToggleComponent
ToggleComponent::ToggleComponent(const pd::Gui& pdGui, Box* parent, bool newObject) : GUIComponent(pdGui, parent, newObject)
{
    addAndMakeVisible(toggleButton);
    toggleButton.setToggleState(getValueOriginal(), dontSendNotification);
    toggleButton.setConnectedEdges(12);
    toggleButton.setName("pd:toggle");

    toggleButton.onClick = [this]()
    {
        startEdition();
        auto newValue = 1.f - getValueOriginal();
        setValueOriginal(newValue);
        toggleButton.setToggleState(newValue, dontSendNotification);
        stopEdition();

        update();
    };

    initParameters(newObject);

    box->restrainer.setSizeLimits(38, 38, 1200, 1200);
    box->restrainer.setFixedAspectRatio(1.0f);
    box->restrainer.checkComponentBounds(box);
}

void ToggleComponent::resized()
{
    gui.setSize(box->getWidth(), box->getHeight());
    toggleButton.setBounds(getLocalBounds().reduced(6));
}

void ToggleComponent::update()
{
    toggleButton.setToggleState((getValueOriginal() > std::numeric_limits<float>::epsilon()), dontSendNotification);
}

// MessageComponent
MessageComponent::MessageComponent(const pd::Gui& pdGui, Box* parent, bool newObject) : GUIComponent(pdGui, parent, newObject)
{
    addAndMakeVisible(input);

    input.setInterceptsMouseClicks(false, false);

    // message box behaviour
    if (!gui.isAtom())
    {
        input.getLookAndFeel().setColour(TextEditor::backgroundColourId, Colours::transparentBlack);

        input.onTextChange = [this]() { gui.setSymbol(input.getText().toStdString()); };

        input.onEditorShow = [this]()
        {
            auto* editor = input.getCurrentTextEditor();

            editor->onTextChange = [this, editor]()
            {
                auto width = input.getFont().getStringWidth(editor->getText()) + 25;

                if (width > box->getWidth())
                {
                    box->setSize(width, box->getHeight());
                }
            };

            editor->onFocusLost = [this]()
            {
                auto width = input.getFont().getStringWidth(input.getText()) + 25;
                if (width < box->getWidth())
                {
                    box->setSize(width, box->getHeight());
                    box->restrainer.checkComponentBounds(box);
                }
            };
        };
    }
    // symbolatom box behaviour
    else
    {
        input.onEditorShow = [this]()
        {
            auto* editor = input.getCurrentTextEditor();
            editor->onReturnKey = [this, editor]()
            {
                startEdition();
                gui.setSymbol(editor->getText().toStdString());
                stopEdition();
                // input.setText(String(gui.getSymbol()), dontSendNotification);
            };

            editor->onFocusLost = [this]()
            {
                auto width = input.getFont().getStringWidth(input.getText()) + 25;
                if (width < box->getWidth())
                {
                    box->setSize(width, box->getHeight());
                    box->restrainer.checkComponentBounds(box);
                }
            };
        };
    }

    box->addMouseListener(this, false);
    box->restrainer.setSizeLimits(50, 30, 500, 600);
    box->restrainer.checkComponentBounds(box);
}

void MessageComponent::lock(bool locked)
{
    isLocked = locked;
    setInterceptsMouseClicks(isLocked, isLocked);
}

void MessageComponent::resized()
{
    input.setBounds(getLocalBounds());
}

void MessageComponent::update()
{
    input.setText(String(gui.getSymbol()), sendNotification);
}

void MessageComponent::paint(Graphics& g)
{
    // Draw message style
    if (!getGui().isAtom())
    {
        auto baseColour = isDown ? Colour(90, 90, 90) : Colour(70, 70, 70);

        auto rect = getLocalBounds().toFloat();
        g.setGradientFill(ColourGradient(baseColour, Point<float>(0.0f, 0.0f), baseColour.darker(1.1f), getPosition().toFloat() + Point<float>(0, getHeight()), false));

        g.fillRoundedRectangle(rect, 2.0f);
    }
    else
    {
        g.fillAll(findColour(ComboBox::backgroundColourId));
    }
}

void MessageComponent::paintOverChildren(Graphics& g)
{
    GUIComponent::paintOverChildren(g);
    g.setColour(findColour(ComboBox::outlineColourId));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 2.0f, 1.5f);
}

void MessageComponent::updateValue()
{
    if (!edited)
    {
        std::string const v = gui.getSymbol();

        if (lastMessage != v && !String(v).startsWith("click"))
        {
            numLines = 1;
            longestLine = 7;

            int currentLineLength = 0;
            for (auto& c : v)
            {
                if (c == '\n')
                {
                    numLines++;
                    longestLine = std::max(longestLine, currentLineLength);
                    currentLineLength = 0;
                }
                else
                {
                    currentLineLength++;
                }
            }
            if (numLines == 1) longestLine = std::max(longestLine, currentLineLength);

            lastMessage = v;

            update();
            // repaint();
        }
    }
}

// NumboxComponent

NumboxComponent::NumboxComponent(const pd::Gui& pdGui, Box* parent, bool newObject) : GUIComponent(pdGui, parent, newObject)
{
    input.addMouseListener(this, false);

    input.onEditorShow = [this]()
    {
        auto* editor = input.getCurrentTextEditor();
        startEdition();

        if (!gui.isAtom())
        {
            editor->setBorder({0, 10, 0, 0});
        }

        if (editor != nullptr)
        {
            editor->setInputRestrictions(0, ".-0123456789");
        }
    };

    input.onEditorHide = [this]()
    {
        setValueOriginal(input.getText().getFloatValue());
        stopEdition();
    };

    if (!gui.isAtom())
    {
        input.setBorderSize({1, 15, 1, 1});
    }
    addAndMakeVisible(input);

    input.setText(String(getValueOriginal()), dontSendNotification);

    initParameters(newObject);
    input.setEditable(false, true);

    box->restrainer.setSizeLimits(50, 30, 500, 30);
    box->restrainer.checkComponentBounds(box);
}

void NumboxComponent::resized()
{
    input.setBounds(getLocalBounds());
}

void NumboxComponent::update()
{
    float value = getValueOriginal();

    input.setText(String(value), dontSendNotification);
}

ListComponent::ListComponent(const pd::Gui& gui, Box* parent, bool newObject) : GUIComponent(gui, parent, newObject)
{
    static const int border = 1;

    label.setBounds(2, 0, getWidth() - 2, getHeight() - 1);
    label.setMinimumHorizontalScale(1.f);
    label.setJustificationType(Justification::centredLeft);
    label.setBorderSize(BorderSize<int>(border + 2, border, border, border));
    label.setText(String(getValueOriginal()), dontSendNotification);
    label.setEditable(false, false);
    label.setInterceptsMouseClicks(false, false);
    label.setColour(Label::textColourId, gui.getForegroundColor());
    setInterceptsMouseClicks(true, false);
    addAndMakeVisible(label);

    label.onEditorHide = [this]()
    {
        auto const newValue = label.getText().getFloatValue();
        if (std::abs(newValue - getValueOriginal()) > std::numeric_limits<float>::epsilon())
        {
            startEdition();
            setValueOriginal(newValue);
            stopEdition();
            label.setText(String(getValueOriginal()), dontSendNotification);
        }
    };

    label.onEditorShow = [this]()
    {
        auto* editor = label.getCurrentTextEditor();
        if (editor != nullptr)
        {
            editor->setIndents(1, 2);
            editor->setBorder(BorderSize<int>(0));
        }
    };

    updateValue();

    box->restrainer.setSizeLimits(100, 30, 500, 600);
    box->restrainer.checkComponentBounds(box);
}

void ListComponent::paint(Graphics& g)
{
    static auto const border = 1.0f;
    const float h = static_cast<float>(getHeight());
    const float w = static_cast<float>(getWidth());
    const float o = h * 0.25f;
    Path p;
    p.startNewSubPath(0.5f, 0.5f);
    p.lineTo(0.5f, h - 0.5f);
    p.lineTo(w - o, h - 0.5f);
    p.lineTo(w - 0.5f, h - o);
    p.lineTo(w - 0.5f, o);
    p.lineTo(w - o, 0.5f);
    p.closeSubPath();
    g.setColour(gui.getBackgroundColor());
    g.fillPath(p);
    g.setColour(Colours::black);
    g.strokePath(p, PathStrokeType(border));
}

void ListComponent::update()
{
    if (edited == false && !label.isBeingEdited())
    {
        auto const array = gui.getList();
        String message;
        for (auto const& atom : array)
        {
            if (message.isNotEmpty())
            {
                message += " ";
            }
            if (atom.isFloat())
            {
                message += String(atom.getFloat());
            }
            else if (atom.isSymbol())
            {
                message += String(atom.getSymbol());
            }
        }
        label.setText(message, NotificationType::dontSendNotification);
    }
}

// SliderComponent
SliderComponent::SliderComponent(bool vertical, const pd::Gui& pdGui, Box* parent, bool newObject) : GUIComponent(pdGui, parent, newObject)
{
    isVertical = vertical;
    addAndMakeVisible(slider);

    isLogarithmic = gui.isLogScale();

    if (vertical) slider.setSliderStyle(Slider::LinearVertical);

    slider.setRange(0., 1., 0.001);
    slider.setTextBoxStyle(Slider::NoTextBox, 0, 0, 0);
    slider.setScrollWheelEnabled(false);

    slider.setVelocityModeParameters(1.0f, 1, 0.0f, false, ModifierKeys::shiftModifier);

    slider.setValue(getValueScaled());

    slider.onDragStart = [this]() { startEdition(); };

    slider.onValueChange = [this]()
    {
        const float val = slider.getValue();
        if (gui.isLogScale())
        {
            float minValue = static_cast<float>(min.getValue());
            float maxValue = static_cast<float>(max.getValue());
            float minimum = minValue == 0.0f ? std::numeric_limits<float>::epsilon() : minValue;
            setValueOriginal(exp(val * log(maxValue / minimum)) * minimum);
        }
        else
        {
            setValueScaled(val);
        }
    };

    slider.onDragEnd = [this]() { stopEdition(); };

    initParameters(newObject);

    if (isVertical)
    {
        box->restrainer.setSizeLimits(40, 77, 250, 500);
        box->restrainer.checkComponentBounds(box);
    }
    else
    {
        box->restrainer.setSizeLimits(100, 35, 500, 250);
        box->restrainer.checkComponentBounds(box);
    }

    isLogarithmic.addListener(this);
}

SliderComponent::~SliderComponent()
{
    isLogarithmic.removeListener(this);
}

void SliderComponent::resized()
{
    gui.setSize(box->getWidth(), box->getHeight());
    slider.setBounds(getLocalBounds().reduced(isVertical ? 0.0 : 3.0, isVertical ? 3.0 : 0.0));
}

void SliderComponent::update()
{
    slider.setValue(getValueScaled(), dontSendNotification);
}

// RadioComponent
RadioComponent::RadioComponent(bool vertical, const pd::Gui& pdGui, Box* parent, bool newObject) : GUIComponent(pdGui, parent, newObject)
{
    isVertical = vertical;

    initParameters(newObject);
    updateRange();

    int selected = getValueOriginal();

    if (selected < radioButtons.size())
    {
        radioButtons[selected]->setToggleState(true, dontSendNotification);
    }
    if (isVertical)
    {
        box->restrainer.setSizeLimits(25, 90, 250, 500);
        box->restrainer.checkComponentBounds(box);
    }
    else
    {
        box->restrainer.setSizeLimits(100, 25, 500, 250);
        box->restrainer.checkComponentBounds(box);
    }
}

void RadioComponent::resized()
{
    gui.setSize(box->getWidth(), box->getHeight());

    FlexBox fb;
    fb.flexWrap = FlexBox::Wrap::noWrap;
    fb.justifyContent = FlexBox::JustifyContent::flexStart;
    fb.alignContent = FlexBox::AlignContent::flexStart;
    fb.flexDirection = isVertical ? FlexBox::Direction::column : FlexBox::Direction::row;

    for (auto* b : radioButtons)
    {
        auto item = FlexItem(*b).withMinWidth(8.0f).withMinHeight(8.0f);
        item.flexGrow = 1.0f;
        item.flexShrink = 1.0f;
        fb.items.add(item);
    }

    fb.performLayout(getLocalBounds().toFloat());
}

void RadioComponent::update()
{
    int selected = getValueOriginal();

    if (selected < radioButtons.size())
    {
        radioButtons[selected]->setToggleState(true, dontSendNotification);
    }
}

void RadioComponent::updateRange()
{
    minimum = gui.getMinimum();
    maximum = gui.getMaximum();

    int numButtons = int(maximum.getValue()) - int(minimum.getValue());

    radioButtons.clear();

    for (int i = 0; i < numButtons; i++)
    {
        radioButtons.add(new TextButton);
        radioButtons[i]->setConnectedEdges(12);
        radioButtons[i]->setRadioGroupId(1001);
        radioButtons[i]->setClickingTogglesState(true);
        addAndMakeVisible(radioButtons[i]);

        radioButtons[i]->onClick = [this, i]() mutable
        {
            lastState = i;
            setValueOriginal(i);
        };
    }

    box->resized();
    resized();
}

// Array component
ArrayComponent::ArrayComponent(const pd::Gui& pdGui, Box* box, bool newObject) : GUIComponent(pdGui, box, newObject), graph(gui.getArray()), array(box->cnv->pd, graph)
{
    setInterceptsMouseClicks(false, true);
    array.setBounds(getLocalBounds());
    addAndMakeVisible(&array);

    box->restrainer.setSizeLimits(100, 40, 500, 600);
}

void ArrayComponent::resized()
{
    array.setBounds(getLocalBounds());
}

// Array graph
GraphicalArray::GraphicalArray(PlugDataAudioProcessor* instance, pd::Array& graph) : array(graph), edited(false), pd(instance)
{
    if (graph.getName().empty()) return;

    vec.reserve(8192);
    temp.reserve(8192);
    try
    {
        array.read(vec);
    }
    catch (...)
    {
        error = true;
    }
    startTimer(100);
    setInterceptsMouseClicks(true, false);
    setOpaque(false);
}

void GraphicalArray::paint(Graphics& g)
{
    g.fillAll(findColour(TextButton::buttonColourId));

    if (error)
    {
        // g.setFont(CamoLookAndFeel::getDefaultFont());
        g.drawText("array " + array.getName() + " is invalid", 0, 0, getWidth(), getHeight(), Justification::centred);
    }
    else
    {
        const float h = static_cast<float>(getHeight());
        const float w = static_cast<float>(getWidth());
        if (!vec.empty())
        {
            const std::array<float, 2> scale = array.getScale();
            if (array.isDrawingCurve())
            {
                const float dh = h / (scale[1] - scale[0]);
                const float dw = w / static_cast<float>(vec.size() - 1);
                Path p;
                p.startNewSubPath(0, h - (clip(vec[0], scale[0], scale[1]) - scale[0]) * dh);
                for (size_t i = 1; i < vec.size() - 1; i += 2)
                {
                    const float y1 = h - (clip(vec[i - 1], scale[0], scale[1]) - scale[0]) * dh;
                    const float y2 = h - (clip(vec[i], scale[0], scale[1]) - scale[0]) * dh;
                    const float y3 = h - (clip(vec[i + 1], scale[0], scale[1]) - scale[0]) * dh;
                    p.cubicTo(static_cast<float>(i - 1) * dw, y1, static_cast<float>(i) * dw, y2, static_cast<float>(i + 1) * dw, y3);
                }
                g.setColour(findColour(ComboBox::outlineColourId));
                g.strokePath(p, PathStrokeType(1));
            }
            else if (array.isDrawingLine())
            {
                const float dh = h / (scale[1] - scale[0]);
                const float dw = w / static_cast<float>(vec.size() - 1);
                Path p;
                p.startNewSubPath(0, h - (clip(vec[0], scale[0], scale[1]) - scale[0]) * dh);
                for (size_t i = 1; i < vec.size(); ++i)
                {
                    const float y = h - (clip(vec[i], scale[0], scale[1]) - scale[0]) * dh;
                    p.lineTo(static_cast<float>(i) * dw, y);
                }
                g.setColour(findColour(ComboBox::outlineColourId));
                g.strokePath(p, PathStrokeType(1));
            }
            else
            {
                const float dh = h / (scale[1] - scale[0]);
                const float dw = w / static_cast<float>(vec.size());
                g.setColour(findColour(ComboBox::outlineColourId));
                for (size_t i = 0; i < vec.size(); ++i)
                {
                    const float y = h - (clip(vec[i], scale[0], scale[1]) - scale[0]) * dh;
                    g.drawLine(static_cast<float>(i) * dw, y, static_cast<float>(i + 1) * dw, y);
                }
            }
        }
    }

    g.setColour(findColour(ComboBox::outlineColourId));
    g.drawRect(getLocalBounds(), 1);
}

void GraphicalArray::mouseDown(const MouseEvent& e)
{
    if (error) return;
    edited = true;
    mouseDrag(e);
}

void GraphicalArray::mouseDrag(const MouseEvent& event)
{
    if (error) return;
    const float s = static_cast<float>(vec.size() - 1);
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float x = static_cast<float>(event.x);
    const float y = static_cast<float>(event.y);

    const std::array<float, 2> scale = array.getScale();
    const size_t index = static_cast<size_t>(std::round(clip(x / w, 0.f, 1.f) * s));
    vec[index] = (1.f - clip(y / h, 0.f, 1.f)) * (scale[1] - scale[0]) + scale[0];

    const CriticalSection* cs = pd->getCallbackLock();

    if (cs->tryEnter())
    {
        try
        {
            array.write(index, vec[index]);
        }
        catch (...)
        {
            error = true;
        }
        cs->exit();
    }

    pd->enqueueMessages(stringArray, array.getName(), {});
    repaint();
}

void GraphicalArray::mouseUp(const MouseEvent& event)
{
    if (error) return;
    edited = false;
}

void GraphicalArray::timerCallback()
{
    if (!edited)
    {
        error = false;
        try
        {
            array.read(temp);
        }
        catch (...)
        {
            error = true;
        }
        if (temp != vec)
        {
            vec.swap(temp);
            repaint();
        }
    }
}

size_t GraphicalArray::getArraySize() const noexcept
{
    return vec.size();
}

// Graph On Parent
GraphOnParent::GraphOnParent(const pd::Gui& pdGui, Box* box, bool newObject) : GUIComponent(pdGui, box, newObject)
{
    setInterceptsMouseClicks(box->locked == false, true);

    subpatch = gui.getPatch();
    updateCanvas();

    box->resized();
    box->setLabelVisible(false);

    resized();
}

GraphOnParent::~GraphOnParent()
{
    box->setLabelVisible(true);
    closeOpenedSubpatchers();
}

void GraphOnParent::resized()
{
}

void GraphOnParent::lock(bool locked)
{
    isLocked = locked;
    setInterceptsMouseClicks(isLocked, true);
}

void GraphOnParent::mouseDown(const MouseEvent& e)
{
    GUIComponent::mouseDown(e);
    if (!isLocked)
    {
        box->mouseDown(e.getEventRelativeTo(box));
    }
}

void GraphOnParent::mouseDrag(const MouseEvent& e)
{
    if (!isLocked)
    {
        box->mouseDrag(e.getEventRelativeTo(box));
    }
}

void GraphOnParent::mouseUp(const MouseEvent& e)
{
    if (!isLocked)
    {
        box->mouseUp(e.getEventRelativeTo(box));
    }
}

void GraphOnParent::updateCanvas()
{
    // if(isShowing() && !canvas) {
    //  It could be an optimisation to only construct the canvas if its showing
    //  But it's also kinda weird
    if (!canvas)
    {
        canvas.reset(new Canvas(box->cnv->main, subpatch, true));
        addAndMakeVisible(canvas.get());

        auto [x, y, w, h] = getPatch()->getBounds();

        canvas->setBounds(-x, -y, w + x, h + y);

        box->resized();

        // Make sure that the graph doesn't become the current canvas
        box->cnv->patch.setCurrent(true);
        box->cnv->main.updateUndoState();
    }
    if (canvas)
    {
        auto [x, y, w, h] = getPatch()->getBounds();

        canvas->checkBounds();
        canvas->setBounds(-x, -y, w + x, h + y);

        auto parentBounds = box->getBounds();
        box->setSize(w, h);
    }
}

void GraphOnParent::updateValue()
{
    updateCanvas();

    if (!canvas) return;

    for (auto& box : canvas->boxes)
    {
        if (box->graphics)
        {
            box->graphics->updateValue();
        }
    }
}

PanelComponent::PanelComponent(const pd::Gui& gui, Box* box, bool newObject) : GUIComponent(gui, box, newObject)
{
    box->restrainer.setSizeLimits(40, 40, 2000, 2000);
    box->restrainer.checkComponentBounds(box);

    initParameters(newObject);
}

// Subpatch, phony GUI object
Subpatch::Subpatch(const pd::Gui& pdGui, Box* box, bool newObject) : GUIComponent(pdGui, box, newObject)
{
    subpatch = gui.getPatch();
}

void Subpatch::updateValue()
{
    // Pd sometimes sets the isgraph flag too late...
    // In that case we tell the box to create the gui
    if (static_cast<t_canvas*>(gui.getPointer())->gl_isgraph)
    {
        box->setType(box->getText(), true);
    }
};

Subpatch::~Subpatch()
{
    closeOpenedSubpatchers();
}

// Comment
CommentComponent::CommentComponent(const pd::Gui& pdGui, Box* box, bool newObject) : GUIComponent(pdGui, box, newObject)
{
    setInterceptsMouseClicks(false, false);
    setVisible(false);
}

void CommentComponent::paint(Graphics& g)
{
}

MousePad::MousePad(const pd::Gui& gui, Box* box, bool newObject) : GUIComponent(gui, box, newObject)
{
    Desktop::getInstance().addGlobalMouseListener(this);

    // setInterceptsMouseClicks(box->locked, box->locked);

    addMouseListener(box, false);
    box->setLabelVisible(false);
}

MousePad::~MousePad()
{
    removeMouseListener(box);
    box->setLabelVisible(true);
    Desktop::getInstance().removeGlobalMouseListener(this);
}

void MousePad::paint(Graphics& g){

};

void MousePad::updateValue(){

};

void MousePad::mouseDown(const MouseEvent& e)
{
    GUIComponent::mouseDown(e);
    if (!getScreenBounds().contains(e.getScreenPosition()) || !isLocked) return;

    auto* x = static_cast<t_pad*>(gui.getPointer());
    t_atom at[3];

    auto relativeEvent = e.getEventRelativeTo(this);

    // int xpos = text_xpix(&x->x_obj, glist), ypos = text_ypix(&x->x_obj, glist);
    x->x_x = (relativeEvent.getPosition().x / (float)getWidth()) * 127.0f;
    x->x_y = (relativeEvent.getPosition().y / (float)getHeight()) * 127.0f;

    SETFLOAT(at, 1.0f);
    sys_lock();
    outlet_anything(x->x_obj.ob_outlet, gensym("click"), 1, at);
    sys_unlock();

    isPressed = true;

    // glist_grab(x->x_glist, &x->x_obj.te_g, (t_glistmotionfn)pad_motion, 0, (float)xpix, (float)ypix);
}

void MousePad::mouseDrag(const MouseEvent& e)
{
    mouseMove(e);
}

void MousePad::mouseMove(const MouseEvent& e)
{
    if (!getScreenBounds().contains(e.getScreenPosition()) || !isLocked) return;

    auto* x = static_cast<t_pad*>(gui.getPointer());
    t_atom at[3];

    auto relativeEvent = e.getEventRelativeTo(this);

    // int xpos = text_xpix(&x->x_obj, glist), ypos = text_ypix(&x->x_obj, glist);
    x->x_x = (relativeEvent.getPosition().x / (float)getWidth()) * 127.0f;
    x->x_y = (relativeEvent.getPosition().y / (float)getHeight()) * 127.0f;

    SETFLOAT(at, x->x_x);
    SETFLOAT(at + 1, x->x_y);

    sys_lock();
    outlet_anything(x->x_obj.ob_outlet, &s_list, 2, at);
    sys_unlock();
}

void MousePad::mouseUp(const MouseEvent& e)
{
    if (!getScreenBounds().contains(e.getScreenPosition()) && !isPressed) return;

    auto* x = static_cast<t_pad*>(gui.getPointer());
    t_atom at[1];
    SETFLOAT(at, 0);
    outlet_anything(x->x_obj.ob_outlet, gensym("click"), 1, at);
}

void MousePad::lock(bool locked)
{
    isLocked = locked;
}

MouseComponent::MouseComponent(const pd::Gui& gui, Box* box, bool newObject) : GUIComponent(gui, box, newObject)
{
    Desktop::getInstance().addGlobalMouseListener(this);
}

MouseComponent::~MouseComponent()
{
    Desktop::getInstance().removeGlobalMouseListener(this);
}

void MouseComponent::updateValue(){};

void MouseComponent::mouseDown(const MouseEvent& e)
{
}
void MouseComponent::mouseMove(const MouseEvent& e)
{
    // Do this with a mouselistener?
    auto pos = Desktop::getInstance().getMousePosition();

    if (Desktop::getInstance().getMouseSource(0)->isDragging())
    {
        t_atom args[1];
        SETFLOAT(args, 0);

        pd_typedmess((t_pd*)gui.getPointer(), gensym("_up"), 1, args);
    }
    else
    {
        t_atom args[1];
        SETFLOAT(args, 1);

        pd_typedmess((t_pd*)gui.getPointer(), gensym("_up"), 1, args);
    }

    t_atom args[2];
    SETFLOAT(args, pos.x);
    SETFLOAT(args + 1, pos.y);

    pd_typedmess((t_pd*)gui.getPointer(), gensym("_getscreen"), 2, args);
}

void MouseComponent::mouseUp(const MouseEvent& e)
{
}

void MouseComponent::mouseDrag(const MouseEvent& e)
{
}

KeyboardComponent::KeyboardComponent(const pd::Gui& gui, Box* box, bool newObject) : GUIComponent(gui, box, newObject), keyboard(state, MidiKeyboardComponent::horizontalKeyboard)
{
    keyboard.setAvailableRange(36, 83);
    keyboard.setScrollButtonsVisible(false);

    state.addListener(this);
    addAndMakeVisible(keyboard);

    box->restrainer.setSizeLimits(50, 70, 1200, 1200);
}

void KeyboardComponent::resized()
{
    keyboard.setBounds(getLocalBounds());
}

void KeyboardComponent::updateValue(){

};

void KeyboardComponent::handleNoteOn(MidiKeyboardState* source, int midiChannel, int note, float velocity)
{
    auto* x = (t_keyboard*)gui.getPointer();

    box->cnv->pd->enqueueFunction(
        [x, note, velocity]() mutable
        {
            int ac = 2;
            t_atom at[2];
            SETFLOAT(at, note);
            SETFLOAT(at + 1, velocity * 127);

            outlet_list(x->x_out, &s_list, ac, at);
            if (x->x_send != &s_ && x->x_send->s_thing) pd_list(x->x_send->s_thing, &s_list, ac, at);
        });
}

void KeyboardComponent::handleNoteOff(MidiKeyboardState* source, int midiChannel, int note, float velocity)
{
    auto* x = (t_keyboard*)gui.getPointer();

    box->cnv->pd->enqueueFunction(
        [x, note, velocity]() mutable
        {
            int ac = 2;
            t_atom at[2];
            SETFLOAT(at, note);
            SETFLOAT(at + 1, 0);

            outlet_list(x->x_out, &s_list, ac, at);
            if (x->x_send != &s_ && x->x_send->s_thing) pd_list(x->x_send->s_thing, &s_list, ac, at);
        });
};

#define CLOSED 1      /* polygon */
#define BEZ 2         /* bezier shape */
#define NOMOUSERUN 4  /* disable mouse interaction when in run mode  */
#define NOMOUSEEDIT 8 /* same in edit mode */
#define NOVERTICES 16 /* disable only vertex grabbing in run mode */
#define A_ARRAY 55    /* LATER decide whether to enshrine this in m_pd.h */

/* getting and setting values via fielddescs -- note confusing names;
 the above are setting up the fielddesc itself. */
static t_float fielddesc_getfloat(t_fielddesc* f, t_template* templ, t_word* wp, int loud)
{
    if (f->fd_type == A_FLOAT)
    {
        if (f->fd_var)
            return (template_getfloat(templ, f->fd_un.fd_varsym, wp, loud));
        else
            return (f->fd_un.fd_float);
    }
    else
    {
        return (0);
    }
}

static int rangecolor(int n) /* 0 to 9 in 5 steps */
{
    int n2 = (n == 9 ? 8 : n); /* 0 to 8 */
    int ret = (n2 << 5);       /* 0 to 256 in 9 steps */
    if (ret > 255) ret = 255;
    return (ret);
}

static void numbertocolor(int n, char* s)
{
    int red, blue, green;
    if (n < 0) n = 0;
    red = n / 100;
    blue = ((n / 10) % 10);
    green = n % 10;
    sprintf(s, "#%2.2x%2.2x%2.2x", rangecolor(red), rangecolor(blue), rangecolor(green));
}

void TemplateDraw::paintOnCanvas(Graphics& g, Canvas* canvas, t_scalar* scalar, t_gobj* obj, int baseX, int baseY)
{
    auto* glist = canvas->patch.getPointer();
    auto* x = (t_curve*)obj;
    auto* templ = template_findbyname(scalar->sc_template);

    bool vis = true;

    int i, n = x->x_npoints;
    t_fielddesc* f = x->x_vec;

    auto* data = scalar->sc_vec;

    /* see comment in plot_vis() */
    if (vis && !fielddesc_getfloat(&x->x_vis, templ, data, 0))
    {
        return;
    }

    // Reduce clip region
    auto pos = canvas->getLocalPoint(canvas->main.getCurrentCanvas(), canvas->getPosition()) * -1;
    auto bounds = canvas->getParentComponent()->getLocalBounds().withPosition(pos);

    Path toDraw;

    if (vis)
    {
        if (n > 1)
        {
            int flags = x->x_flags, closed = (flags & CLOSED);
            t_float width = fielddesc_getfloat(&x->x_width, templ, data, 1);

            char outline[20], fill[20];
            int pix[200];
            if (n > 100) n = 100;
            /* calculate the pixel values before we start printing
             out the TK message so that "error" printout won't be
             interspersed with it.  Only show up to 100 points so we don't
             have to allocate memory here. */
            for (i = 0, f = x->x_vec; i < n; i++, f += 2)
            {
                // glist->gl_havewindow = canvas->isGraphChild;
                // glist->gl_isgraph = canvas->isGraph;
                canvas->pd->getCallbackLock()->enter();
                float xCoord = (baseX + fielddesc_getcoord(f, templ, data, 1)) / glist->gl_pixwidth;
                float yCoord = (baseY + fielddesc_getcoord(f + 1, templ, data, 1)) / glist->gl_pixheight;
                canvas->pd->getCallbackLock()->exit();

                pix[2 * i] = xCoord * bounds.getWidth() + pos.x;
                pix[2 * i + 1] = yCoord * bounds.getHeight() + pos.y;
            }
            if (width < 1) width = 1;
            if (glist->gl_isgraph) width *= glist_getzoom(glist);

            numbertocolor(fielddesc_getfloat(&x->x_outlinecolor, templ, data, 1), outline);
            if (flags & CLOSED)
            {
                numbertocolor(fielddesc_getfloat(&x->x_fillcolor, templ, data, 1), fill);

                // sys_vgui(".x%lx.c create polygon\\\n",
                //     glist_getcanvas(glist));
            }
            // else sys_vgui(".x%lx.c create line\\\n", glist_getcanvas(glist));

            // sys_vgui("%d %d\\\n", pix[2*i], pix[2*i+1]);

            if (flags & CLOSED)
            {
                toDraw.startNewSubPath(pix[0], pix[1]);
                for (i = 1; i < n; i++)
                {
                    toDraw.lineTo(pix[2 * i], pix[2 * i + 1]);
                }
                toDraw.lineTo(pix[0], pix[1]);
            }
            else
            {
                toDraw.startNewSubPath(pix[0], pix[1]);
                for (i = 1; i < n; i++)
                {
                    toDraw.lineTo(pix[2 * i], pix[2 * i + 1]);
                }
            }

            Colour juceColourOutline = Colour::fromString("FF" + String::fromUTF8(outline + 1));
            Colour juceColourFill = Colour::fromString("FF" + String::fromUTF8(fill + 1));

            g.setColour(juceColourFill);

            // sys_vgui("-width %f\\\n", width);

            String objName = String::fromUTF8(x->x_obj.te_g.g_pd->c_name->s_name);
            if (objName.contains("fill"))
            {
                g.fillPath(toDraw);
            }
            else
            {
                g.strokePath(toDraw, PathStrokeType(width));
            }

            if (flags & BEZ)
            {
                // sys_vgui("-smooth 1\\\n")
            };

            // sys_vgui("-tags curve%lx\n", data);
        }
        else
            post("warning: curves need at least two points to be graphed");
    }
    else
    {
        if (n > 1) sys_vgui(".x%lx.c delete curve%lx\n", glist_getcanvas(glist), data);
    }
}
