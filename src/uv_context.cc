#include "uv_context.h"

#include <node.h>
#include <v8.h>

#define UV_CONTEXT_LOCK()   g_mutex_lock(&mutex_)
#define UV_CONTEXT_UNLOCK() g_mutex_unlock(&mutex_)
#define UV_CONTEXT_WAIT()   g_cond_wait(&cond_, &mutex_)
#define UV_CONTEXT_SIGNAL() g_cond_signal(&cond_)

using v8::Context;
using v8::External;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

namespace frida {

UVContext::UVContext(uv_loop_t* loop) : usage_count_(0), pending_(NULL) {
  uv_async_init(loop, &async_, ProcessPendingWrapper);
  uv_unref(reinterpret_cast<uv_handle_t*>(&async_));
  async_.data = this;
  g_mutex_init(&mutex_);
  g_cond_init(&cond_);

  auto isolate = Isolate::GetCurrent();
  auto module = Object::New(isolate);
  auto process_pending = Function::New(isolate, ProcessPendingWrapper,
      External::New(isolate, this));
  auto process_pending_name = String::NewFromUtf8(isolate, "processPending");
  process_pending->SetName(process_pending_name);
  module->Set(process_pending_name, process_pending);
  module_.Reset(isolate, module);
  process_pending_.Reset(isolate, process_pending);
}

UVContext::~UVContext() {
  process_pending_.Reset();
  module_.Reset();
  g_cond_clear(&cond_);
  g_mutex_clear(&mutex_);
  uv_close(reinterpret_cast<uv_handle_t*>(&async_), NULL);
}

void UVContext::IncreaseUsage() {
  if (++usage_count_ == 1)
    uv_ref(reinterpret_cast<uv_handle_t*>(&async_));
}

void UVContext::DecreaseUsage() {
  if (usage_count_-- == 1)
    uv_unref(reinterpret_cast<uv_handle_t*>(&async_));
}

void UVContext::Schedule(std::function<void ()> f) {
  auto work = new std::function<void ()>(f);
  UV_CONTEXT_LOCK();
  pending_ = g_slist_append(pending_, work);
  UV_CONTEXT_UNLOCK();
  uv_async_send(&async_);
}

void UVContext::Perform(std::function<void ()> f) {
  volatile bool finished = false;

  Schedule([this, f, &finished]() {
    f();

    UV_CONTEXT_LOCK();
    finished = true;
    UV_CONTEXT_SIGNAL();
    UV_CONTEXT_UNLOCK();
  });

  UV_CONTEXT_LOCK();
  while (!finished)
    UV_CONTEXT_WAIT();
  UV_CONTEXT_UNLOCK();
}

void UVContext::ProcessPending() {
  UV_CONTEXT_LOCK();
  while (pending_ != NULL) {
    auto work = static_cast<std::function<void ()>*>(pending_->data);
    pending_ = g_slist_delete_link(pending_, pending_);
    UV_CONTEXT_UNLOCK();
    (*work)();
    delete work;
    UV_CONTEXT_LOCK();
  }
  UV_CONTEXT_UNLOCK();
}

void UVContext::ProcessPendingWrapper(const FunctionCallbackInfo<Value>& args) {
  UVContext* self = static_cast<UVContext*>(
      args.Data().As<External>()->Value ());
  self->ProcessPending();
}

void UVContext::ProcessPendingWrapper(uv_async_t* handle) {
  auto isolate = Isolate::GetCurrent();
  HandleScope handle_scope(isolate);

  auto self = static_cast<UVContext*>(handle->data);
  auto module = Local<Object>::New(isolate, self->module_);
  auto process_pending = Local<Function>::New(isolate, self->process_pending_);
  node::MakeCallback(isolate, module, process_pending, 0, NULL);
}

}
