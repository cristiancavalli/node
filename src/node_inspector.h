#ifndef SRC_NODE_INSPECTOR_H_
#define SRC_NODE_INSPECTOR_H_

#include "v8-inspector.h"

#include <stddef.h>

#if !HAVE_INSPECTOR
#error("This header can only be used when inspector is enabled")
#endif

namespace v8 {
class Platform;
template<typename T>
class Local;
class Value;
class Message;
}

namespace node {
class Environment;

namespace inspector {
class NodeInspectorClient;

class NodeInspectorSessionDelegate {
 public:
  virtual bool WaitForFrontendMessage() = 0;
  virtual void OnMessage(const v8_inspector::StringView message) = 0;
};

class NodeInspector {
 public:
  NodeInspector(Environment* env, v8::Platform* platform);
  ~NodeInspector();
  void RunMessageLoop();
  void FatalException(v8::Local<v8::Value> error,
                      v8::Local<v8::Message> message);
  void Connect(NodeInspectorSessionDelegate* delegate);
  void Disconnect();
  void Dispatch(const v8_inspector::StringView message);
 private:
  NodeInspectorClient* const client_;
};

}  // namespace inspector
}  // namespace node

#endif  // SRC_NODE_INSPECTOR_H_
