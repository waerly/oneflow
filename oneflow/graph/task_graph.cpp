#include "graph/task_graph.h"

namespace oneflow {

void TaskGraph::Init(const StageGraph* stage_dag,
                     const IDMap& id_map,
                     bool need_bp) {
  Stage2TndsMap stage2tnds;
  InitComputeTnds(stage_dag, id_map, &stage2tnds);
  InitBoxingTnds(stage_dag, id_map, &stage2tnds);
  ConnectTnds(stage_dag, &stage2tnds);
  UpdateStartAndStop();
  if (need_bp) {
    BuildBpStruct();
  }
}

void TaskGraph::InitComputeTnds(const StageGraph* stage_dag,
                                const IDMap& id_map,
                                Stage2TndsMap* stage2tnds) {
  for (const std::unique_ptr<Node>& node : stage_dag->node_vec()) {
    auto stage = of_dynamic_cast<const StageNode*> (node.get());
    bool is_first_stage = stage_dag->IsFirstNode(stage);
    bool is_last_stage = stage_dag->IsLastNode(stage);
    if (stage->parallel_desc().engine() == ParallelDesc::Engine::kDevice) {
      Stage2DeviceComputeTnds(stage,
                              id_map,
                              &((*stage2tnds)[stage]),
                              is_first_stage,
                              is_last_stage);
    } else {
      Stage2HostComputeTnds(stage, id_map, &((*stage2tnds)[stage]));
    }
  }
}

void TaskGraph::Stage2DeviceComputeTnds(const StageNode* stage,
                                        const IDMap& id_map,
                                        TndsWithinStage* tnds_within_stage,
                                        bool is_first_stage,
                                        bool is_last_stage) {
  MachineId machine_id = stage->machine_id();
  for (auto device_physical_id : stage->parallel_desc().devices_on_machine(machine_id)) {
    ThreadLocalId thread_local_id =
        id_map.ThreadLocalIdFromDevicePhysicalId(device_physical_id);
    // compute_tnd
    DeviceComputeTnd* compute_tnd = NewDeviceComputeTnd();
    compute_tnd->mutable_op_vec() = stage->op_vec();
    compute_tnd->mutable_parallel_desc_ptr() = stage->parallel_desc_ptr();
    compute_tnd->mutable_machine_id() = machine_id;
    compute_tnd->mutable_thread_local_id() = thread_local_id;
    // compute_in_tnd
    if (!is_first_stage) {
      CopyHDTnd* compute_in_tnd = NewCopyHDTnd();
      compute_in_tnd->mutable_machine_id() = machine_id;
      compute_in_tnd->mutable_thread_local_id() = thread_local_id;
      ConnectTwoNode(compute_in_tnd, compute_tnd);
      tnds_within_stage->compute_in_tnds.push_back(compute_in_tnd);
    } else {
      tnds_within_stage->compute_in_tnds.push_back(compute_tnd);
    }
    // compute_out_tnd
    if (!is_last_stage) {
      CopyHDTnd* compute_out_tnd = NewCopyHDTnd();
      compute_out_tnd->mutable_machine_id() = machine_id;
      compute_out_tnd->mutable_thread_local_id() = thread_local_id;
      ConnectTwoNode(compute_tnd, compute_out_tnd);
      tnds_within_stage->compute_out_tnds.push_back(compute_out_tnd);
    } else {
      tnds_within_stage->compute_out_tnds.push_back(compute_tnd);
    }
  }
}

void TaskGraph::Stage2HostComputeTnds(const StageNode* stage,
                                      const IDMap& id_map,
                                      TndsWithinStage* tnds_within_stage) {
  HostComputeTnd* compute_tnd = NewHostComputeTnd();
  compute_tnd->mutable_op_vec() = stage->op_vec();
  compute_tnd->mutable_parallel_desc_ptr() = stage->parallel_desc_ptr();
  compute_tnd->mutable_machine_id() = stage->machine_id();
  // since we only support GPU now, it must be a data-op
  compute_tnd->mutable_thread_local_id() = id_map.data_thread_local_id();
  tnds_within_stage->compute_in_tnds.push_back(compute_tnd);
  tnds_within_stage->compute_out_tnds.push_back(compute_tnd);
}

void TaskGraph::InitBoxingTnds(const StageGraph* stage_dag,
                               const IDMap& id_map,
                               Stage2TndsMap* stage2tnds) {
  for (const std::unique_ptr<Node>& node : stage_dag->node_vec()) {
    auto stage = of_dynamic_cast<const StageNode*> (node.get());
    InitInboxingTnd(stage, id_map, &(stage2tnds->at(stage)));
    InitOutBoxingTnd(stage, id_map, &(stage2tnds->at(stage)));
  }
}

void TaskGraph::InitInboxingTnd(const StageNode* stage,
                                const IDMap& id_map,
                                TndsWithinStage* tnds_within_stage) {
  tnds_within_stage->in_boxing_tnd = nullptr;
  if (stage->predecessors().size() == 1
      && tnds_within_stage->compute_in_tnds.size() == 1) {
    return;
  }
  BoxingTnd* boxing_tnd = NewBoxingTnd();
  boxing_tnd->mutable_machine_id() = stage->machine_id();
  boxing_tnd->mutable_thread_local_id() = id_map.boxing_thread_local_id();
  for (TaskNode* compute_in_tnd : tnds_within_stage->compute_in_tnds) {
    ConnectTwoNode(boxing_tnd, compute_in_tnd);
  }
  tnds_within_stage->in_boxing_tnd = boxing_tnd;
}

void TaskGraph::InitOutBoxingTnd(const StageNode* stage,
                                 const IDMap& id_map,
                                 TndsWithinStage* tnds_within_stage) {
  tnds_within_stage->out_boxing_tnd = nullptr;
  if (stage->successors().size() == 1
      && tnds_within_stage->compute_out_tnds.size() == 1) {
    return;
  }
  BoxingTnd* boxing_tnd = NewBoxingTnd();
  boxing_tnd->mutable_machine_id() = stage->machine_id();
  boxing_tnd->mutable_thread_local_id() = id_map.boxing_thread_local_id();
  for (TaskNode* compute_out_tnd : tnds_within_stage->compute_out_tnds) {
    ConnectTwoNode(compute_out_tnd, boxing_tnd);
  }
  tnds_within_stage->out_boxing_tnd = boxing_tnd;
}

void TaskGraph::ConnectTnds(const StageGraph* stage_dag,
                            const Stage2TndsMap* stage2tnds) {
  for (const std::unique_ptr<Node>& node : stage_dag->node_vec()) {
    auto cur_stage = of_dynamic_cast<const StageNode*> (node.get());
    const TndsWithinStage& cur_tnds = stage2tnds->at(cur_stage);
    TaskNode* out_node = cur_tnds.out_boxing_tnd;
    if (out_node == nullptr) {
      CHECK_EQ(cur_tnds.compute_out_tnds.size(), 1);
      out_node = cur_tnds.compute_out_tnds[0];
    }
    for (const Node* next : cur_stage->successors()) {
      auto next_stage = of_dynamic_cast<const StageNode*> (next);
      const TndsWithinStage& next_tnds = stage2tnds->at(next_stage);
      TaskNode* in_node = next_tnds.in_boxing_tnd;
      if (in_node == nullptr) {
        CHECK_EQ(next_tnds.compute_in_tnds.size(), 1);
        in_node = next_tnds.compute_in_tnds[0];
      }
      if (cur_stage->machine_id() == next_stage->machine_id()) {
        ConnectTwoNode(out_node, in_node);
      } else {
        CommNetTnd* out_comm_net_node = NewCommNetTnd();
        CommNetTnd* in_comm_net_node = NewCommNetTnd();
        ConnectTwoNode(out_node, out_comm_net_node);
        ConnectTwoNode(out_comm_net_node, in_comm_net_node);
        ConnectTwoNode(in_comm_net_node, in_node);
      }
    }
  }
}

void TaskGraph::GenerateRelatedBpNodes(
    std::function<void(const TaskNode*, TaskNode*)> add_fw_bp_pair,
    const std::unordered_map<const TaskNode*, TaskNode*>& fw_node2bp_node,
    std::vector<TaskNode*> *turning_node_vec) {
  for (auto node_it = begin(); node_it != end(); ++node_it) {
    auto task_node = of_dynamic_cast<TaskNode*> (&(*node_it));
    if (auto compute_tnd = dynamic_cast<ComputeTnd*> (task_node)) {
      if (compute_tnd->HasOpWithOutDiff()) {
        add_fw_bp_pair(task_node, task_node->ConstructBpNode());
      } else {
        if (compute_tnd->HasOpWithIndiff()) {
          turning_node_vec->push_back(task_node);
        }
      }
    } else {
      for (Node* pred_node : task_node->predecessors()) {
        if (fw_node2bp_node.find(of_dynamic_cast<TaskNode*> (pred_node)) !=
            fw_node2bp_node.end()) {
          add_fw_bp_pair(task_node, task_node->ConstructBpNode());
        }
      }
    }
  }
}

void TaskGraph::BackwardConnect(
    const std::unordered_map<const TaskNode*, TaskNode*>& fw_node2bp_node,
    const std::unordered_map<TaskNode*, const TaskNode*>& bp_node2fw_node,
    const std::vector<TaskNode*>& turning_node_vec) {
  std::queue<TaskNode*> bp_node_queue;
  for (TaskNode* turning_node : turning_node_vec) {
    for (Node* fw_pred_node : turning_node->predecessors()) {
      TaskNode* bp_pred_node =
          fw_node2bp_node.at(of_dynamic_cast<TaskNode*>(fw_pred_node));
      ConnectTwoNode(turning_node, bp_pred_node);
      bp_node_queue.push(bp_pred_node);
    }
  }
  while (!bp_node_queue.empty()) {
    TaskNode* bp_cur_node = bp_node_queue.front();
    bp_node_queue.pop();
    for (Node* fw_pred_node : bp_node2fw_node.at(bp_cur_node)->predecessors()) {
      TaskNode* bp_pred_node =
          fw_node2bp_node.at(of_dynamic_cast<TaskNode*>(fw_pred_node));
      ConnectTwoNode(bp_cur_node, bp_pred_node);
      bp_node_queue.push(bp_pred_node);
    }
  }
}

void TaskGraph::BuildBpStruct() {
  std::unordered_map<const TaskNode*, TaskNode*> fw_node2bp_node;
  std::unordered_map<TaskNode*, const TaskNode*> bp_node2fw_node;
  std::function<void(const TaskNode*, TaskNode*)> add_fw_bp_pair =
      [&fw_node2bp_node, &bp_node2fw_node]
      (const TaskNode* fw_node, TaskNode* bp_node) {
    fw_node2bp_node[fw_node] = bp_node;
    bp_node2fw_node[bp_node] = fw_node;
  };
  std::vector<TaskNode*> turning_node_vec;
  GenerateRelatedBpNodes(add_fw_bp_pair, fw_node2bp_node, &turning_node_vec);
  BackwardConnect(fw_node2bp_node, bp_node2fw_node, turning_node_vec);
  UpdateStartAndStop();
}

} // namespace oneflow
