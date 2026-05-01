// LibreSprite
// Copyright (C) 2021  LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "base/string.h"
#include "script/script_object.h"
#include "ui/base.h"
#include "ui/close_event.h"
#include "ui/widget.h"
#include "ui/window.h"
#include <iostream>

#include "base/bind.h"
#include "base/memory.h"
#include "ui/ui.h"

#include "app/app.h"
#include "app/color.h"
#include "app/context.h"
#include "app/modules/gui.h"
#include "app/script/app_scripting.h"
#include "app/task_manager.h"
#include "app/ui/color_button.h"
#include "app/ui/status_bar.h"
#include "app/ui/dialog.h"
#include "widget_script.h"
#include "doc/color.h"
#include "script/engine.h"
#include "ui/button.h"
#include "ui/combobox.h"
#include "ui/label.h"
#include "ui/separator.h"
#include "ui/slider.h"

#include <memory>
#include <list>
#include <string>
#include <vector>

namespace ui {
class Dialog;
}

namespace {

script::Value makeColorValue(const app::Color& color) {
  auto values = new script::Value::Map::data_t[1];
  auto& map = values[0];
  map["red"] = color.getRed();
  map["green"] = color.getGreen();
  map["blue"] = color.getBlue();
  map["alpha"] = color.getAlpha();
  map["r"] = color.getRed();
  map["g"] = color.getGreen();
  map["b"] = color.getBlue();
  map["a"] = color.getAlpha();
  return {values, true};
}

int mapInt(script::Value::Map::data_t* map,
           const std::string& longName,
           const std::string& shortName,
           int defaultValue) {
  if (!map)
    return defaultValue;

  auto it = map->find(longName);
  if (it != map->end())
    return it->second;

  it = map->find(shortName);
  if (it != map->end())
    return it->second;

  return defaultValue;
}

app::Color colorFromValue(const script::Value& value) {
  if (value.type == script::Value::Type::STRING)
    return app::Color::fromString(value);

  if (value.type == script::Value::Type::MAP) {
    auto map = static_cast<script::Value::Map::data_t*>(value);
    return app::Color::fromRgb(mapInt(map, "red", "r", 0),
                               mapInt(map, "green", "g", 0),
                               mapInt(map, "blue", "b", 0),
                               mapInt(map, "alpha", "a", 255));
  }

  int color = value;
  return app::Color::fromRgb(doc::rgba_getr(color),
                             doc::rgba_getg(color),
                             doc::rgba_getb(color),
                             doc::rgba_geta(color));
}

std::vector<std::string> stringsFromValue(const script::Value& value) {
  std::vector<std::string> strings;
  if (value.type == script::Value::Type::MAP) {
    auto map = static_cast<script::Value::Map::data_t*>(value);
    int length = mapInt(map, "length", "length", 0);
    for (int i = 0; i < length; ++i) {
      auto it = map->find(std::to_string(i));
      if (it != map->end())
        strings.push_back(it->second.str());
    }
  } else if (value.type == script::Value::Type::STRING) {
    strings.push_back(value.str());
  }
  return strings;
}

class ColorEntryWidgetScriptObject : public WidgetScriptObject {
public:
  ColorEntryWidgetScriptObject() {
    addProperty("color",
                [this]{return makeColorValue(button()->getColor());},
                [this](script::Value color){
                  button()->setColor(colorFromValue(color));
                  return color;
                });

    addProperty("value",
                [this]{return makeColorValue(button()->getColor());},
                [this](script::Value color){
                  button()->setColor(colorFromValue(color));
                  return color;
                });
  }

  app::ColorButton* button() {
    auto button = getWidget<app::ColorButton>();
    if (!button)
      throw script::ObjectDestroyedException{};
    return button;
  }

  DisplayType getDisplayType() override {return DisplayType::Inline;}

  Handle build() override {
    auto scriptFileName = app::AppScripting::getFileName();
    auto button = new app::ColorButton(app::Color::fromRgb(0, 0, 0, 255),
                                       app::app_get_current_pixel_format());
    auto handle = button->handle();
    button->Change.connect([=](const app::Color&){
      if (handle)
        app::AppScripting::raiseEvent(scriptFileName, {button->id() + "_change"});
    });
    return handle;
  }
};

class CheckboxWidgetScriptObject : public WidgetScriptObject {
  std::string m_text;

public:
  CheckboxWidgetScriptObject() {
    addProperty("text",
                [this]{return m_text;},
                [this](const std::string& text){
                  if (auto checkbox = getWidget<ui::CheckBox>())
                    checkbox->setText(text);
                  m_text = text;
                  return text;
                });

    addProperty("value",
                [this]{return checkbox()->isSelected();},
                [this](bool value){
                  checkbox()->setSelected(value);
                  return value;
                });

    addProperty("selected",
                [this]{return checkbox()->isSelected();},
                [this](bool value){
                  checkbox()->setSelected(value);
                  return value;
                });
  }

  ui::CheckBox* checkbox() {
    auto checkbox = getWidget<ui::CheckBox>();
    if (!checkbox)
      throw script::ObjectDestroyedException{};
    return checkbox;
  }

  DisplayType getDisplayType() override {return DisplayType::Inline;}

  Handle build() override {
    auto scriptFileName = app::AppScripting::getFileName();
    auto checkbox = new ui::CheckBox(m_text);
    auto handle = checkbox->handle();
    checkbox->Click.connect([=](ui::Event&){
      if (handle)
        app::AppScripting::raiseEvent(scriptFileName, {checkbox->id() + "_change"});
    });
    return handle;
  }
};

class SliderWidgetScriptObject : public WidgetScriptObject {
public:
  SliderWidgetScriptObject() {
    addProperty("min",
                [this]{return slider()->getMinValue();},
                [this](int min){
                  slider()->setRange(min, slider()->getMaxValue());
                  return min;
                });

    addProperty("max",
                [this]{return slider()->getMaxValue();},
                [this](int max){
                  slider()->setRange(slider()->getMinValue(), max);
                  return max;
                });

    addProperty("value",
                [this]{return slider()->getValue();},
                [this](int value){
                  slider()->setValue(value);
                  return value;
                });
  }

  ui::Slider* slider() {
    auto slider = getWidget<ui::Slider>();
    if (!slider)
      throw script::ObjectDestroyedException{};
    return slider;
  }

  DisplayType getDisplayType() override {return DisplayType::Inline;}

  Handle build() override {
    auto scriptFileName = app::AppScripting::getFileName();
    auto slider = new ui::Slider(0, 100, 0);
    auto handle = slider->handle();
    slider->Change.connect([=]{
      if (handle)
        app::AppScripting::raiseEvent(scriptFileName, {slider->id() + "_change", slider->getValue()});
    });
    return handle;
  }
};

class ComboBoxWidgetScriptObject : public WidgetScriptObject {
public:
  ComboBoxWidgetScriptObject() {
    addProperty("value",
                [this]{return combo()->getValue();},
                [this](const std::string& value){
                  combo()->setValue(value);
                  return value;
                });

    addProperty("selected",
                [this]{return combo()->getSelectedItemIndex();},
                [this](int value){
                  combo()->setSelectedItemIndex(value);
                  return value;
                });

    addMethod("setOptions", &ComboBoxWidgetScriptObject::setOptions);
  }

  ui::ComboBox* combo() {
    auto combo = getWidget<ui::ComboBox>();
    if (!combo)
      throw script::ObjectDestroyedException{};
    return combo;
  }

  void setOptions(script::Value options) {
    auto items = stringsFromValue(options);
    combo()->removeAllItems();
    for (auto& item : items)
      combo()->addItem(item);
    if (!items.empty())
      combo()->setSelectedItemIndex(0);
  }

  DisplayType getDisplayType() override {return DisplayType::Inline;}

  Handle build() override {
    auto scriptFileName = app::AppScripting::getFileName();
    auto combo = new ui::ComboBox();
    combo->setEditable(false);
    auto handle = combo->handle();
    combo->Change.connect([=]{
      if (handle)
        app::AppScripting::raiseEvent(scriptFileName, {combo->id() + "_change", combo->getValue()});
    });
    return handle;
  }
};

class SeparatorWidgetScriptObject : public WidgetScriptObject {
public:
  DisplayType getDisplayType() override {return DisplayType::Block;}

  Handle build() override {
    return new ui::Separator("", ui::HORIZONTAL);
  }
};

static script::ScriptObject::Regular<ColorEntryWidgetScriptObject> colorEntrySO("ColorentryWidgetScriptObject");
static script::ScriptObject::Regular<CheckboxWidgetScriptObject> checkboxSO("CheckboxWidgetScriptObject");
static script::ScriptObject::Regular<SliderWidgetScriptObject> sliderSO("SliderWidgetScriptObject");
static script::ScriptObject::Regular<ComboBoxWidgetScriptObject> comboBoxSO("ComboboxWidgetScriptObject");
static script::ScriptObject::Regular<SeparatorWidgetScriptObject> separatorSO("SeparatorWidgetScriptObject");

} // anonymous namespace

class DialogScriptObject : public WidgetScriptObject {
  std::unordered_map<std::string, script::ScriptObject*> m_widgets;

  Handle build() {
    auto dialog = new ui::Dialog();

    // Scripting engine has finished working, build and show the Window
    getEngine()->afterEval([handle = dialog->handle()](bool success){
      if (auto dialog = handle.get<ui::Widget, ui::Dialog>())
        dialog->build();
    });

    return dialog;
  }

public:
  DialogScriptObject() {
    addProperty("title",
                [this] {return dialog()->text();},
                [this](const std::string& title){
                  dialog()->setText(title);
                  return title;
                })
      .doc("read+write. Sets the title of the dialog window.");

    addProperty("width",[this] {return dialog()->size().w;})
      .doc("read only. Gets the width of the dialog window.");
    addProperty("height",[this] {return dialog()->size().h;})
      .doc("read only. Gets the height of the dialog window.");

    addProperty("canClose",
                []{return true;},
                [this](bool canClose){
                  if (!canClose) {
                    dialog()->removeDecorativeWidgets();
                  }
                  return canClose;
                })
      .doc("write only. Determines if the user can close the dialog window.");

    addMethod("add", &DialogScriptObject::add);

    addMethod("get", &DialogScriptObject::get);

    addFunction("close", [this]{
      dialog()->closeWindow(false, true);
      setWrapped({}, false);
      return true;
    });

    addFunction("addDropDown", [this](const std::string& id) {
        auto dropdown = add("dropdown", id);
        return dropdown;
    });

    addFunction("addLabel", [this](const std::string& text, const std::string& id) {
        auto label = add("label", id);
        if (label)
            label->set("text", text);
        return label;
    });

    addFunction("addImageView", [this](const std::string& id) {
        return add("imageview", id);
    });

    addFunction("addButton", [this](const std::string& text, const std::string& id) {
        auto button = add("button", id);
        if (button)
            button->set("text", text);
        return button;
    });

    addFunction("addPaletteListBox", [this](const std::string& id) {
        return add("palettelistbox", id);
    });

    addFunction("addIntEntry", [this](const std::string& text, const std::string& id, int min, int max) {
        auto label = add("label", id + "-label");
        if (label)
            label->set("text", text);
        auto intentry = add("intentry", id);
        if (intentry) {
            intentry->set("min", min);
            intentry->set("max", max);
        }
        return intentry;
    });

    addFunction("addEntry", [this](const std::string& text, const std::string& id) {
        if (!text.empty()) {
            auto label = add("label", id + "-label");
            if (label)
                label->set("text", text);
        }
        return add("entry", id);
    });

    addFunction("addColorEntry", [this](const std::string& id,
                                        const std::string& label,
                                        script::Value color) {
        if (!label.empty()) {
            auto labelWidget = add("label", id + "-label");
            if (labelWidget)
                labelWidget->set("text", label);
        }
        auto colorEntry = add("colorentry", id);
        if (colorEntry)
            colorEntry->set("color", color);
        return colorEntry;
    });

    addFunction("addCheckbox", [this](const std::string& id,
                                      const std::string& label,
                                      bool value) {
        auto checkbox = add("checkbox", id);
        if (checkbox) {
            checkbox->set("text", label);
            checkbox->set("value", value);
        }
        return checkbox;
    });

    addFunction("addSlider", [this](const std::string& id,
                                    const std::string& label,
                                    int min,
                                    int max,
                                    int value) {
        if (!label.empty()) {
            auto labelWidget = add("label", id + "-label");
            if (labelWidget)
                labelWidget->set("text", label);
        }
        auto slider = add("slider", id);
        if (slider) {
            slider->set("min", min);
            slider->set("max", max);
            slider->set("value", value);
        }
        return slider;
    });

    addFunction("addSeparator", [this]{
      auto separator = add("separator", "");
      dialog()->addBreak();
      return separator;
    });

    addFunction("addComboBox", [this](const std::string& id,
                                      const std::string& label,
                                      script::Value options) {
        if (!label.empty()) {
            auto labelWidget = add("label", id + "-label");
            if (labelWidget)
                labelWidget->set("text", label);
        }
        auto combo = add("combobox", id);
        if (combo)
            combo->call("setOptions", options);
        return combo;
    });

    addFunction("addBreak", [this]{
      dialog()->addBreak();
      return true;
    });
  }

  ~DialogScriptObject() {
    auto dialog = getWidget<ui::Dialog>();
    if (!dialog)
      return;
    if (!dialog->isVisible())
      dialog->closeWindow(false, false);
  }

  ui::Dialog* dialog() {
    auto dialog = handle<ui::Widget, ui::Dialog>();
    if (!dialog)
      throw script::ObjectDestroyedException{};
    return dialog;
  }

  ScriptObject* get(const std::string& id) {
    auto it = m_widgets.find(id);
    return it != m_widgets.end() ? it->second : nullptr;
  }

  ScriptObject* add(const std::string& type, const std::string& id) {
    auto dialog = this->dialog();
    if (!dialog)
      return nullptr;

    if (type.empty() || get(id))
      return nullptr;

    auto cleanType = base::string_to_lower(type); // "lAbEl" -> "label"
    auto unprefixedType = cleanType;
    cleanType[0] = toupper(cleanType[0]);         // "label" -> "Label"
    cleanType += "WidgetScriptObject";            // "Label" -> "LabelWidgetScriptObject"

    auto sobj = getEngine()->create(cleanType);
    if (!sobj) {
      return nullptr;
    }

    auto widget = sobj->handle<ui::Widget>();
    if (!widget)
      return nullptr;

    dialog->add(widget);

    auto cleanId = !id.empty() ? id : unprefixedType + std::to_string(m_nextWidgetId++);
    sobj->set("id", cleanId);
    m_widgets[cleanId] = sobj;

    return sobj;
  }

  uint32_t m_nextWidgetId = 0;
};

static script::ScriptObject::Regular<DialogScriptObject> dialogSO(typeid(ui::Dialog*).name());
