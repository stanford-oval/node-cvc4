//
// Created by gcampagn on 7/10/17.
//

#include <future>
#include <string>
#include <type_traits>

#include <node/node.h>
#include <uv.h>

#include <cvc4/cvc4.h>
#undef As

using v8::Isolate;
using v8::Locker;
using v8::HandleScope;
using v8::Local;
using v8::Context;
using v8::Object;
using v8::String;
using v8::Value;
using v8::SealHandleScope;
using v8::FunctionCallbackInfo;

namespace node_cvc4 {

inline Local <String>
to_javascript(Isolate *isolate, const std::u16string &s) {
    return v8::String::NewFromTwoByte(isolate, (const uint16_t *) s.data(),
                                      v8::String::NewStringType::kNormalString, s.length());
}
inline Local <String>
to_javascript(Isolate *isolate, const std::string &s) {
    return v8::String::NewFromOneByte(isolate, (const uint8_t *) s.data(),
                                      v8::String::NewStringType::kNormalString, s.length());
}

std::basic_string<char16_t>
v8_to_string(const Local <v8::String> &s) {
    int length = s->Length();
    char16_t *buffer = new char16_t[length];

    s->Write((uint16_t *) buffer, 0, -1, v8::String::NO_NULL_TERMINATION);
    std::basic_string<char16_t> stdstring = std::basic_string<char16_t>(buffer,
                                                                        (size_t) length);
    delete[] buffer;
    return stdstring;
}

v8::Local <v8::Value>
exception_to_v8(Isolate *isolate, const char *error_msg) {
    Local <String> v8_message = v8::String::NewFromOneByte(isolate, (uint8_t *) error_msg);
    return v8::Exception::Error(v8_message);
}

template<typename T>
class UVAsyncCall : private uv_work_t {
private:
    using function_type = T();
    std::packaged_task<function_type> task;
    std::future<T> future;
    v8::Isolate *isolate;
    v8::Global<v8::Promise::Resolver> js_promise;

    template<typename Callable>
    UVAsyncCall(v8::Isolate *_isolate, Callable&& _task) : task(std::forward<Callable>(_task)),
                                                           future(task.get_future()),
                                                           isolate(_isolate)
    {
        js_promise = v8::Global<v8::Promise::Resolver>(isolate, v8::Promise::Resolver::New(isolate));
    }

    static void do_work(uv_work_t* req) {
        UVAsyncCall<T> *self = static_cast<UVAsyncCall<T>*>(req);
        self->task();
    }

    static void do_after_work(uv_work_t* req, int status) {
        UVAsyncCall<T> *self = static_cast<UVAsyncCall<T>*>(req);
        {
            v8::Isolate *isolate = self->isolate;
            v8::HandleScope scope(isolate);
            v8::Local<v8::Promise::Resolver> promise(self->js_promise.Get(isolate));

            try {
                T value = self->future.get();
                promise->Resolve(to_javascript(isolate, value));
            } catch (std::exception &e) {
                promise->Reject(exception_to_v8(isolate, e.what()));
            }
        }
        delete self;
    }

    template<typename Callable>
    friend v8::Local<v8::Promise> Schedule(v8::Isolate *isolate, Callable&& _task);
};

template<typename Callable>
static v8::Local<v8::Promise> Schedule(v8::Isolate *isolate, Callable&& _task) {
    using result_type = decltype(std::forward<Callable>(_task)());
    UVAsyncCall<result_type> *req = new UVAsyncCall<result_type>(isolate, std::forward<Callable>(_task));
    v8::Local<v8::Promise::Resolver> resolver = req->js_promise.Get(isolate);
    v8::Local<v8::Promise> promise = resolver->GetPromise();

    uv_queue_work(uv_default_loop(), req, &UVAsyncCall<result_type>::do_work, &UVAsyncCall<result_type>::do_after_work);
    return promise;
}

static std::string do_solve()
{
    CVC4::ExprManager expr_manager;
    static std::mutex option_lock;
    static const char * const opt_strings[] = {
        "cvc4",
        "--dump-models",
        "--lang"
        "smt"
    };

    CVC4::Options &options(const_cast<CVC4::Options &>(expr_manager.getOptions()));
    {
        std::lock_guard<std::mutex> guard(option_lock);
        CVC4::Options::parseOptions(&options, sizeof(opt_strings) / sizeof(opt_strings[0]),
                                    (char **) opt_strings);
    }

    CVC4::SmtEngine engine(&expr_manager);

    CVC4::parser::ParserBuilder parser_builder(&expr_manager, "<http>", options);
    std::istringstream istream;
    std::ostringstream ostream;
    parser_builder
            .withInputLanguage(CVC4::language::input::LANG_SMTLIB_V2)
            .withIncludeFile(false)
            .withStreamInput(istream);

    std::unique_ptr<CVC4::parser::Parser> parser(parser_builder.build());

    while (!parser->done()) {
        std::unique_ptr<CVC4::Command> command(parser->nextCommand());
        if (command == nullptr)
            continue;
        command->invoke(&engine, ostream);
    }

    return ostream.str();
}

static void solve(const FunctionCallbackInfo<Value>& args)
{
    args.GetReturnValue().Set(Schedule(args.GetIsolate(), do_solve));
}

static void register_module(Local<Object> exports, Local<Value>, void *)
{
    NODE_SET_METHOD(exports, "solve", solve);
}

NODE_MODULE(cvc4, register_module);

}
