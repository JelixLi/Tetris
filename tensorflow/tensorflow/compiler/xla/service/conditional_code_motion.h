/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_COMPILER_XLA_SERVICE_CONDITIONAL_CODE_MOTION_H_
#define TENSORFLOW_COMPILER_XLA_SERVICE_CONDITIONAL_CODE_MOTION_H_

#include "absl/strings/string_view.h"
#include "tensorflow/compiler/xla/service/hlo_module.h"
#include "tensorflow/compiler/xla/service/hlo_pass_interface.h"
#include "tensorflow/compiler/xla/statusor.h"

namespace xla {

namespace conditional_opt {
// At the conceptual level, a boundary can be thought of as representing a
// single virtual operation, except this virtual operation is conditionally
// instantiated into different concrete operations at each conditional branch.
// So a boundary is mapped to a single concrete operation if it is outside of
// conditional branches, and is mapped to a list of instructions if inside the
// branches. This data structure therefore allows a common data structure
// representation of the instructions to be moved, whether  they are inside or
// outside of the branches. Subsequently, it allows a common implementation
// basis to be used for both moving instructions out of and for moving them
// inside branches.
class Boundary {
 public:
  enum class Position { kInsideBranch, kOutsideBranch, kUndefined };
  Boundary() : position_(Position::kUndefined) {}
  explicit Boundary(Position p) : position_(p) {}
  std::vector<HloInstruction*>& mutable_operands() { return operands_; }
  const std::vector<HloInstruction*>& operands() const { return operands_; }
  bool IsInsideBranch() const { return position_ == Position::kInsideBranch; }
  bool IsOutsideBranch() const { return position_ == Position::kOutsideBranch; }
  Position GetPosition() const { return position_; }
  bool IsEmpty() const { return operands_.empty(); }
  std::string ToString() const {
    std::string res;
    for (HloInstruction* op : operands_) {
      res += op->ToString() + ";";
    }
    return res;
  }
  bool operator==(const Boundary& that) {
    return ContainersEqual(operands_, that.operands_);
  }

 private:
  // Boundary instructions in the conditional branches, one from each branch
  // of the conditional; or a single operand from outside the conditional.
  std::vector<HloInstruction*> operands_;
  Position position_;
};

// HLO pass that moves identical ops in/out of conditional.
// - The definition of identical are the shape of the operands are identical
// and their properties are identical.
// - Only the identical ops that won't share operands with other ops will
// be moved out of conditional.
class ConditionalCodeMotion : public HloModulePass {
 public:
  // If is_layout_sensitive is true, then the hoist process preserves layout
  // during identical comparison. Otherwise, layout is ignored.
  explicit ConditionalCodeMotion(bool is_layout_sensitive,
                                 bool pursue_full_conditional_code_motion)
      : is_layout_sensitive_(is_layout_sensitive),
        pursue_full_conditional_code_motion_(
            pursue_full_conditional_code_motion) {}
  absl::string_view name() const override { return "conditional-code-motion"; }
  StatusOr<bool> Run(HloModule* module) override;

  // Optimization decision for each boundary of the conditional instruction.
  class Decision {
   public:
    enum class Direction : uint8 {
      kMoveOutOfBranch,
      kMoveIntoBranch,
      kNoChange
    };

   public:
    Decision(Direction direction, int benefit)
        : direction_(direction), benefit_(benefit) {}
    Direction GetDirection() const { return direction_; }
    int GetBenefit() const { return benefit_; }

   private:
    Direction direction_;
    int benefit_;
  };
  // If the optimization decision is NO_CHANGE, new_boundary is set to nullptr;
  // otherwise, it is set to the new boundary after proposed optimization.
  virtual Decision ConsiderCodeMotion(
      HloInstruction* conditional, const Boundary& cur_boundary,
      std::vector<Boundary>& to_move, std::vector<Boundary>& new_boundaries,
      absl::flat_hash_map<HloInstruction*, int>& visited_count);

 private:
  const bool is_layout_sensitive_;
  const bool pursue_full_conditional_code_motion_;

  StatusOr<bool> MoveInstructionOut(HloInstruction* conditional,
                                    std::vector<Boundary>& to_move_out,
                                    std::vector<Boundary>& new_boundaries);
  StatusOr<bool> MoveInstructionIn(HloInstruction* conditional,
                                   std::vector<Boundary>& to_move_in,
                                   std::vector<Boundary>& new_boundaries);
};
}  // namespace conditional_opt

}  // namespace xla

#endif  // TENSORFLOW_COMPILER_XLA_SERVICE_CONDITIONAL_CODE_MOTION_H_
