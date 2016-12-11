#include "inspector_jsbindings.h"

#include "env.h"
#include "env-inl.h"
#include "inspector_agent.h"
#include "node_inspector.h"
#include "node.h"
#include "v8-platform.h"
#include "v8-inspector.h"

#include <vector>

namespace node {
namespace inspector {
namespace {
using namespace v8;

class JsBindingsSessionDelegate : public NodeInspectorSessionDelegate {
 public:
  JsBindingsSessionDelegate(Environment* env,
                            Local<Object> receiver,
                            Local<Function> callback)
                            : env_(env),
                              receiver_(env->isolate(), receiver),
                              callback_(env->isolate(), callback) {}

  ~JsBindingsSessionDelegate() {
    receiver_.Reset();
    callback_.Reset();
  }

  bool WaitForFrontendMessage() override {
    return false;
  }

  void OnMessage(const v8_inspector::StringView message) override {
    MaybeLocal<String> v8string =
        String::NewFromTwoByte(env_->isolate(), message.characters16(),
                               NewStringType::kNormal, message.length());
    Local<Value> argument = v8string.ToLocalChecked().As<Value>();
    TryCatch try_catch(env_->isolate());
    Local<Function> callback = callback_.Get(env_->isolate());
    Local<Object> receiver = receiver_.Get(env_->isolate());
    callback->Call(receiver, 1, &argument);
    if (try_catch.HasCaught())
      try_catch.ReThrow();
  }
 private:
  Environment* env_;
  Persistent<Object> receiver_;
  Persistent<Function> callback_;
};

NodeInspector* GetInspectorChecked(Environment* env) {
  Agent* agent = env->inspector_agent();
  if (!agent) {
    env->ThrowError("Inspector was not setup");
    return nullptr;
  }
  return agent->inspector();  
}

void Connect(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  if (info.Length() != 1 || !info[0]->IsFunction()) {
    env->ThrowError("Message callback must be specified");
    return;
  }
  NodeInspector* inspector = GetInspectorChecked(env);
  if (!inspector) {
    return;
  }
  if (inspector->delegate()) {
    env->ThrowError("Inspector listener already setup");
    return;
  }
  JsBindingsSessionDelegate* delegate =
      new JsBindingsSessionDelegate(env, info.Holder(),
                                    info[0].As<Function>());
  inspector->Connect(delegate);
}

static void Dispatch(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  NodeInspector* inspector = GetInspectorChecked(env);
  if (!inspector) {
    return;
  }
  if (!inspector->delegate()) {
    env->ThrowError("Inspector is not connected");
    return;
  }
  if (info.Length() != 1 || !info[0]->IsString()) {
    env->ThrowError("Inspector message must be a string");
    return;
  }
  Local<String> msg = info[0].As<String>();
  std::vector<uint16_t> contents(msg->Length(), 0);
  msg->Write(contents.data());
  v8_inspector::StringView message(contents.data(), contents.size());
  inspector->Dispatch(message);
}

static void Disconnect(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  NodeInspector* inspector = GetInspectorChecked(env);
  if (!inspector) {
    return;
  }
  if (!inspector->delegate()) {
    env->ThrowError("Inspector is not connected");
    return;
  }
  Local<String> msg = info[0].As<String>();
  std::vector<uint16_t> contents(msg->Length(), 0);
  msg->Write(contents.data());
  inspector->Disconnect();
}

}

#define READONLY_PROPERTY(obj, str, var)                                      \
  do {                                                                        \
    obj->DefineOwnProperty(env->context(),                                    \
                           OneByteString(env->isolate(), str),                \
                           var,                                               \
                           ReadOnly).FromJust();                              \
  } while (0)


bool InspectorJSBindings::Install(Environment* env,
                                  Local<Object> inspector) {
  env->SetMethod(inspector, "Connect", Connect);
  env->SetMethod(inspector, "Dispatch", Dispatch);
  env->SetMethod(inspector, "Disconnect", Disconnect);
  return true;
}

}  // namespace inspector
}  // namespace node
