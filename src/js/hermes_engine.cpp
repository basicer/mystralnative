// Hermes JavaScript engine adapter, implemented on Hermes' JSI embedding API.
#include "mystral/js/engine.h"

#if defined(MYSTRAL_JS_HERMES)
#include <hermes/hermes.h>
#include <hermes/Public/RuntimeConfig.h>
#include <jsi/instrumentation.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace mystral::js {
namespace {
namespace jsi = facebook::jsi;

class OwnedBuffer final : public jsi::MutableBuffer {
public:
    OwnedBuffer(const uint8_t* source, size_t length)
        : bytes_(length) {
        if (source && length) std::memcpy(bytes_.data(), source, length);
    }
    size_t size() const override { return bytes_.size(); }
    uint8_t* data() override { return bytes_.data(); }

private:
    std::vector<uint8_t> bytes_;
};

class ExternalBuffer final : public jsi::MutableBuffer {
public:
    ExternalBuffer(void* data, size_t length)
        : data_(static_cast<uint8_t*>(data)), length_(length) {}
    size_t size() const override { return length_; }
    uint8_t* data() override { return data_; }

private:
    uint8_t* data_;
    size_t length_;
};

class BytecodeBuffer final : public jsi::Buffer {
public:
    BytecodeBuffer(const uint8_t* data, size_t length)
        : bytes_(data, data + length) {}
    size_t size() const override { return bytes_.size(); }
    const uint8_t* data() const override { return bytes_.data(); }

private:
    std::vector<uint8_t> bytes_;
};

class PrivateDataState final : public jsi::NativeState {
public:
    void* data = nullptr;
    std::function<void()> release;
    ~PrivateDataState() override {
        if (release) release();
    }
};

class HermesEngine final : public Engine {
public:
    explicit HermesEngine(std::unique_ptr<facebook::hermes::HermesRuntime> runtime)
        : runtime_(std::move(runtime)), startTime_(std::chrono::steady_clock::now()) { setupGlobals(); }

    EngineType getType() const override { return EngineType::Hermes; }
    const char* getName() const override { return "Hermes"; }
    bool eval(const char* code, const char* filename) override { return evalWithResult(code, filename).ptr != nullptr; }
    JSValueHandle evalWithResult(const char* code, const char* filename) override {
        try {
            return store(runtime_->evaluateJavaScript(std::make_shared<jsi::StringBuffer>(code ? code : ""), filename ? filename : "<eval>"));
        } catch (const std::exception& e) {
            capture(e);
            return {};
        }
    }
    bool evalScript(const char* code, const char* filename) override { return eval(code, filename); }
    JSValueHandle evalScriptWithResult(const char* code, const char* filename) override { return evalWithResult(code, filename); }
    bool evalBytecode(const uint8_t* data, size_t length, const char* filename) override {
        try {
            auto* api = facebook::jsi::castInterface<facebook::hermes::IHermesRootAPI>(
                facebook::hermes::makeHermesRootAPI());
            std::string validationError;
            if (!api->hermesBytecodeSanityCheck(data, length, &validationError)) {
                lastException_ = "Invalid Hermes bytecode";
                if (!validationError.empty()) {
                    lastException_ += ": " + validationError;
                }
                return false;
            }
            runtime_->evaluateJavaScript(std::make_shared<BytecodeBuffer>(data, length), filename ? filename : "<bytecode>");
            return true;
        } catch (const std::exception& e) {
            capture(e);
            return false;
        }
    }

    JSValueHandle getGlobal() override { return store(jsi::Value(runtime_->global())); }
    bool setGlobalProperty(const char* name, JSValueHandle value) override { return setProperty(getGlobal(), name, value); }
    JSValueHandle getGlobalProperty(const char* name) override { return getProperty(getGlobal(), name); }

    JSValueHandle newUndefined() override { return store(jsi::Value::undefined()); }
    JSValueHandle newNull() override { return store(jsi::Value::null()); }
    JSValueHandle newBoolean(bool value) override { return store(jsi::Value(value)); }
    JSValueHandle newNumber(double value) override { return store(jsi::Value(value)); }
    JSValueHandle newString(const char* value) override { return store(jsi::Value(jsi::String::createFromUtf8(*runtime_, value ? value : ""))); }
    JSValueHandle newObject() override { return store(jsi::Value(jsi::Object(*runtime_))); }
    JSValueHandle newArray(size_t length) override { return store(jsi::Value(jsi::Array(*runtime_, length))); }
    JSValueHandle newArrayBuffer(const uint8_t* data, size_t length) override {
        return store(jsi::Value(jsi::ArrayBuffer(*runtime_, std::make_shared<OwnedBuffer>(data, length))));
    }
    JSValueHandle newArrayBufferExternal(void* data, size_t length) override {
        return store(jsi::Value(jsi::ArrayBuffer(*runtime_, std::make_shared<ExternalBuffer>(data, length))));
    }
    void* getArrayBufferData(JSValueHandle value, size_t* size) override {
        try {
            const auto* v = get(value);
            if (!v || !v->isObject()) return nullptr;
            auto object = v->asObject(*runtime_);
            if (object.isArrayBuffer(*runtime_)) {
                auto buffer = object.getArrayBuffer(*runtime_);
                if (size) *size = buffer.size(*runtime_);
                return buffer.data(*runtime_);
            }
            auto bufferValue = object.getProperty(*runtime_, "buffer");
            if (!bufferValue.isObject()) return nullptr;
            auto bufferObject = bufferValue.asObject(*runtime_);
            if (!bufferObject.isArrayBuffer(*runtime_)) return nullptr;
            auto buffer = bufferObject.getArrayBuffer(*runtime_);
            auto offset = object.getProperty(*runtime_, "byteOffset");
            auto byteLength = object.getProperty(*runtime_, "byteLength");
            const size_t byteOffset = offset.isNumber() ? static_cast<size_t>(offset.getNumber()) : 0;
            if (byteOffset > buffer.size(*runtime_)) return nullptr;
            if (size) *size = byteLength.isNumber() ? static_cast<size_t>(byteLength.getNumber()) : buffer.size(*runtime_) - byteOffset;
            return buffer.data(*runtime_) + byteOffset;
        } catch (const std::exception& e) {
            capture(e);
            return nullptr;
        }
    }
    JSValueHandle createFloat32Array(const float* data, size_t count) override { return createTypedArray("Float32Array", data, count * sizeof(float)); }
    JSValueHandle createFloat32ArrayView(float* data, size_t count) override { return createTypedArray("Float32Array", data, count * sizeof(float), true); }
    JSValueHandle createUint32Array(const uint32_t* data, size_t count) override { return createTypedArray("Uint32Array", data, count * sizeof(uint32_t)); }
    JSValueHandle createUint8Array(const uint8_t* data, size_t count) override { return createTypedArray("Uint8Array", data, count); }

    JSValueHandle newFunction(const char* name, NativeFunction fn) override { return createHostFunction(name, std::move(fn)); }
    JSValueHandle newConstructor(const char* name, NativeFunction fn) override {
        // Hermes host functions are constructable and preserve a returned object.
        return createHostFunction(name, std::move(fn));
    }

    bool toBoolean(JSValueHandle value) override {
        const auto* v = get(value);
        if (!v || v->isNull() || v->isUndefined()) return false;
        if (v->isBool()) return v->getBool();
        return !v->isNumber() || (v->getNumber() != 0 && !std::isnan(v->getNumber()));
    }
    double toNumber(JSValueHandle value) override {
        const auto* v = get(value);
        if (!v || v->isNull()) return 0;
        if (v->isUndefined()) return std::numeric_limits<double>::quiet_NaN();
        if (v->isNumber()) return v->getNumber();
        if (v->isBool()) return v->getBool() ? 1 : 0;

        // JSI asNumber() does not perform JavaScript coercion.
        try {
            const std::string text = v->toString(*runtime_).utf8(*runtime_);
            size_t consumed = 0;
            const double result = std::stod(text, &consumed);
            return consumed == text.size() ? result : std::numeric_limits<double>::quiet_NaN();
        } catch (const std::exception&) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }
    std::string toString(JSValueHandle value) override {
        try {
            return get(value) ? get(value)->toString(*runtime_).utf8(*runtime_) : "";
        } catch (const std::exception& e) {
            capture(e);
            return "";
        }
    }
    bool isUndefined(JSValueHandle value) override { return !get(value) || get(value)->isUndefined(); }
    bool isNull(JSValueHandle value) override { return get(value) && get(value)->isNull(); }
    bool isBoolean(JSValueHandle value) override { return get(value) && get(value)->isBool(); }
    bool isNumber(JSValueHandle value) override { return get(value) && get(value)->isNumber(); }
    bool isString(JSValueHandle value) override { return get(value) && get(value)->isString(); }
    bool isObject(JSValueHandle value) override { return get(value) && get(value)->isObject(); }
    bool isArray(JSValueHandle value) override {
        try {
            return isObject(value) && get(value)->asObject(*runtime_).isArray(*runtime_);
        } catch (const std::exception& e) {
            capture(e);
            return false;
        }
    }
    bool isFunction(JSValueHandle value) override {
        try {
            return isObject(value) && get(value)->asObject(*runtime_).isFunction(*runtime_);
        } catch (const std::exception& e) {
            capture(e);
            return false;
        }
    }

    bool setProperty(JSValueHandle obj, const char* name, JSValueHandle value) override {
        try {
            auto* o = get(obj);
            auto* v = get(value);
            if (!o || !v || !o->isObject()) return false;
            o->asObject(*runtime_).setProperty(*runtime_, name, clone(*v));
            return true;
        } catch (const std::exception& e) {
            capture(e);
            return false;
        }
    }
    JSValueHandle getProperty(JSValueHandle obj, const char* name) override {
        try {
            auto* o = get(obj);
            return o && o->isObject() ? store(o->asObject(*runtime_).getProperty(*runtime_, name)) : JSValueHandle{};
        } catch (const std::exception& e) {
            capture(e);
            return {};
        }
    }
    bool setPropertyIndex(JSValueHandle arr, uint32_t index, JSValueHandle value) override {
        try {
            auto* a = get(arr);
            auto* v = get(value);
            if (!a || !v || !a->isObject()) return false;
            // Numeric properties support both ordinary and typed arrays.
            a->asObject(*runtime_).setProperty(*runtime_, std::to_string(index).c_str(), clone(*v));
            return true;
        } catch (const std::exception& e) {
            capture(e);
            return false;
        }
    }
    JSValueHandle getPropertyIndex(JSValueHandle arr, uint32_t index) override {
        try {
            auto* a = get(arr);
            if (!a || !a->isObject()) return {};
            return store(a->asObject(*runtime_).getProperty(*runtime_, std::to_string(index).c_str()));
        } catch (const std::exception& e) {
            capture(e);
            return {};
        }
    }
    JSValueHandle call(JSValueHandle func, JSValueHandle thisArg, const std::vector<JSValueHandle>& args) override {
        try {
            auto* fn = get(func);
            if (!fn || !fn->isObject()) return {};
            auto function = fn->asObject(*runtime_).asFunction(*runtime_);
            std::vector<jsi::Value> values;
            values.reserve(args.size());
            for (auto arg : args) values.push_back(get(arg) ? clone(*get(arg)) : jsi::Value::undefined());
            auto* thisValue = get(thisArg);
            const auto* arguments = static_cast<const jsi::Value*>(values.data());
            auto result = thisValue && thisValue->isObject() ? function.callWithThis(*runtime_, thisValue->asObject(*runtime_), arguments, values.size()) : function.call(*runtime_, arguments, values.size());
            runtime_->drainMicrotasks();
            return store(std::move(result));
        } catch (const std::exception& e) {
            capture(e);
            return {};
        }
    }
    void protect(JSValueHandle) override {}
    void unprotect(JSValueHandle) override {}
    void gc() override { runtime_->instrumentation().collectGarbage("requested by MystralNative"); }
    void drainMicrotasks() override { runtime_->drainMicrotasks(); }

    void registerRelease(JSValueHandle obj, std::function<void()> callback) override {
        updateState(obj, [callback = std::move(callback)](PrivateDataState& state) mutable { state.release = std::move(callback); });
    }
    bool hasException() override { return !lastException_.empty(); }
    std::string getException() override { return std::exchange(lastException_, {}); }
    void throwException(const char* message) override {
        lastException_ = message ? message : "Hermes exception";
        throw jsi::JSError(*runtime_, lastException_);
    }
    void setPrivateData(JSValueHandle obj, void* data) override {
        updateState(obj, [data](PrivateDataState& state) { state.data = data; });
    }
    void* getPrivateData(JSValueHandle obj) override {
        try {
            auto* v = get(obj);
            if (!v || !v->isObject()) return nullptr;
            auto o = v->asObject(*runtime_);
            return o.hasNativeState<PrivateDataState>(*runtime_) ? o.getNativeState<PrivateDataState>(*runtime_)->data : nullptr;
        } catch (const std::exception& e) {
            capture(e);
            return nullptr;
        }
    }
    void* getRawContext() override { return runtime_.get(); }

private:
    jsi::Value clone(const jsi::Value& value) const { return jsi::Value(*runtime_, value); }
    const jsi::Value* get(JSValueHandle value) const { return static_cast<const jsi::Value*>(value.ptr); }
    JSValueHandle store(jsi::Value value) {
        values_.push_back(std::make_unique<jsi::Value>(std::move(value)));
        return {values_.back().get(), runtime_.get()};
    }
    void capture(const std::exception& e) {
        lastException_ = e.what();
        std::cerr << "[Hermes] " << lastException_ << std::endl;
    }
    template <typename Mutator>
    void updateState(JSValueHandle value, Mutator mutate) {
        try {
            auto* v = get(value);
            if (!v || !v->isObject()) return;
            auto o = v->asObject(*runtime_);
            auto state = o.hasNativeState<PrivateDataState>(*runtime_) ? o.getNativeState<PrivateDataState>(*runtime_) : std::make_shared<PrivateDataState>();
            mutate(*state);
            if (!o.hasNativeState<PrivateDataState>(*runtime_)) o.setNativeState(*runtime_, std::move(state));
        } catch (const std::exception& e) { capture(e); }
    }
    JSValueHandle createTypedArray(const char* type, const void* data, size_t bytes, bool external = false) {
        auto buffer = external ? newArrayBufferExternal(const_cast<void*>(data), bytes) : newArrayBuffer(static_cast<const uint8_t*>(data), bytes);
        try {
            auto ctor = getGlobalProperty(type);
            auto* c = get(ctor);
            auto* b = get(buffer);
            if (!c || !b || !c->isObject()) return {};
            return store(c->asObject(*runtime_).asFunction(*runtime_).callAsConstructor(*runtime_, clone(*b)));
        } catch (const std::exception& e) {
            capture(e);
            return {};
        }
    }
    JSValueHandle createHostFunction(const char* name, NativeFunction callback) {
        auto function = jsi::Function::createFromHostFunction(*runtime_, jsi::PropNameID::forUtf8(*runtime_, name ? name : ""), 0,
                                                              [this, callback = std::move(callback)](jsi::Runtime&, const jsi::Value&, const jsi::Value* args, size_t count) -> jsi::Value {
                                                                  std::vector<JSValueHandle> nativeArgs;
                                                                  nativeArgs.reserve(count);
                // Callbacks retained by native APIs must outlive this host call.
                                                                  for (size_t i = 0; i < count; ++i) {
                                                                      nativeArgs.push_back(store(clone(args[i])));
                                                                  }
                                                                  auto result = callback(runtime_.get(), nativeArgs);
                                                                  const auto* resultValue = get(result);
                                                                  return resultValue ? clone(*resultValue) : jsi::Value::undefined();
                                                              });
        return store(jsi::Value(std::move(function)));
    }
    void setupGlobals() {
        auto console = jsi::Object(*runtime_);
        const auto logger = [this](jsi::Runtime&, const jsi::Value&, const jsi::Value* args, size_t count) { for (size_t i = 0; i < count; ++i) std::cout << args[i].toString(*runtime_).utf8(*runtime_) << (i + 1 == count ? '\n' : ' '); return jsi::Value::undefined(); };
        for (const char* name : {"log", "info", "warn", "error", "debug"}) console.setProperty(*runtime_, name, jsi::Function::createFromHostFunction(*runtime_, jsi::PropNameID::forAscii(*runtime_, name), 0, logger));
        runtime_->global().setProperty(*runtime_, "console", std::move(console));
        auto performance = jsi::Object(*runtime_);
        performance.setProperty(*runtime_, "now", jsi::Function::createFromHostFunction(*runtime_, jsi::PropNameID::forAscii(*runtime_, "now"), 0, [this](jsi::Runtime&, const jsi::Value&, const jsi::Value*, size_t) { return jsi::Value(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime_).count()); }));
        runtime_->global().setProperty(*runtime_, "performance", std::move(performance));
    }
    std::unique_ptr<facebook::hermes::HermesRuntime> runtime_;
    std::vector<std::unique_ptr<jsi::Value>> values_;
    std::string lastException_;
    std::chrono::steady_clock::time_point startTime_;
};
} // namespace

std::unique_ptr<Engine> createHermesEngine() {
    // static_h enables modern JavaScript and microtasks in its default config.
    auto runtime = facebook::hermes::makeHermesRuntime();
    if (!runtime) {
        std::cerr << "[Hermes] Failed to create runtime" << std::endl;
        return nullptr;
    }
    return std::make_unique<HermesEngine>(std::move(runtime));
}
} // namespace mystral::js
#endif
