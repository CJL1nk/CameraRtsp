#pragma once

#include <jni.h>

class ScopedEnvironment {
public:
    explicit ScopedEnvironment(JavaVM* vm) : vm_(vm), attached_(false), env_(nullptr) {
        if (vm_->GetEnv(reinterpret_cast<void **>(&env_), JNI_VERSION_1_6) != JNI_OK) {
            if (vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
                attached_ = true;
            }
        }
    }

    ~ScopedEnvironment() {
        if (attached_) {
            vm_->DetachCurrentThread();
            attached_ = false;
        }
    }

    JNIEnv* env() const { return env_; }

private:
    JavaVM* vm_;
    bool attached_;
    JNIEnv* env_;
};
