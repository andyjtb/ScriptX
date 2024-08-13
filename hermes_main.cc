#include <hermes/hermes.h>
#include <iostream>

#include <jsi/decorator.h>
#include <jsi/jsilib.h>

#include <ScriptX/ScriptX.h>

#include <iostream>

#include <filesystem>
#include <fstream>

#define CATCH_CONFIG_ENABLE_BENCHMARKING 1
#define CATCH_CONFIG_RUNNER
#include <catch.hpp>
#include <string>

std::shared_ptr<script::ScriptEngine> createEngine() {
#if !defined(SCRIPTX_BACKEND_WEBASSEMBLY)
  return std::shared_ptr<script::ScriptEngine>{new script::ScriptEngineImpl(),
                                               script::ScriptEngine::Deleter()};
#else
  return std::shared_ptr<script::ScriptEngine>{script::ScriptEngineImpl::instance(), [](void*) {}};
#endif
}

// const auto codePath = "/Users/andy/Desktop/browsers/ScriptX/code.js";
// const auto bytecodePath = "/Users/andy/Desktop/browsers/ScriptX/myfile.hbc";
const auto codePath = "/Users/andy/Desktop/browsers/ScriptX/full-air.js";
const auto bytecodePath = "/Users/andy/Desktop/browsers/ScriptX/full-air.hbc";

TEST_CASE("Test", "") {
  std::filesystem::path p(codePath);
  std::ifstream file(p, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return;
  }

  // Read the file contents into a string
  std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

  // Close the file
  file.close();
  BENCHMARK("Load string") {
    auto engine = createEngine();
    {
      try {
        script::EngineScope enter(engine.get());
        const auto out = engine->eval(content, 123);
        // REQUIRE(out.describeUtf8() == "132");
      } catch (...) {
      }
    }
  };

  p = std::filesystem::path(bytecodePath);
  file = std::ifstream(p, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return;
  }

  // Read the file contents into a string
  content = std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

  // Close the file
  file.close();

  BENCHMARK("Load bytecode") {
    auto engine = createEngine();
    {
      try {
        script::EngineScope enter(engine.get());
        const auto out = engine->eval(content, 123);
        // REQUIRE(out.describeUtf8() == "132");
      } catch (...) {
      }
    }
  };
}

namespace demo {

// ability provided by embedder
namespace host_ability {

class HostImage {
  std::string src_;

 public:
  void setSrc(const std::string& src) {
    src_ = src;
    downloadImage(src);
  }

  const std::string& getSrc() const { return src_; }

  int getWidth() const { return 0; }
  int getHeight() const { return 0; }

  void drop() { src_ = {}; }

 private:
  void downloadImage(const std::string& src) {}
};

void drawImage(HostImage* img) {
  std::ostringstream() << "api: drawImage: " << img->getSrc() << std::endl;
}

void sendMessage(const std::string& to, const std::string& message) {
  std::ostringstream() << "api: sendMessage: [" << message << "] to: [" << to << "]" << std::endl;
}

}  // namespace host_ability

std::shared_ptr<script::ScriptEngine> createEngine();

void exportHostAbility(const std::shared_ptr<script::ScriptEngine>& engine);

std::string_view getScriptBaseLibrary();

std::string_view downloadGameScript();

void runMiniGame() {
  auto engine = createEngine();

  SECTION("String") {
    {
      script::EngineScope enter(engine.get());
      engine->set("testing", "123");
      REQUIRE(engine->get("testing").describeUtf8() == "123");
    }

    // Outside set scope
    {
      script::EngineScope enter(engine.get());
      REQUIRE(engine->get("testing").describeUtf8() == "123");
    }
  }

  SECTION("UTF8 String") {
    script::EngineScope enter(engine.get());
    engine->set("testing", std::u8string(u8"123ЁЁ"));
    REQUIRE(engine->get("testing").describeUtf8() == "123ЁЁ");
    REQUIRE(engine->get("testing").asString().toU8string() == u8"123ЁЁ");
  }

  SECTION("Number") {
    script::EngineScope enter(engine.get());
    engine->set("testing", 321);
    REQUIRE(engine->get("testing").describeUtf8() == "321");
    REQUIRE(engine->get("testing").asNumber().toInt32() == 321);
  }

  SECTION("Boolean") {
    script::EngineScope enter(engine.get());
    engine->set("testing", false);
    REQUIRE(engine->get("testing").describeUtf8() == "false");
    REQUIRE(engine->get("testing").asBoolean().value() == false);
  }

  SECTION("Array") {
    script::EngineScope enter(engine.get());
    const auto arr = engine->eval("const a = [1,2,3]; a");
    REQUIRE(arr.isArray());
    REQUIRE(arr.asArray().size() == 3);

    for (int i = 0; i < 3; i++) REQUIRE(arr.asArray().get(i).asNumber().toInt32() == i + 1);
    for (int i = 0; i < 3; i++) arr.asArray().set(i, 3 - i);
    for (int i = 0; i < 3; i++) REQUIRE(arr.asArray().get(i).asNumber().toInt32() == 3 - i);

    arr.asArray().clear();
    REQUIRE(arr.isArray());
    REQUIRE(arr.asArray().size() == 0);

    for (int i = 0; i < 3; i++)
      arr.asArray().add(script::converter::Converter<int>::toScript(3 - i));
    REQUIRE(arr.asArray().size() == 3);
    for (int i = 0; i < 3; i++) REQUIRE(arr.asArray().get(i).asNumber().toInt32() == 3 - i);
  }

  SECTION("Object") {
    script::EngineScope enter(engine.get());
    const auto obj = engine->eval("const a = { 'property': 'Value', 'other': 123}; a");
    REQUIRE(obj.isObject());
    REQUIRE(obj.asObject().getKeys().size() == 2);

    REQUIRE(obj.asObject().has("property"));
    REQUIRE(obj.asObject().has("other"));

    const auto prop = obj.asObject().get("property");
    REQUIRE(prop.describeUtf8() == "Value");
    REQUIRE(prop.isString());

    auto other = obj.asObject().get("other");
    REQUIRE(other.describeUtf8() == "123");
    REQUIRE(other.isNumber());

    obj.asObject().set("other", "new other");
    other = obj.asObject().get("other");
    REQUIRE(other.describeUtf8() == "new other");
    REQUIRE(other.isString());

    obj.asObject().set("new_other", "some other prop");
    other = obj.asObject().get("new_other");
    REQUIRE(other.describeUtf8() == "some other prop");
    REQUIRE(other.isString());

    obj.asObject().remove("property");
    REQUIRE(obj.asObject().getKeys().size() == 2);
    REQUIRE_FALSE(obj.asObject().has("property"));
  }

  SECTION("Function") {
    script::EngineScope enter(engine.get());
    const auto func = engine->eval(
        "function testFunction(num1, num2, num3) { return num1 + num2 + num3; }; testFunction");
    REQUIRE(func.isFunction());

    auto res = func.asFunction().call(func, 1, 2, 3);
    REQUIRE(res.isNumber());
    REQUIRE(res.describeUtf8() == "6");
  }

  SECTION("Construct Array") {
    script::EngineScope enter(engine.get());
    const auto func = engine->eval(
        "function sumArray(arr) { var total = 0;  for (var i = 0; i < arr.length; i++) { total += "
        "arr[i]; } "
        "return total; }; sumArray");
    REQUIRE(func.isFunction());

    script::Local<script::Array> arr = script::Array::newArray();
    SECTION("Add") {
      for (int i = 0; i < 3; i++) arr.add(script::converter::Converter<int>::toScript(i + 1));
    }

    SECTION("Set") {
      arr = script::Array::newArray(3);
      for (int i = 0; i < 3; i++) arr.set(i, script::converter::Converter<int>::toScript(i + 1));
    }
    auto res = func.asFunction().call(func, arr);
    REQUIRE(res.isNumber());
    REQUIRE(res.describeUtf8() == "6");
  }

  SECTION("Construct Object") {
    script::EngineScope enter(engine.get());
    script::Local<script::Object> obj = script::Object::newObject();

    for (int i = 0; i < 3; i++)
      obj.set(std::to_string(i), script::converter::Converter<int>::toScript(i + 1));

    REQUIRE(obj.getKeys().size() == 3);
    const auto a = obj.describeUtf8();
    for (int i = 0; i < 3; i++) REQUIRE(obj.get(std::to_string(i)).asNumber().toInt32() == i + 1);
  }

  SECTION("Construct Function") {
    script::EngineScope enter(engine.get());
    std::string capturedMsg;

    auto log = script::Function::newFunction(
        [&capturedMsg](const std::string& msg) { capturedMsg = msg; });

    engine->set("log", log);
    engine->eval("log('hello world');");

    REQUIRE(capturedMsg == "hello world");
  }

  SECTION("Exception") {
    script::EngineScope enter(engine.get());

    try {
      engine->eval("log('hello world');");
    } catch (script::Exception& e) {
      REQUIRE(e.message() == "Property 'log' doesn't exist");
      REQUIRE(e.stacktrace() ==
              "ReferenceError: Property 'log' doesn't exist\n    at global (:1:1)");
    }
  }

  SECTION("Register Native Class") {
    script::EngineScope enter(engine.get());
    exportHostAbility(engine);
    engine->eval(getScriptBaseLibrary());
  }

  //  // 3. run downloaded game script
  //  {
  //    script::EngineScope enter(engine.get());
  //    try {
  //      engine->eval(downloadGameScript());
  //    } catch (const script::Exception& e) {
  //      std::cerr << "failed to run game " << e;
  //    }
  //  }
  // exit and shutdown.
}

std::shared_ptr<script::ScriptEngine> createEngine() {
#if !defined(SCRIPTX_BACKEND_WEBASSEMBLY)
  return std::shared_ptr<script::ScriptEngine>{new script::ScriptEngineImpl(),
                                               script::ScriptEngine::Deleter()};
#else
  return std::shared_ptr<script::ScriptEngine>{script::ScriptEngineImpl::instance(), [](void*) {}};
#endif
}

void exportHostAbility(const std::shared_ptr<script::ScriptEngine>& engine) {
  using host_ability::HostImage;

  // wrapper inherits from HostImage and ScriptClass
  class HostImageWrapper : public HostImage, public script::ScriptClass {
   public:
    using script::ScriptClass::ScriptClass;
  };

  static script::ClassDefine<HostImageWrapper> hostImageDef =
      script::defineClass<HostImageWrapper>("Image")
          .constructor()
          .instanceProperty("src", &HostImage::getSrc, &HostImage::setSrc)
          .instanceProperty("width", &HostImage::getWidth, nullptr)
          .instanceProperty("height", &HostImage::getWidth, nullptr)
          .instanceFunction("drop", &HostImage::drop)
          .build();

  engine->registerNativeClass(hostImageDef);

  auto drawImageFunc =
      script::Function::newFunction([](HostImageWrapper* img) { host_ability::drawImage(img); });
  engine->set("_drawImage", drawImageFunc);

  auto sendMessageFunc = script::Function::newFunction(host_ability::sendMessage);
  engine->set("_sendMessage", sendMessageFunc);
}

std::string_view getScriptBaseLibrary() {
  using std::string_view_literals::operator""sv;
#ifdef SCRIPTX_LANG_JAVASCRIPT
  return R"(

var API = {};
API.createImage = function(src) {
  let img = new Image();
  img.src = src;
  return img;
};

API.drawImage = function(img) { _drawImage(img); };

API.sendMessage = function(to, message) { _sendMessage(to, message); };

)"sv;
#elif defined(SCRIPTX_LANG_LUA)
  return R"(

API = {};
function API.createImage(src)
  local img = Image();
  img.src = src;
  return img;
end

function API.drawImage(img) _drawImage(img); end
function API.sendMessage(to, message) _sendMessage(to, message); end

)"sv;

#else
  throw std::logic_error("add for script language");
#endif
}

std::string_view downloadGameScript() {
  using std::string_view_literals::operator""sv;

#ifdef SCRIPTX_LANG_JAVASCRIPT
  return R"(
    var img = API.createImage("https://landerlyoung.github.io/images/profile.png");
    API.drawImage(img);
    img.drop();

    API.sendMessage("jenny", "hello there!");
)";
#elif defined(SCRIPTX_LANG_LUA)
  return R"(
    local img = API.createImage("https://landerlyoung.github.io/images/profile.png");
    API.drawImage(img);
    img:drop();

    API.sendMessage("jenny", "hello there!");
)";
#else
  throw std::logic_error("add for script language");
#endif
}

}  // namespace demo

TEST_CASE("asd") { demo::runMiniGame(); }

int main(int argc, char* argv[]) {
  //
  //  return 0;
  return Catch::Session().run(argc, argv);
}