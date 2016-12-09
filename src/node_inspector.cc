#include "node_inspector.h"

#include "node.h"
#include "env.h"
#include "env-inl.h"

#include "v8-inspector.h"
#include "v8-platform.h"

#include "libplatform/libplatform.h"


namespace node {
namespace inspector {

namespace {
using v8_inspector::StringBuffer;
using v8_inspector::StringView;
using V8Inspector = v8_inspector::V8Inspector;

// Used in NodeInspectorClient::currentTimeMS() below.
const int NANOS_PER_MSEC = 1000000;
const int CONTEXT_GROUP_ID = 1;

std::unique_ptr<StringBuffer> ToProtocolString(v8::Local<v8::Value> value) {
  if (value.IsEmpty() || value->IsNull() || value->IsUndefined() ||
      !value->IsString()) {
    return StringBuffer::create(StringView());
  }
  v8::Local<v8::String> string_value = v8::Local<v8::String>::Cast(value);
  size_t len = string_value->Length();
  std::basic_string<uint16_t> buffer(len, '\0');
  string_value->Write(&buffer[0], 0, len);
  return StringBuffer::create(StringView(buffer.data(), len));
}

class ChannelImpl final : public v8_inspector::V8Inspector::Channel {
 public:
  explicit ChannelImpl(V8Inspector* inspector,
                       NodeInspectorSessionDelegate* delegate)
                       : delegate_(delegate) {
    session_ = inspector->connect(1, this, StringView());
  }

  virtual ~ChannelImpl() {}

  void dispatchProtocolMessage(const StringView& message) {
    session_->dispatchProtocolMessage(message);
  }

  bool waitForFrontendMessage() {
    return delegate_->WaitForFrontendMessage();
  }
 private:
  void sendProtocolResponse(int callId, const StringView& message) override {
    sendMessageToFrontend(message);
  }

  void sendProtocolNotification(const StringView& message) override {
    sendMessageToFrontend(message);
  }

  void flushProtocolNotifications() override { }

  void sendMessageToFrontend(const StringView& message) {
    delegate_->OnMessage(message);
  }

  NodeInspectorSessionDelegate* const delegate_;
  std::unique_ptr<v8_inspector::V8InspectorSession> session_;
};
}

class NodeInspectorClient : public v8_inspector::V8InspectorClient {
 public:
  NodeInspectorClient(node::Environment* env,
                      v8::Platform* platform) : env_(env),
                                                platform_(platform),
                                                terminated_(false),
                                                running_nested_loop_(false) {
    inspector_ = V8Inspector::create(env->isolate(), this);
    const uint8_t CONTEXT_NAME[] = "Node.js Main Context";
    StringView context_name(CONTEXT_NAME, sizeof(CONTEXT_NAME) - 1);
    v8_inspector::V8ContextInfo info(env->context(), CONTEXT_GROUP_ID,
                                     context_name);
    inspector_->contextCreated(info);
  }

  void runMessageLoopOnPause(int context_group_id) override {
    CHECK(channel_);
    if (running_nested_loop_)
      return;
    terminated_ = false;
    running_nested_loop_ = true;
    while (!terminated_ && channel_->waitForFrontendMessage()) {
      while (v8::platform::PumpMessageLoop(platform_, env_->isolate()))
        {}
    }
    terminated_ = false;
    running_nested_loop_ = false;
  }

  double currentTimeMS() override {
    return uv_hrtime() * 1.0 / NANOS_PER_MSEC;
  }

  void quitMessageLoopOnPause() override {
    terminated_ = true;
  }

  void connectFrontend(NodeInspectorSessionDelegate* delegate) {
    CHECK(!channel_);
    channel_ = std::unique_ptr<ChannelImpl>(
        new ChannelImpl(inspector_.get(), delegate));
  }

  void disconnectFrontend() {
    quitMessageLoopOnPause();
    channel_.reset();
  }

  void dispatchMessageFromFrontend(const StringView& message) {
    CHECK(channel_);
    channel_->dispatchProtocolMessage(message);
  }

  v8::Local<v8::Context> ensureDefaultContextInGroup(int contextGroupId)
      override {
    return env_->context();
  }

  void FatalException(v8::Local<v8::Value> error,
                      v8::Local<v8::Message> message) {
    v8::Local<v8::Context> context = env_->context();

    int script_id = message->GetScriptOrigin().ScriptID()->Value();

    v8::Local<v8::StackTrace> stack_trace = message->GetStackTrace();

    if (!stack_trace.IsEmpty() &&
        stack_trace->GetFrameCount() > 0 &&
        script_id == stack_trace->GetFrame(0)->GetScriptId()) {
      script_id = 0;
    }

    const uint8_t DETAILS[] = "Uncaught";

    inspector_->exceptionThrown(
        context,
        StringView(DETAILS, sizeof(DETAILS) - 1),
        error,
        ToProtocolString(message->Get())->string(),
        ToProtocolString(message->GetScriptResourceName())->string(),
        message->GetLineNumber(context).FromMaybe(0),
        message->GetStartColumn(context).FromMaybe(0),
        inspector_->createStackTrace(stack_trace),
        script_id);
  }

 private:
  node::Environment* env_;
  v8::Platform* platform_;
  bool terminated_;
  bool running_nested_loop_;
  std::unique_ptr<V8Inspector> inspector_;
  std::unique_ptr<ChannelImpl> channel_;
};

NodeInspector::NodeInspector(Environment* env, v8::Platform* platform)
                             : client_(new NodeInspectorClient(env, platform)) {
}

NodeInspector::~NodeInspector() {
  delete client_;
}

void NodeInspector::RunMessageLoop() {
  client_->runMessageLoopOnPause(CONTEXT_GROUP_ID);
}

void NodeInspector::FatalException(v8::Local<v8::Value> error,
                                   v8::Local<v8::Message> message) {
  client_->FatalException(error, message);
}

void NodeInspector::Connect(NodeInspectorSessionDelegate* delegate) {
  client_->connectFrontend(delegate);
}

void NodeInspector::Disconnect() {
  client_->disconnectFrontend();
}

void NodeInspector::Dispatch(const v8_inspector::StringView message) {
  client_->dispatchMessageFromFrontend(message);
}

}  // namespace inspector
}  // namespace node
