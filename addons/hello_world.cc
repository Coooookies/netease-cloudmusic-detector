#include <napi.h>

class HelloWorld : public Napi::ObjectWrap<HelloWorld> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  HelloWorld(const Napi::CallbackInfo& info);

 private:
  static Napi::FunctionReference constructor;
  Napi::Value SayHello(const Napi::CallbackInfo& info);
};

Napi::FunctionReference HelloWorld::constructor;

Napi::Object HelloWorld::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "HelloWorld", {
    InstanceMethod("sayHello", &HelloWorld::SayHello),
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("HelloWorld", func);
  return exports;
}

HelloWorld::HelloWorld(const Napi::CallbackInfo& info) 
  : Napi::ObjectWrap<HelloWorld>(info) {}

Napi::Value HelloWorld::SayHello(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  return Napi::String::New(env, "Hello World");
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return HelloWorld::Init(env, exports);
}

NODE_API_MODULE(hello_world, InitAll) 