// LibreSprite
// Copyright (C) 2015-2016  David Capello
// Copyright (C) 2023 LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/commands/commands.h"
#include "app/commands/command.h"
#include "app/commands/params.h"
#include "app/color.h"
#include "app/color_target.h"
#include "app/color_utils.h"
#include "app/context.h"
#include "app/document.h"
#include "app/document_api.h"
#include "base/launcher.h"
#include "base/connection.h"
#include "app/modules/editors.h"
#include "app/pref/preferences.h"
#include "app/script/app_scripting.h"
#include "app/task_manager.h"
#include "app/transaction.h"
#include "app/tools/active_tool.h"
#include "app/tools/tool.h"
#include "app/tools/tool_box.h"
#include "app/ui/dialog.h"
#include "app/ui/document_view.h"
#include "app/ui/editor/editor.h"
#include "app/ui_context.h"
#include "doc/color.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/primitives.h"
#include "doc/site.h"
#include "gfx/point.h"
#include "gfx/region.h"
#include "script/engine.h"
#include "script/engine_delegate.h"
#include "script/script_object.h"
#include "ui/alert.h"
#include "ui/widget.h"

#include <algorithm>
#include <sstream>
#include <vector>

class DudScriptObject : public script::InternalScriptObject {
public:
  void makeGlobal(const std::string& name) override {
    globalName = name;
  }
  std::string globalName;
};
static script::InternalScriptObject::Regular<DudScriptObject> dud("DudScriptObject");

namespace dialog {
ui::Widget* getDialogById(const std::string&);
}

namespace app {

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

script::Value mapValue(script::Value::Map::data_t* map, const std::string& key) {
  if (!map)
    return {};
  auto it = map->find(key);
  return it != map->end() ? it->second : script::Value{};
}

bool pointFromValue(const script::Value& value, gfx::Point& point) {
  if (value.type != script::Value::Type::MAP)
    return false;

  auto map = static_cast<script::Value::Map::data_t*>(value);
  point.x = mapInt(map, "x", "0", 0);
  point.y = mapInt(map, "y", "1", 0);
  return true;
}

std::vector<gfx::Point> pointsFromValue(const script::Value& value) {
  std::vector<gfx::Point> points;
  if (value.type != script::Value::Type::MAP)
    return points;

  auto map = static_cast<script::Value::Map::data_t*>(value);
  int length = mapInt(map, "length", "length", 0);
  for (int i = 0; i < length; ++i) {
    auto it = map->find(std::to_string(i));
    if (it == map->end())
      continue;

    gfx::Point point;
    if (pointFromValue(it->second, point))
      points.push_back(point);
  }
  return points;
}

bool isSaveCommand(app::Command* command) {
  if (!command)
    return false;

  auto& id = command->id();
  return id == "SaveFile" || id == "SaveFileAs" || id == "SaveFileCopyAs";
}

} // anonymous namespace

class AppScriptObject : public script::ScriptObject {
public:
  inject<ScriptObject> m_pixelColor{"pixelColor"};
  inject<ScriptObject> m_command{"command"};
  script::Value m_onBeforeSave;
  script::Value m_onAfterSave;
  script::Value m_onSpriteChange;
  base::ScopedConnection m_beforeCommandConn;
  base::ScopedConnection m_afterCommandConn;

  AppScriptObject() {
    addProperty("activeFrameNumber", [this]{return updateSite() ? m_site.frame() : 0;})
      .doc("read-only. Returns the number of the currently active animation frame.");

    addProperty("activeLayerNumber", [this]{return updateSite() ? m_site.layerIndex() : 0;})
      .doc("read-only. Returns the number of the current layer.");

    addProperty("activeImage", [this]{
        return getEngine()->getScriptObject(app::current_editor ? app::current_editor->getSite().image() : nullptr);
    }).doc("read-only, can be null. Returns the current layer/frame's image.");

    addProperty("activeCel", [this]{
        return getEngine()->getScriptObject(updateSite() ? m_site.cel().get() : nullptr);
    }).doc("read-only, can be null. Returns the current Cel.");

    addProperty("activeLayer", [this]{
        return getEngine()->getScriptObject(updateSite() ? m_site.layer() : nullptr);
    }).doc("read-only, can be null. Returns the current Layer.");

    addProperty("activeFrame", [this]{
        return updateSite() ? m_site.frame() : 0;
    }).doc("read-only. Returns the number of the currently active animation frame.");

    addProperty("activeSprite", [this]{
        return getEngine()->getScriptObject(app::current_editor ? app::current_editor->getSite().sprite() : nullptr);
    }).doc("read-only. Returns the currently active Sprite.");

    addProperty("activeDocument", [this]{
        return getEngine()->getScriptObject(app::current_editor ? app::current_editor->getSite().document() : nullptr);
    }).doc("read-only. Returns the currently active Document.");

    addProperty("activeTool",
            [this]() -> std::string {
                auto tool = App::instance() ? App::instance()->activeTool() : nullptr;
                return tool ? tool->getId() : std::string{};
            }
        ).doc("read-only. Returns the selected tool id.");

    addProperty("fgColor",
                []{return makeColorValue(Preferences::instance().colorBar.fgColor());},
                [](script::Value color){
                  Preferences::instance().colorBar.fgColor(colorFromValue(color));
                  return color;
                })
      .doc("read+write. Returns and sets the foreground color.");

    addProperty("bgColor",
                []{return makeColorValue(Preferences::instance().colorBar.bgColor());},
                [](script::Value color){
                  Preferences::instance().colorBar.bgColor(colorFromValue(color));
                  return color;
                })
      .doc("read+write. Returns and sets the background color.");

    addProperty("onBeforeSave",
                [this]{return m_onBeforeSave;},
                [this](script::Value callback){
                  m_onBeforeSave = callback;
                  return callback;
                });

    addProperty("onAfterSave",
                [this]{return m_onAfterSave;},
                [this](script::Value callback){
                  m_onAfterSave = callback;
                  return callback;
                });

    addProperty("onSpriteChange",
                [this]{return m_onSpriteChange;},
                [this](script::Value callback){
                  m_onSpriteChange = callback;
                  return callback;
                });

    addProperty("pixelColor", [this]{return m_pixelColor.get();})
      .doc("read-only. Returns an object with functions for color conversion.");

    addProperty("command", [this]{return m_command.get();})
      .doc("read-only. Returns an object with functions for running commands.");

    addProperty("version", []{return script::Value{VERSION};})
      .doc("read-only. Returns LibreSprite's current version as a string.");

    addProperty("platform", []() -> std::string {
      #ifdef EMSCRIPTEN
      return "emscripten";
      #elif _WIN32
      return "windows";
      #elif __APPLE__
      return "macos";
      #elif ANDROID
      return "android";
      #else
      return "linux";
      #endif
    }).doc("read-only. Returns one of: emscripten, windows, macos, android, linux.");

    addMethod("documentation", &AppScriptObject::documentation)
      .doc("Prints this text.");

    addMethod("createDialog", &AppScriptObject::createDialog)
      .doc("Creates a dialog window");

    addMethod("yield", &AppScriptObject::yield)
      .doc("Schedules a yield event on the next frame")
      .docArg("event", "Name of the event to be raised. The default is yield.");

    addMethod("open", &AppScriptObject::open)
      .doc("Opens a document for editing");

    addMethod("launch", &AppScriptObject::launch);

    addMethod("redraw", &AppScriptObject::redraw);

    addMethod("refresh", &AppScriptObject::redraw);

    addMethod("alert", &AppScriptObject::alert);

    addMethod("transaction", &AppScriptObject::transaction);

    addMethod("useTool", &AppScriptObject::useTool);

    addMethod("undo", &AppScriptObject::undo);

    addMethod("redo", &AppScriptObject::redo);

    addMethod("save", &AppScriptObject::save);

    addMethod("export_", &AppScriptObject::exportFile);

    m_beforeCommandConn = UIContext::instance()->BeforeCommandExecution.connect(
      &AppScriptObject::onBeforeCommandExecution, this);
    m_afterCommandConn = UIContext::instance()->AfterCommandExecution.connect(
      &AppScriptObject::onAfterCommandExecution, this);

    makeGlobal("app");
  }

  void undo() {
    Command* cmd = CommandsModule::instance()->getCommandByName(CommandId::Undo);
    UIContext::instance()->executeCommand(cmd);
  }

  void redo() {
    Command* cmd = CommandsModule::instance()->getCommandByName(CommandId::Redo);
    UIContext::instance()->executeCommand(cmd);
  }

  void save() {
    auto doc = UIContext::instance()->activeDocument();
    if (!doc) return;
    Command* cmd = CommandsModule::instance()->getCommandByName(CommandId::SaveFile);
    UIContext::instance()->executeCommand(cmd);
  }

  void exportFile(script::Value options) {
    auto doc = UIContext::instance()->activeDocument();
    if (!doc) return;
    Params params;
    if (options.type == script::Value::Type::MAP) {
      auto map = static_cast<script::Value::Map::data_t*>(options);
      auto pathIt = map->find("path");
      if (pathIt != map->end())
        params.set("filename", pathIt->second.str().c_str());
    }
    Command* cmd = CommandsModule::instance()->getCommandByName(CommandId::ExportSpriteSheet);
    UIContext::instance()->executeCommand(cmd, params);
  }

  void redraw() {
    ui::Manager::getDefault()->invalidate();
  }

  void alert(const std::string& message) {
    ui::Alert::show("Script Alert<<%s||&OK", message.c_str());
  }

  void transaction(script::Value callback) {
    if (callback.type != script::Value::Type::CALLABLE) {
      callback.call();
      return;
    }

    if (!UIContext::instance()->activeDocument()) {
      callback.call();
      return;
    }

    app::Transaction transaction(UIContext::instance(),
                                 "Script Transaction",
                                 app::ModifyDocument);
    callback.call();
    transaction.commit();
    redraw();
  }

  void useTool(script::Value options) {
    if (!updateSite() || !m_site.image() || !m_site.cel())
      return;

    if (options.type != script::Value::Type::MAP)
      return;

    auto map = static_cast<script::Value::Map::data_t*>(options);
    auto toolValue = mapValue(map, "tool");
    if (toolValue.type != script::Value::Type::UNDEFINED && App::instance()) {
      auto tool = App::instance()->toolBox()->getToolById(toolValue.str());
      if (tool)
        App::instance()->activeToolManager()->setSelectedTool(tool);
    }

    auto points = pointsFromValue(mapValue(map, "points"));
    if (points.empty())
      return;

    auto colorValue = mapValue(map, "color");
    app::Color color = colorValue.type == script::Value::Type::UNDEFINED ?
      Preferences::instance().colorBar.fgColor():
      colorFromValue(colorValue);

    auto image = m_site.image();
    auto cel = m_site.cel();
    auto pixel = m_site.layer() ?
      color_utils::color_for_target(color, ColorTarget(m_site.layer())):
      doc::rgba(color.getRed(), color.getGreen(), color.getBlue(), color.getAlpha());
    int minX = points[0].x;
    int minY = points[0].y;
    int maxX = points[0].x;
    int maxY = points[0].y;

    auto toImagePoint = [&](const gfx::Point& point) {
      return gfx::Point(point.x - cel->x(), point.y - cel->y());
    };

    if (points.size() == 1) {
      auto p = toImagePoint(points[0]);
      if (unsigned(p.x) < unsigned(image->width()) &&
          unsigned(p.y) < unsigned(image->height()))
        image->putPixel(p.x, p.y, pixel);
    } else {
      for (std::size_t i = 1; i < points.size(); ++i) {
        auto a = toImagePoint(points[i - 1]);
        auto b = toImagePoint(points[i]);
        doc::draw_line(image, a.x, a.y, b.x, b.y, pixel);
        minX = std::min(minX, points[i].x);
        minY = std::min(minY, points[i].y);
        maxX = std::max(maxX, points[i].x);
        maxY = std::max(maxY, points[i].y);
      }
    }

    auto doc = static_cast<app::Document*>(m_site.document());
    if (doc) {
      doc->notifySpritePixelsModified(m_site.sprite(),
                                      gfx::Region(gfx::Rect(minX, minY,
                                                            maxX - minX + 1,
                                                            maxY - minY + 1)),
                                      m_site.frame());
    }

    if (m_onSpriteChange)
      m_onSpriteChange.call({"useTool"});
    redraw();
  }

  void onBeforeCommandExecution(CommandExecutionEvent& ev) {
    if (isSaveCommand(ev.command()) && m_onBeforeSave) {
      auto result = m_onBeforeSave.call({ev.command()->id()});
      if (result.type != script::Value::Type::UNDEFINED && !result)
        ev.cancel();
    }
  }

  void onAfterCommandExecution(CommandExecutionEvent& ev) {
    if (isSaveCommand(ev.command())) {
      if (m_onAfterSave)
        m_onAfterSave.call({ev.command()->id()});
      return;
    }

    if (m_onSpriteChange)
      m_onSpriteChange.call({ev.command() ? ev.command()->id() : std::string{}});
  }

  void yield(const std::string& event, int cycles) {
    auto fileName = app::AppScripting::getFileName();
    TaskManager::instance().delayed([=, this] {
      if (cycles > 0) {
        yield(event, cycles - 1);
        return;
      }
      app::AppScripting::raiseEvent(fileName, {event.empty() ? "yield" : event});
    });
  }

  ScriptObject* createDialog(const std::string& id) {
    auto dialog = getEngine()->create<ui::Dialog>();
    if (!dialog)
      return nullptr;

    if (!id.empty())
      dialog->set("id", id);

    return dialog;
  }

  void documentation() {
    std::stringstream out;
    if (!this->get("activeDocument")) {
      return;
    }

    auto& internalRegistry = script::InternalScriptObject::getRegistry();
    auto originalDefault = internalRegistry[""];
    script::InternalScriptObject::setDefault("DudScriptObject");

    for (auto& entry : script::ScriptObject::getRegistry()) {
      if (entry.first.empty())
        continue;
      inject<ScriptObject> so{entry.first};
      auto internal = dynamic_cast<DudScriptObject*>(so->getInternalScriptObject());
      if (!internal)
        continue;

      out << "# ";
      if (!internal->globalName.empty())
        out << "global " << internal->globalName << " ";

      std::string className = entry.first;
      auto dot = className.rfind("ScriptObject");
      if (dot != std::string::npos)
        className.resize(dot);

      out << "[class " << className << "]" << std::endl;

      if (internal->properties.empty()) {
        out << "## No Properties." << std::endl;
      } else {
        out << "## Properties: " << std::endl;
        for (auto& propEntry : internal->properties) {
          auto& prop = propEntry.second;
          out << "   - `" << propEntry.first << "`: " << prop.docStr << std::endl;
        }
      }

      out << std::endl;

      if (internal->functions.empty()) {
        out << "## No Methods." << std::endl;
      } else {
        out << "## Methods: " << std::endl;
        for (auto& funcEntry : internal->functions) {
          auto& func = funcEntry.second;
          out << "   - `" << funcEntry.first << "(";
          bool first = true;
          for (auto& arg : func.docArgs) {
            if (!first) out << ", ";
            first = false;
            out << arg.name;
          }
          out << ")`: " << std::endl;

          for (auto& arg : func.docArgs) {
            out << "     - " << arg.name << ": " << arg.docStr << std::endl;
          }

          out << "      returns: " << func.docReturnsStr << std::endl;

          if (!func.docStr.empty())
            out << "      " << func.docStr << std::endl;

          out << std::endl;
        }
      }
      out << std::endl << std::endl;
    }

    std::cout << out.str() << std::endl;

    // inject<script::EngineDelegate>{}->onConsolePrint(out.str().c_str());

    internalRegistry[""] = originalDefault;
  }

  bool updateSite() {
    app::Document* doc = UIContext::instance()->activeDocument();
    app::DocumentView* m_view = UIContext::instance()->getFirstDocumentView(doc);
    if (!m_view)
      return false;
    m_view->getSite(&m_site);
    return true;
  }

  script::Value open(const std::string& fn) {
    if (fn.empty())
      return {};
    auto oldDoc = static_cast<doc::Document*>(UIContext::instance()->activeDocument());
    Command* openCommand = CommandsModule::instance()->getCommandByName(CommandId::OpenFile);
    Params params;
    params.set("filename", fn.c_str());
    UIContext::instance()->executeCommand(openCommand, params);
    auto newDoc = static_cast<doc::Document*>(UIContext::instance()->activeDocument());
    if (newDoc == oldDoc)
      return {};
    return getEngine()->getScriptObject(newDoc);
  }

  bool launch(const std::string& cmd) {
    return base::launcher::open_file(cmd);
  }

  void App_exit() {
    Command* exitCommand = CommandsModule::instance()->getCommandByName(CommandId::Exit);
    UIContext::instance()->executeCommand(exitCommand);
  }

  std::unordered_map<ui::Widget*, ScriptObject*> m_dialogScriptObjects;
  doc::Site m_site;
};

static script::ScriptObject::Regular<AppScriptObject> reg("AppScriptObject", {"global"});

}
