#ifndef ONEFLOW_OPERATOR_CONCAT_OP_H_
#define ONEFLOW_OPERATOR_CONCAT_OP_H_

#include "operator/operator.h"

namespace oneflow {

class ConcatOp final : public UserOperator {
 public:
  OF_DISALLOW_COPY_AND_MOVE(ConcatOp);
  ConcatOp() = default;
  ~ConcatOp() = default;

  void InitFromOpConf(const OperatorConf& op_conf) override;

  std::string GetValueFromPbOpConf(const std::string& k) const override;
  std::string normal_ibn2lbn(const std::string& input_bn) const override {
    return ibn2lbn_.at(input_bn);
  }

  void InferShape4FwBlobs(
      std::function<Shape*(const std::string&)> GetShapePtr4BnInOp,
      ParallelPolicy policy,
      uint64_t parallel_id,
      uint64_t parallel_size) const override;

 private:
  std::unordered_map<std::string, std::string> ibn2lbn_;

};

} // namespace oneflow

#endif // ONEFLOW_OPERATOR_CONCAT_OP_H_
