#include "runtime.h"

using v8::Function;
using v8::Handle;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

namespace frida {

Runtime::Runtime(UVContext* uv_context, GLibContext* glib_context)
  : uv_context_(uv_context),
    glib_context_(glib_context),
    data_(g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL)) {
  auto isolate = Isolate::GetCurrent();
  auto global = isolate->GetCurrentContext()->Global();
  auto json_module = Local<Object>::Cast(
      global->Get(String::NewFromUtf8(isolate, "JSON")));
  auto json_stringify = Local<Function>::Cast(
      json_module->Get(String::NewFromUtf8(isolate, "stringify")));
  auto json_parse = Local<Function>::Cast(
      json_module->Get(String::NewFromUtf8(isolate, "parse")));
  json_module_.Reset(isolate, json_module);
  json_stringify_.Reset(isolate, json_stringify);
  json_parse_.Reset(isolate, json_parse);
}

Runtime::~Runtime() {
  json_parse_.Reset();
  json_stringify_.Reset();
  json_module_.Reset();

  g_hash_table_unref(data_);

  delete glib_context_;
  delete uv_context_;
}

UVContext* Runtime::GetUVContext() const {
  return uv_context_;
}

GLibContext* Runtime::GetGLibContext() const {
  return glib_context_;
}

void* Runtime::GetDataPointer(const char* id) {
  return g_hash_table_lookup(data_, id);
}

void Runtime::SetDataPointer(const char* id, void* value) {
  g_hash_table_insert(data_, const_cast<char*>(id), value);
}

Local<String> Runtime::ValueToJson(Isolate* isolate, Handle<Value> value) {
  auto module = Local<Object>::New(isolate, json_module_);
  auto stringify = Local<Function>::New(isolate, json_stringify_);
  Local<Value> argv[] = { value };
  return Local<String>::Cast(stringify->Call(module, 1, argv));
}

Local<Value> Runtime::ValueFromJson(Isolate* isolate, Handle<String> json) {
  auto module = Local<Object>::New(isolate, json_module_);
  auto parse = Local<Function>::New(isolate, json_parse_);
  Local<Value> argv[] = { json };
  return parse->Call(module, 1, argv);
}

}
