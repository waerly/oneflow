#ifndef ONEFLOW_CORE_FRAMEWORK_OP_KERNEL_H_
#define ONEFLOW_CORE_FRAMEWORK_OP_KERNEL_H_

#include "oneflow/core/common/util.h"
#include "oneflow/core/device/device_context.h"
#include "oneflow/core/framework/blob.h"
#include "oneflow/core/framework/user_op_conf.h"

namespace std {

template<>
struct hash<std::pair<std::string, int32_t>> {
  std::size_t operator()(const std::pair<std::string, int32_t>& p) const {
    return std::hash<std::string>{}(p.first) ^ std::hash<int32_t>{}(p.second);
  }
};

}  // namespace std

namespace oneflow {

class KernelCtx;

namespace user_op {

class KernelInitContext final {
 public:
  KernelInitContext() = default;
  ~KernelInitContext() = default;
  explicit KernelInitContext(const KernelInitContext&) {}

 private:
};

using ArgNameAndIndex2Blob =
    HashMap<std::pair<std::string, int32_t>, std::unique_ptr<user_op::Blob>>;

class KernelContext final {
 public:
  KernelContext() = default;
  ~KernelContext() = default;
  explicit KernelContext(DeviceCtx*, ArgNameAndIndex2Blob&&, UserOpConfWrapper&&);

  user_op::Blob* Blob4ArgNameAndIndex(const std::string&, int32_t);
  DeviceCtx* device_ctx() const { return device_ctx_; }
  template<typename T>
  T GetAttr(const std::string& attr_name) const {
    return user_op_conf_.attr<T>(attr_name);
  }

 private:
  DeviceCtx* device_ctx_;
  ArgNameAndIndex2Blob blobs_;
  UserOpConfWrapper user_op_conf_;
};

class OpKernel {
 public:
  OF_DISALLOW_COPY_AND_MOVE(OpKernel);
  virtual ~OpKernel() = default;

  virtual void Compute(KernelContext*) = 0;

 protected:
  OpKernel(const KernelInitContext&) {}

 private:
};

}  // namespace user_op

}  // namespace oneflow

#endif
