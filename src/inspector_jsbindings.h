#ifndef SRC_INSPECOTOR_BINDINGS_H_
#define SRC_INSPECOTOR_BINDINGS_H_

#include "v8-inspector.h"

#include <stddef.h>

#if !HAVE_INSPECTOR
#error("This header can only be used when inspector is enabled")
#endif

namespace v8 {
template<typename T>
class Local;
class Value;
}

namespace node {
class Environment;

namespace inspector {

class InspectorJSBindings {
 public:
  bool Install(Environment* env, v8::Local<v8::Object> inspector);
};

}  // namespace inspector
}  // namespace node

#endif  // SRC_INSPECOTOR_BINDINGS_H_
