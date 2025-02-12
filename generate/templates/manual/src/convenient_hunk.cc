#include <nan.h>
#include <string.h>

extern "C" {
  #include <git2.h>
}

#include "../include/context.h"
#include "../include/functions/copy.h"
#include "../include/convenient_hunk.h"
#include "../include/diff_line.h"

using namespace std;
using namespace v8;
using namespace node;

void HunkDataFree(HunkData *hunk) {
  while (!hunk->lines->empty()) {
    git_diff_line *line = hunk->lines->back();
    hunk->lines->pop_back();
    free((void *)line->content);
    free((void *)line);
  }
  delete hunk->lines;
  delete hunk;
}

ConvenientHunk::ConvenientHunk(HunkData *raw) {
  this->hunk = raw;
}

ConvenientHunk::~ConvenientHunk() {
  HunkDataFree(this->hunk);
}

void ConvenientHunk::InitializeComponent(Local<v8::Object> target, nodegit::Context *nodegitContext) {
  Nan::HandleScope scope;

  Local<External> nodegitExternal = Nan::New<External>(nodegitContext);
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(JSNewFunction, nodegitExternal);

  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  tpl->SetClassName(Nan::New("ConvenientHunk").ToLocalChecked());

  Nan::SetPrototypeMethod(tpl, "size", Size, nodegitExternal);
  Nan::SetPrototypeMethod(tpl, "lines", Lines, nodegitExternal);

  Nan::SetPrototypeMethod(tpl, "oldStart", OldStart, nodegitExternal);
  Nan::SetPrototypeMethod(tpl, "oldLines", OldLines, nodegitExternal);
  Nan::SetPrototypeMethod(tpl, "newStart", NewStart, nodegitExternal);
  Nan::SetPrototypeMethod(tpl, "newLines", NewLines, nodegitExternal);
  Nan::SetPrototypeMethod(tpl, "headerLen", HeaderLen, nodegitExternal);
  Nan::SetPrototypeMethod(tpl, "header", Header, nodegitExternal);

  Local<Function> constructor_template = Nan::GetFunction(tpl).ToLocalChecked();
  nodegitContext->SaveToPersistent("ConvenientHunk::Template", constructor_template);
  Nan::Set(target, Nan::New("ConvenientHunk").ToLocalChecked(), constructor_template);
}

NAN_METHOD(ConvenientHunk::JSNewFunction) {

  if (info.Length() == 0 || !info[0]->IsExternal()) {
       return Nan::ThrowError("A new ConvenientHunk cannot be instantiated.");
   }

  ConvenientHunk* object = new ConvenientHunk(static_cast<HunkData *>(Local<External>::Cast(info[0])->Value()));
  object->Wrap(info.This());

  info.GetReturnValue().Set(info.This());
}

Local<v8::Value> ConvenientHunk::New(void *raw) {
  Nan::EscapableHandleScope scope;
  Local<v8::Value> argv[1] = { Nan::New<External>((void *)raw) };
  nodegit::Context *nodegitContext = nodegit::Context::GetCurrentContext();
  Local<Function> constructor_template = nodegitContext->GetFromPersistent("ConvenientHunk::Template").As<Function>();
  return scope.Escape(Nan::NewInstance(constructor_template, 1, argv).ToLocalChecked());
}

HunkData *ConvenientHunk::GetValue() {
  return this->hunk;
}

size_t ConvenientHunk::GetSize() {
  return this->hunk->numLines;
}

NAN_METHOD(ConvenientHunk::Size) {
  Local<v8::Value> to;
  to = Nan::New<Number>(Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetSize());
  info.GetReturnValue().Set(to);
}

NAN_METHOD(ConvenientHunk::Lines) {
  if (info.Length() == 0 || !info[0]->IsFunction()) {
    return Nan::ThrowError("Callback is required and must be a Function.");
  }

  LinesBaton *baton = new LinesBaton();

  baton->hunk = Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetValue();
  baton->lines = new std::vector<git_diff_line *>;
  baton->lines->reserve(baton->hunk->numLines);

  Nan::Callback *callback = new Nan::Callback(Local<Function>::Cast(info[0]));
  LinesWorker *worker = new LinesWorker(baton, callback);

  worker->Reference<ConvenientHunk>("hunk", info.This());

  nodegit::Context *nodegitContext = reinterpret_cast<nodegit::Context *>(info.Data().As<External>()->Value());
  nodegitContext->QueueWorker(worker);
  return;
}

nodegit::LockMaster ConvenientHunk::LinesWorker::AcquireLocks() {
  return nodegit::LockMaster(true);
}

void ConvenientHunk::LinesWorker::Execute() {
  for (unsigned int i = 0; i < baton->hunk->numLines; ++i) {
    git_diff_line *storeLine = (git_diff_line *)malloc(sizeof(git_diff_line));
    storeLine->origin = baton->hunk->lines->at(i)->origin;
    storeLine->old_lineno = baton->hunk->lines->at(i)->old_lineno;
    storeLine->new_lineno = baton->hunk->lines->at(i)->new_lineno;
    storeLine->num_lines = baton->hunk->lines->at(i)->num_lines;
    storeLine->content_len = baton->hunk->lines->at(i)->content_len;
    storeLine->content_offset = baton->hunk->lines->at(i)->content_offset;
    storeLine->content = strdup(baton->hunk->lines->at(i)->content);
    baton->lines->push_back(storeLine);
  }
}

void ConvenientHunk::LinesWorker::HandleErrorCallback() {
  while (!baton->lines->empty()) {
    free(baton->lines->back());
    baton->lines->pop_back();
  }

  delete baton->lines;
}

void ConvenientHunk::LinesWorker::HandleOKCallback() {
  unsigned int size = baton->lines->size();
  Local<Array> result = Nan::New<Array>(size);

  for(unsigned int i = 0; i < size; ++i) {
    Nan::Set(result, Nan::New<Number>(i), GitDiffLine::New(baton->lines->at(i), true));
  }

  delete baton->lines;

  Local<v8::Value> argv[2] = {
    Nan::Null(),
    result
  };
  callback->Call(2, argv, async_resource);

  delete baton;
}

NAN_METHOD(ConvenientHunk::OldStart) {
  Local<v8::Value> to;
  int old_start = Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetValue()->hunk.old_start;
  info.GetReturnValue().Set(Nan::New<Number>(old_start));
}


NAN_METHOD(ConvenientHunk::OldLines) {
  Local<v8::Value> to;
  int old_lines = Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetValue()->hunk.old_lines;
  info.GetReturnValue().Set(Nan::New<Number>(old_lines));
}

NAN_METHOD(ConvenientHunk::NewStart) {
  Local<v8::Value> to;
  int new_start = Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetValue()->hunk.new_start;
  info.GetReturnValue().Set(Nan::New<Number>(new_start));
}

NAN_METHOD(ConvenientHunk::NewLines) {
  Local<v8::Value> to;
  int new_lines = Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetValue()->hunk.new_lines;
  info.GetReturnValue().Set(Nan::New<Number>(new_lines));
}

NAN_METHOD(ConvenientHunk::HeaderLen) {
  Local<v8::Value> to;
  size_t header_len = Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetValue()->hunk.header_len;
  info.GetReturnValue().Set(Nan::New<Number>(header_len));
}

NAN_METHOD(ConvenientHunk::Header) {
  Local<v8::Value> to;

  char *header = Nan::ObjectWrap::Unwrap<ConvenientHunk>(info.This())->GetValue()->hunk.header;
  if (header) {
    to = Nan::New<String>(header).ToLocalChecked();
  } else {
    to = Nan::Null();
  }

  info.GetReturnValue().Set(to);
}

void ConvenientHunk::Reference() {
  Ref();
}

void ConvenientHunk::Unreference() {
  Unref();
}
